/********************************** (C) COPYRIGHT *******************************
 * File Name          : dap_usb.h
 * Description        : CMSIS-DAP v2 bulk transport over EP1 IN / EP2 OUT.
*******************************************************************************/
#ifndef __DAP_USB_H
#define __DAP_USB_H

#include <stdint.h>

/* 4-deep queue to fit CH32V203G6 10KB RAM (see dap_usb.c) */
#define DAP_PACKET_QUEUE 4

void DAP_EndpointOut(void);     /* called from EP2 OUT CTR callback */
void DAP_EndpointInDone(void);  /* called from EP1 IN CTR callback  */
void DAP_Process(void);         /* call from main loop */

#endif /* __DAP_USB_H */
