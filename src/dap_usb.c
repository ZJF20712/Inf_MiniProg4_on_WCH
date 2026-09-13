/********************************** (C) COPYRIGHT *******************************
 * File Name          : dap_usb.c
 * Description        : CMSIS-DAP v2 (bulk) request/response ring buffer,
 *                      mirrors the KitProg3 bulk-mode data path.
 *
 *  Endpoint 2 (OUT) receives 64-byte DAP command packets. Every packet is
 *  queued (except ID_DAP_TransferAbort which acts immediately) and executed
 *  in the main loop via DAP_ExecuteCommand(). Responses go out on Endpoint 1
 *  (IN) as short packets - the v2 bulk protocol has no report-id prefix.
*******************************************************************************/
#include <string.h>
#include "usb_lib.h"
#include "usb_desc.h"
#include "usb_regs.h"
#include "dap_usb.h"
#include "debug.h"
#include "DAP_config.h"
#include "DAP.h"

/* Use queue depth that fits in the 10KB RAM of CH32V203G6 */
#if DAP_PACKET_COUNT > 8
#undef DAP_PACKET_COUNT
#define DAP_PACKET_COUNT 8
#endif

static uint8_t  USB_Request[DAP_PACKET_COUNT][DAP_PACKET_SIZE];
static uint8_t  USB_Response[DAP_PACKET_COUNT][DAP_PACKET_SIZE];
static uint16_t USB_ResponseSize[DAP_PACKET_COUNT];

static volatile uint32_t ReqIn   = 0;
static volatile uint32_t ReqOut  = 0;
static volatile uint8_t  ReqFull = 0;

static volatile uint32_t RespIn   = 0;
static volatile uint32_t RespOut  = 0;
static volatile uint8_t  RespFull = 0;
static volatile uint8_t  RespIdle = 1;   /* no packet in flight on EP1 */

static void DAP_SendResponse(void)
{
#ifdef DAP_FW_V1
    /* HID: always send full 64-byte report (no report ID, padded) */
    USB_SIL_Write(EP1_IN, (uint8_t *)USB_Response[RespOut], DAP_PACKET_SIZE);
    SetEPTxValid(ENDP1);
#else
    USB_SIL_Write(EP2_IN, (uint8_t *)USB_Response[RespOut], USB_ResponseSize[RespOut]);
    SetEPTxValid(ENDP2);
#endif

    uint32_t n = RespOut + 1;
    if (n == DAP_PACKET_COUNT) n = 0;
    RespOut = n;
    RespFull = 0;
}

/* Called by EP1 IN interrupt context: packet accepted by host.
 * NEVER call DAP_SendResponse from ISR - just set the idle flag.
 * The main loop (DAP_Process) will send the next queued response.
 * Calling SetEPTxValid from ISR deadlocks the toggle verification loop. */
void DAP_EndpointInDone(void)
{
    RespIdle = 1;
}

/* Called by EP OUT interrupt context (EP1 bulk / EP2 HID) */
void DAP_EndpointOut(void)
{
    uint8_t buf[DAP_PACKET_SIZE];

#ifdef DAP_FW_V1
    USB_SIL_Read(EP2_OUT, buf);         /* HID: commands arrive on EP2 OUT */
    SetEPRxStatus(ENDP2, EP_RX_VALID);
#else
    USB_SIL_Read(EP1_OUT, buf);         /* Bulk: commands arrive on EP1 OUT */
    SetEPRxStatus(ENDP1, EP_RX_VALID);
#endif

    if (buf[0] == ID_DAP_TransferAbort)
    {
        DAP_TransferAbort = 1;
        return;                     /* abort is never queued */
    }

    if (ReqFull)
        return;                     /* queue full: packet dropped */

    memcpy((uint8_t *)USB_Request[ReqIn], buf, DAP_PACKET_SIZE);

    uint32_t n = ReqIn + 1;
    if (n == DAP_PACKET_COUNT) n = 0;
    ReqIn = n;
    if (ReqIn == ReqOut) ReqFull = 1;
}

/* Main loop: execute queued commands, kick the first response out */
void DAP_Process(void)
{
    /* keep the response queue filled while the request queue drains */
    while ((ReqOut != ReqIn || ReqFull) && !RespFull)
    {
        uint16_t n_resp;
        /* SWD bit-bang is time-critical: a mid-transfer USB ISR would stretch
         * clock edges and can fault the CCG5 test controller on long blocks */
        __disable_irq();
        n_resp = (uint16_t)DAP_ExecuteCommand(USB_Request[ReqOut], USB_Response[RespIn]);
        __enable_irq();

        uint32_t n = ReqOut + 1;
        if (n == DAP_PACKET_COUNT) n = 0;
        ReqOut = n;
        ReqFull = 0;

        USB_ResponseSize[RespIn] = n_resp;
        n = RespIn + 1;
        if (n == DAP_PACKET_COUNT) n = 0;
        RespIn = n;
        if (RespIn == RespOut) RespFull = 1;

        /* Send response IMMEDIATELY - before any debug printing. */
        if (RespIdle)
        {
            RespIdle = 0;
            DAP_SendResponse();
        }
        /* Minimal trace after response is sent */
        Debug_Print("D%02x ", USB_Request[ReqOut == 0 ? DAP_PACKET_COUNT-1 : ReqOut-1][0]);
    }

    if (RespIdle && (RespOut != RespIn || RespFull))
    {
        RespIdle = 0;
        DAP_SendResponse();
    }
}
