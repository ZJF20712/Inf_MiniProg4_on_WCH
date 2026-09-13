/********************************** (C) COPYRIGHT *******************************
 * File Name          : vcom_serial.h
 * Description        : CDC data <==> USART bridge for the USB-UART function.
 *                      KitProg3-compatible: 8N1, optional HW flow control
 *                      configured through KHPI 0x93.
*******************************************************************************/
#ifndef __VCOM_SERIAL_H
#define __VCOM_SERIAL_H

#include "ch32v20x.h"

typedef volatile struct __attribute__((packed)) {
    uint8_t  in_bytes;          /* bytes in EP IN packet */
    uint8_t  out_bytes;         /* bytes received from host */
    uint8_t  out_buff[64];      /* host -> uart buffer */
    uint8_t  ep_in_ready;       /* EP4 IN can accept a packet */
    uint8_t  flowControl;       /* KHPI 0x93: 0 = none, 2 = RTS/CTS */
    uint8_t  uartMode;          /* KHPI 0x93: 0 = full duplex */
} VCOM;

extern volatile VCOM Vcom;

typedef struct __attribute__((packed)) {
    uint32_t u32DTERate;
    uint8_t  u8CharFormat;
    uint8_t  u8ParityType;
    uint8_t  u8DataBits;
} VCOM_LINE_CODING;

extern VCOM_LINE_CODING LineCfg;

void VCOM_Init(void);
void VCOM_LineCoding(VCOM_LINE_CODING *LineCfgx);
void VCOM_TransferData(void);

#endif
