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

/* Wedge post-mortem: g_diagNow mirrors the command about to execute; on a
 * main-loop stall these survive in SRAM (the request ring itself gets
 * overwritten by later host packets). Dump via SWD after the hang. */
volatile uint8_t  g_diagNow[8];
volatile uint8_t  g_diagLast[8];
volatile uint32_t g_diagSeq  = 0;
volatile uint32_t g_diagExec = 0;

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

/* Called by EP OUT interrupt context (EP1 bulk / EP2 HID).
 * NOTE: no __disable_irq/__enable_irq here - this runs in the WCH HPE fast
 * USB ISR where forcing GIE wedged the whole interrupt system (SysTick and
 * main loop dead, ISTR flags pending forever). The main loop wraps its own
 * queue updates instead. */
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
    {
        /* queue full: drop the content but the endpoint above is already
         * re-armed - returning without re-arming killed the command channel
         * for the rest of the session (host: 'Failed to send packet') */
        return;
    }

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
        {
            uint8_t k;
            for (k = 0; k < 8; k++) g_diagLast[k] = g_diagNow[k];
            g_diagNow[0] = USB_Request[ReqOut][0];
            g_diagNow[1] = USB_Request[ReqOut][1];
            g_diagNow[2] = USB_Request[ReqOut][2];
            g_diagNow[3] = USB_Request[ReqOut][3];
            g_diagNow[4] = RespIdle;
            g_diagNow[5] = RespFull;
            g_diagNow[6] = ReqFull;
            g_diagNow[7] = (uint8_t)g_diagSeq;
            g_diagSeq++;
        }
        /* NO global IRQ masking around DAP_ExecuteCommand: long commands
         * (KHPI 0x80 acquire hammers for tens of ms) freeze SysTick_ms with
         * IRQs off, which made the acquire window loop never exit - the probe
         * hammered SWD forever, deaf to USB (GUI hang, Scan timeout). */
        n_resp = (uint16_t)DAP_ExecuteCommand(USB_Request[ReqOut], USB_Response[RespIn]);
        g_diagExec++;

        /* Advance the request index. Deliberately NOT irq-masked: forcing
         * GIE from thread context raced the HPE fast-USB ISR and left the
         * chip with interrupts permanently disabled (SysTick/USB dead, probe
         * deaf mid-program). The residual races only lose/duplicate single
         * commands, which hosts recover from. */
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
        /* NO per-command tracing here: at the 1ms HID cadence of a flash
         * program stream the 1.3ms UART print per command starves the
         * 8-deep queue and the overflowed packets used to kill the pipe. */
    }

    if (RespIdle && (RespOut != RespIn || RespFull))
    {
        RespIdle = 0;
        DAP_SendResponse();
    }
}
