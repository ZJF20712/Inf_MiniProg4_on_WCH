/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_istr.c
 * Description        : USB interrupt service routines + endpoint data pumps
 *                      for WCH-MiniProg4.
*******************************************************************************/
#include "usb_lib.h"
#include "usb_conf.h"
#include "usb_prop.h"
#include "usb_pwr.h"
#include "usb_istr.h"
#include "usb_desc.h"
#include "usb_mem.h"
#include "vcom_serial.h"
#include "dap_usb.h"
#include "bridge.h"
#include "debug.h"

__IO uint16_t wIstr;
__IO uint8_t  bIntPackSOF = 0;
__IO uint32_t esof_counter = 0;
__IO uint32_t wCNTR = 0;

/* non-control endpoint service routines */
void (*pEpInt_IN[7])(void) = {
    EP1_IN_Callback,
    EP2_IN_Callback,
    EP3_IN_Callback,
    EP4_IN_Callback,
    EP5_IN_Callback,
    EP6_IN_Callback,
    EP7_IN_Callback,
};

void (*pEpInt_OUT[7])(void) = {
    EP1_OUT_Callback,
    EP2_OUT_Callback,
    EP3_OUT_Callback,
    EP4_OUT_Callback,
    EP5_OUT_Callback,
    EP6_OUT_Callback,
    EP7_OUT_Callback,
};

volatile uint32_t istrCount = 0;
volatile uint32_t resetCount = 0;
void USB_Istr(void)
{
    istrCount++;
    wIstr = _GetISTR();
    if (wIstr & ISTR_RESET) resetCount++;

#if (IMR_MSK & ISTR_SOF)
    if (wIstr & ISTR_SOF & wInterrupt_Mask)
    {
        _SetISTR((uint16_t)CLR_SOF);
        bIntPackSOF++;
    }
#endif

#if (IMR_MSK & ISTR_CTR)
    if (wIstr & ISTR_CTR & wInterrupt_Mask)
    {
        CTR_LP();
    }
#endif

#if (IMR_MSK & ISTR_RESET)
    if (wIstr & ISTR_RESET & wInterrupt_Mask)
    {
        _SetISTR((uint16_t)CLR_RESET);
        Debug_Print("[USB] bus reset\r\n");
        Device_Property.Reset();
    }
#endif

#if (IMR_MSK & ISTR_SUSP)
    if (wIstr & ISTR_SUSP & wInterrupt_Mask)
    {
        /* Bus idle: do NOT power down. A debug probe must stay live when the
         * host resumes - a frozen probe just times out on the host side. */
        _SetISTR((uint16_t)CLR_SUSP);
        Debug_Print("[USB] suspend ignored\r\n");
    }
#endif

#if (IMR_MSK & ISTR_WKUP)
    if (wIstr & ISTR_WKUP & wInterrupt_Mask)
    {
        _SetISTR((uint16_t)CLR_WKUP);
        Debug_Print("[USB] wakeup\r\n");
        Resume(RESUME_INTERNAL);
    }
#endif

#if (IMR_MSK & ISTR_ERR)
    if (wIstr & ISTR_ERR & wInterrupt_Mask)
    {
        _SetISTR((uint16_t)CLR_ERR);
    }
#endif

#if (IMR_MSK & ISTR_ESOF)
    if (wIstr & ISTR_ESOF & wInterrupt_Mask)
    {
        _SetISTR((uint16_t)CLR_ESOF);
        esof_counter++;
    }
#endif
}

/*******************************************************************************
* endpoint callbacks
*******************************************************************************/

/* DAP endpoints move with the runtime mode:
 *   v1 HID : EP2 OUT = commands, EP1 IN = responses
 *   v2 bulk: EP1 OUT = commands, EP2 IN = responses */
void EP1_OUT_Callback(void)
{
    if (g_dapV2Mode) DAP_EndpointOut();
}

void EP2_OUT_Callback(void)
{
    if (!g_dapV2Mode) DAP_EndpointOut();
}

void EP1_IN_Callback(void)
{
    if (!g_dapV2Mode) DAP_EndpointInDone();
}

void EP2_IN_Callback(void)
{
    if (g_dapV2Mode) DAP_EndpointInDone();
}

/* CDC data IN: packet accepted, allow VCOM to send next */
void EP4_IN_Callback(void)
{
    Vcom.ep_in_ready = 1;
}

/* CDC data OUT: packet from host */
void EP5_OUT_Callback(void)
{
    Vcom.out_bytes = USB_SIL_Read(EP5_OUT, (uint8_t *)Vcom.out_buff);
}

/* Bridge bulk OUT: command packet from host (BCP bridge protocol) */
void EP7_OUT_Callback(void)
{
    Bridge_EndpointOut();
}

void EP3_OUT_Callback(void) {}
void EP4_OUT_Callback(void) {}
void EP5_IN_Callback(void)  {}
void EP6_OUT_Callback(void) {}
void EP7_IN_Callback(void)  {}
