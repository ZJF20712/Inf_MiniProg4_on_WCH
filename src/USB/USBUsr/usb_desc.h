/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_desc.h
 * Description        : USB descriptor definitions for WCH-MiniProg4.
 *
 * Two build modes (select in Makefile with -DDAP_FW_V1):
 *   Bulk (default): PID 0xF151, CMSIS-DAP v2 (vendor bulk, WinUSB)
 *   HID  (DAP_FW_V1): PID 0xF152, CMSIS-DAP v1 (HID interrupt endpoints)
 *
 * Bridge and CDC-UART interfaces are identical in both modes.
*******************************************************************************/
#ifndef __USB_DESC_H
#define __USB_DESC_H

#include "ch32v20x.h"

/* ---- Both DAP transport modes compiled in; runtime selectable ---- */
#define USBD_VID    0x04B4

/* v1 HID mode (PID 0xF152) */
#define DAP_HID_IN_EP      EP1_IN   /* 0x81: probe -> host responses */
#define DAP_HID_OUT_EP     EP2_OUT  /* 0x02: host -> probe commands */
#define DAP_HID_IN_SZ      64
#define DAP_HID_OUT_SZ     64
#define DAP_HID_INTERVAL   1        /* 1 ms polling */

/* HID report descriptor length (33 bytes, standard CMSIS-DAP v1) */
#define USBD_SIZE_REPORT_DESC       33

/* v2 bulk mode (PID 0xF151) */
#define DAP_BULK_OUT_EP     EP1_OUT /* 0x01: host -> probe commands */
#define DAP_BULK_IN_EP      EP2_IN  /* 0x82: probe -> host responses */

/* Shared endpoints (identical in both modes) */
#define CDC_INT_IN_EP       EP3_IN  /* 0x83 */
#define CDC_BULK_IN_EP      EP4_IN  /* 0x84 */
#define CDC_BULK_OUT_EP     EP5_OUT /* 0x05 */
#define BRIDGE_BULK_IN_EP   EP6_IN  /* 0x86 */
#define BRIDGE_BULK_OUT_EP  EP7_OUT /* 0x07 */

/* Endpoint indexes (peripheral registers) */
#ifdef DAP_FW_V1
#define DAP_IN_ENDP         ENDP1   /* EP1 IN for HID responses */
#define DAP_OUT_ENDP        ENDP2   /* EP2 OUT for HID commands */
#else
#define DAP_IN_ENDP         ENDP2   /* EP2 IN for bulk responses */
#define DAP_OUT_ENDP        ENDP1   /* EP1 OUT for bulk commands */
#endif
#define CDC_NOTIF_ENDP      ENDP3
#define CDC_IN_ENDP         ENDP4
#define CDC_OUT_ENDP        ENDP5
#define BRIDGE_IN_ENDP      ENDP6
#define BRIDGE_OUT_ENDP     ENDP7

/* Endpoint maximum packet sizes */
#define USB_MAX_EP0_SZ      8
#define DAP_PACKET_SZ       64
#define CDC_INT_IN_SZ       8
#define CDC_BULK_SZ         64
#define BRIDGE_PACKET_SZ    64

/* Descriptor sizes */
#define USBD_SIZE_DEVICE_DESC       18
#define USBD_SIZE_CONFIG_DESC       9
#define USBD_SIZE_INTERFACE_DESC    9
#define USBD_SIZE_ENDPOINT_DESC     7
#define USBD_SIZE_STRING_LANGID     4

/* wTotalLength of configuration descriptor (both variants compiled in) */
/* HID: 9 + [9+9(HID)+7+7] DAP + [9+7+7] bridge + [8+9+5+5+4+5+7] CDC ctrl + [9+7+7] CDC data */
#define USBD_SIZE_CONFIG_TOTAL_V1   130
/* Bulk: 9 + [9+7+7] DAP + [9+7+7] bridge + [8+9+5+5+4+5+7] CDC ctrl + [9+7+7] CDC data */
#define USBD_SIZE_CONFIG_TOTAL_V2   121
/* legacy alias for the v1 default boot mode */
#define USBD_SIZE_CONFIG_TOTAL      USBD_SIZE_CONFIG_TOTAL_V1

/* String descriptor sizes (bytes, including 2-byte header) */
#define USBD_SIZE_STRING_VENDOR     (2 + 2*21)  /* "Cypress Semiconductor"     */
#define USBD_SIZE_STRING_PRODUCT    (2 + 2*29)  /* "Cypress MiniProg4 (CMSIS-DAP)" */
#define USBD_SIZE_STRING_SERIAL     (2 + 2*12)
#define USBD_SIZE_STRING_DAP        (2 + 2*29)  /* "Cypress MiniProg4 (CMSIS-DAP)" */
#define USBD_SIZE_STRING_BRIDGE     (2 + 2*16)  /* "MiniProg4 bridge"          */
#define USBD_SIZE_STRING_UART       (2 + 2*17)  /* "MiniProg4 USBUART"         */
#define USBD_SIZE_STRING_CDCDATA    (2 + 2*18)  /* "CDC Data Interface"        */

#define USB_EPT_DESC_CONTROL        0x00
#define USB_EPT_DESC_ISO            0x01
#define USB_EPT_DESC_BULK           0x02
#define USB_EPT_DESC_INTERRUPT      0x03

#define HID_CLASS_DESC_HID          0x21
#define HID_CLASS_DESC_REPORT       0x22

extern uint8_t USBD_DeviceDescriptor[];
extern const uint8_t USBD_ConfigDescriptor_V1[];
extern const uint8_t USBD_ConfigDescriptor_V2[];
extern const uint8_t USBD_StringLangID[];
extern const uint8_t USBD_StringVendor[];
extern const uint8_t USBD_StringProduct[];
extern uint8_t       USBD_StringSerial[];
extern const uint8_t USBD_StringDAP[];
extern const uint8_t USBD_StringBridge[];
extern const uint8_t USBD_StringUART[];
extern const uint8_t USBD_StringCDCData[];

/* DAP transport mode: 0 = v1 HID (F152), 1 = v2 bulk (F151).
 * Set at boot from the persisted SRAM flag; 0x82 mode switch re-enumerates. */
extern uint8_t g_dapV2Mode;

extern const uint8_t USBD_HidReportDesc[];
uint8_t *USBD_GetReportDescriptor(uint16_t Length);
uint8_t *USBD_GetHidDescriptor(uint16_t Length);

extern uint8_t BOS_Descriptor[];
uint8_t *USBD_GetBOSDescriptor(uint16_t Length);
uint8_t *USBD_MS_OS_20_DescriptorSet(uint16_t Length);

#endif /* __USB_DESC_H */
