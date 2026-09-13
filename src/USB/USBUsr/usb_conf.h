/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_conf.h
 * Description        : USB endpoint / buffer configuration for WCH-MiniProg4.
 *
 * Bulk mode (default):
 *   EP1 OUT  CMSIS-DAP v2 bulk (commands)
 *   EP2 IN   CMSIS-DAP v2 bulk (responses)
 *
 * HID mode (DAP_FW_V1):
 *   EP1 IN   CMSIS-DAP v1 HID  (responses, interrupt)
 *   EP2 OUT  CMSIS-DAP v1 HID  (commands, interrupt)
 *
 * Shared in both modes:
 *   EP0       control (8 byte)
 *   EP3 IN    CDC notification (interrupt)
 *   EP4 IN    CDC data bulk
 *   EP5 OUT   CDC data bulk
 *   EP6 IN    Bridge bulk (responses)
 *   EP7 OUT   Bridge bulk (commands)
*******************************************************************************/
#ifndef __USB_CONF_H
#define __USB_CONF_H

#define EP_NUM              (8)

/* Buffer Description Table: 8 endpoints * 8 bytes = 0x40 */
#define BTABLE_ADDRESS      (0x00)

/* EP0: control, 8 bytes each direction (KitProg3-compatible) */
#define ENDP0_RXADDR        (0x40)
#define ENDP0_TXADDR        (0x48)

/* DAP: EP1 and EP2 share PMA buffers (each endpoint uses only one direction) */
#define ENDP1_RXADDR        (0x50)              /* EP1 OUT: 64B (bulk commands) */
#define ENDP1_TXADDR        (0x50)              /* EP1 IN : 64B (HID responses) - shared with RX */
#define ENDP2_RXADDR        (0x90)              /* EP2 OUT: 64B (HID commands) */
#define ENDP2_TXADDR        (0x90)              /* EP2 IN : 64B (bulk responses) - shared with RX */

/* CDC */
#define ENDP3_TXADDR        (0xD0)              /* EP3 IN  : 8B  (notification) */
#define ENDP4_TXADDR        (0xD8)              /* EP4 IN  : 64B */
#define ENDP5_RXADDR        (0x118)             /* EP5 OUT : 64B */

/* Bridge */
#define ENDP6_TXADDR        (0x158)             /* EP6 IN  : 64B */
#define ENDP7_RXADDR        (0x198)             /* EP7 OUT : 64B */
/* total 0x1D8 <= 0x200 PMA */

/* NOTE: SOF/ESOF kept enabled; suspend power-down is intentionally ignored
 * in usb_istr.c - a debug probe must never freeze on host selective-suspend. */
#define IMR_MSK (CNTR_CTRM | CNTR_WKUPM | CNTR_SUSPM | CNTR_ERRM | CNTR_SOFM | CNTR_ESOFM | CNTR_RESETM)

/* CTR callbacks used by usb_istr.c (everything else is polled in main loop) */
#define EP3_IN_Callback     NOP_Process
#define EP6_IN_Callback     NOP_Process

#define DESC_BOS            0x0F
#define DESC_CAPABILITY     0x10

/* bMS_VendorCode for MS OS 2.0 descriptor retrieval */
#define WINUSB_VENDOR_CODE  0x34

#include "ch32v20x.h"

void Delay_Ms(uint32_t n);
void USB_Port_Set(FunctionalState NewState, FunctionalState Pin_In_IPU);

#endif /* __USB_CONF_H */
