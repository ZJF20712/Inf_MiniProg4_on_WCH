/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_desc.c
 * Description        : USB descriptors for WCH-MiniProg4.
 *
 * Two build modes (DAP_FW_V1 selects HID, default is bulk):
 *   Bulk: PID 0xF151, CMSIS-DAP v2 (vendor bulk, WinUSB)
 *   HID:  PID 0xF152, CMSIS-DAP v1 (HID interrupt endpoints)
*******************************************************************************/
#include "usb_lib.h"
#include "usb_desc.h"

/* Device descriptor: IAD composite, bcdDevice 1.01, iSerial from die UID */
const uint8_t USBD_DeviceDescriptor[] = {
    USBD_SIZE_DEVICE_DESC,              // bLength
    DEVICE_DESCRIPTOR,                  // bDescriptorType
    0x10, 0x02,                         // bcdUSB = 2.10 (BOS / MS OS 2.0)
    0xEF,                               // bDeviceClass: Miscellaneous
    0x02,                               // bDeviceSubClass: Common
    0x01,                               // bDeviceProtocol: IAD
    USB_MAX_EP0_SZ,                     // bMaxPacketSize0 = 8
    USBD_VID & 0xFF, USBD_VID >> 8,     // idVendor  = 0x04B4
    USBD_PID & 0xFF, USBD_PID >> 8,     // idProduct = 0xF151 or 0xF152
    0x01, 0x01,                         // bcdDevice = 1.01
    0x01,                               // iManufacturer
    0x02,                               // iProduct
    0x03,                               // iSerialNumber
    0x01                                // bNumConfigurations
};

#ifdef DAP_FW_V1
/* ======================= HID mode (CMSIS-DAP v1) ======================= */

/* HID report descriptor: 33 bytes, standard CMSIS-DAP v1 format
 * (64-byte input/output reports, no report ID) */
const uint8_t USBD_HidReportDesc[] = {
    0x06, 0x00, 0xFF,       // Usage Page = 0xFF00 (Vendor Defined)
    0x09, 0x01,             // Usage (Vendor Usage 1)
    0xA1, 0x01,             // Collection (Application)
    0x15, 0x00,             //   Logical Minimum (0)
    0x26, 0xFF, 0x00,       //   Logical Maximum (255)
    0x75, 0x08,             //   Report Size (8 bits)
    0x95, 0x40,             //   Report Count (64)
    0x09, 0x01,             //   Usage (Vendor Usage 1)
    0x81, 0x02,             //   Input (Data, Variable, Absolute)
    0x95, 0x40,             //   Report Count (64)
    0x09, 0x01,             //   Usage (Vendor Usage 1)
    0x91, 0x02,             //   Output (Data, Variable, Absolute)
    0x95, 0x01,             //   Report Count (1)
    0x09, 0x01,             //   Usage (Vendor Usage 1)
    0xB1, 0x02,             //   Feature (Data, Variable, Absolute)
    0xC0                    // End Collection
};

/* Configuration descriptor, 130 bytes, 4 interfaces
 * IF0: CMSIS-DAP v1 (HID, class 0x03)       EP2 OUT + EP1 IN (interrupt)
 * IF1: Bridge (vendor bulk, WinUSB)          EP6 IN / EP7 OUT
 * IF2: CDC control (IAD grouped with IF3)    EP3 IN int
 * IF3: CDC data                              EP4 IN / EP5 OUT
 */
const uint8_t USBD_ConfigDescriptor[] = {
    USBD_SIZE_CONFIG_DESC,              // bLength
    CONFIG_DESCRIPTOR,                  // bDescriptorType
    USBD_SIZE_CONFIG_TOTAL & 0xFF,
    USBD_SIZE_CONFIG_TOTAL >> 8,        // wTotalLength = 130
    4,                                  // bNumInterfaces
    0x01,                               // bConfigurationValue
    0x00,                               // iConfiguration
    0x80,                               // bmAttributes: bus powered
    0xC8,                               // bMaxPower: 400 mA

    /* ---- IF0 : CMSIS-DAP v1 HID ---- */
    USBD_SIZE_INTERFACE_DESC,           // bLength
    INTERFACE_DESCRIPTOR,               // bDescriptorType
    0x00,                               // bInterfaceNumber
    0x00,                               // bAlternateSetting
    0x02,                               // bNumEndpoints
    0x03,                               // bInterfaceClass: HID
    0x00,                               // bInterfaceSubClass: no boot
    0x00,                               // bInterfaceProtocol: none
    0x05,                               // iInterface: Cypress MiniProg4 (CMSIS-DAP)

    /* HID class descriptor */
    0x09,                               // bLength
    HID_CLASS_DESC_HID,                 // bDescriptorType: HID
    0x11, 0x01,                         // bcdHID = 1.11
    0x00,                               // bCountryCode: not supported
    0x01,                               // bNumDescriptors: 1
    HID_CLASS_DESC_REPORT,              // bDescriptorType: Report
    USBD_SIZE_REPORT_DESC, 0x00,        // wDescriptorLength: 33

    /* EP1 IN: probe -> host responses (interrupt) - FIRST (matching KitProg3) */
    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    DAP_HID_IN_EP,                      // 0x81
    USB_EPT_DESC_INTERRUPT,
    DAP_HID_IN_SZ, 0x00,
    DAP_HID_INTERVAL,                   // bInterval: 1 ms

    /* EP2 OUT: host -> probe commands (interrupt) - SECOND */
    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    DAP_HID_OUT_EP,                     // 0x02
    USB_EPT_DESC_INTERRUPT,
    DAP_HID_OUT_SZ, 0x00,
    DAP_HID_INTERVAL,                   // bInterval: 1 ms

    /* ---- IF1 : Bridge bulk ---- */
    USBD_SIZE_INTERFACE_DESC,
    INTERFACE_DESCRIPTOR,
    0x01,                               // bInterfaceNumber
    0x00,
    0x02,
    0xFF,                               // vendor specific
    0x00,
    0x00,
    0x04,                               // iInterface: MiniProg4 bridge

    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    BRIDGE_BULK_IN_EP,                  // EP6 IN
    USB_EPT_DESC_BULK,
    BRIDGE_PACKET_SZ, 0x00,
    0x00,

    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    BRIDGE_BULK_OUT_EP,                 // EP7 OUT
    USB_EPT_DESC_BULK,
    BRIDGE_PACKET_SZ, 0x00,
    0x00,

    /* ---- CDC (IAD: IF2 + IF3) ---- */
    0x08,                               // bLength
    0x0B,                               // bDescriptorType: IAD
    0x02,                               // bFirstInterface
    0x02,                               // bInterfaceCount
    0x02,                               // bFunctionClass: CDC
    0x02,                               // bFunctionSubClass
    0x01,                               // bFunctionProtocol
    0x00,                               // iFunction

    /* IF2 : CDC control */
    USBD_SIZE_INTERFACE_DESC,
    INTERFACE_DESCRIPTOR,
    0x02,                               // bInterfaceNumber
    0x00,
    0x01,
    0x02,                               // CDC control class
    0x02,                               // ACM
    0x01,                               // AT commands
    0x06,                               // iInterface: MiniProg4 USBUART

    0x05, 0x24, 0x00, 0x10, 0x01,       // header, CDC 1.10
    0x05, 0x24, 0x01, 0x00, 0x03,       // call management (data IF 3)
    0x04, 0x24, 0x02, 0x00,             // ACM capabilities
    0x05, 0x24, 0x06, 0x02, 0x03,       // union (master IF2, slave IF3)

    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    CDC_INT_IN_EP,                      // EP3 IN interrupt
    USB_EPT_DESC_INTERRUPT,
    CDC_INT_IN_SZ, 0x00,
    2,                                  // bInterval

    /* IF3 : CDC data */
    USBD_SIZE_INTERFACE_DESC,
    INTERFACE_DESCRIPTOR,
    0x03,                               // bInterfaceNumber
    0x00,
    0x02,
    0x0A,                               // CDC data class
    0x00,
    0x00,
    0x07,                               // iInterface: CDC Data Interface

    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    CDC_BULK_IN_EP,                     // EP4 IN
    USB_EPT_DESC_BULK,
    CDC_BULK_SZ, 0x00,
    0x00,

    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    CDC_BULK_OUT_EP,                    // EP5 OUT
    USB_EPT_DESC_BULK,
    CDC_BULK_SZ, 0x00,
    0x00,
};

#else
/* ======================= Bulk mode (CMSIS-DAP v2, default) ======================= */

/* Configuration descriptor, 121 bytes, 4 interfaces
 * IF0: CMSIS-DAP v2 (vendor bulk, WinUSB)   EP1 OUT / EP2 IN
 * IF1: Bridge (vendor bulk, WinUSB)         EP6 IN / EP7 OUT
 * IF2: CDC control (IAD grouped with IF3)   EP3 IN int
 * IF3: CDC data                             EP4 IN / EP5 OUT
 */
const uint8_t USBD_ConfigDescriptor[] = {
    USBD_SIZE_CONFIG_DESC,              // bLength
    CONFIG_DESCRIPTOR,                  // bDescriptorType
    USBD_SIZE_CONFIG_TOTAL & 0xFF,
    USBD_SIZE_CONFIG_TOTAL >> 8,        // wTotalLength = 121
    4,                                  // bNumInterfaces
    0x01,                               // bConfigurationValue
    0x00,                               // iConfiguration
    0x80,                               // bmAttributes: bus powered
    0xC8,                               // bMaxPower: 400 mA

    /* ---- IF0 : CMSIS-DAP v2 bulk ---- */
    USBD_SIZE_INTERFACE_DESC,           // bLength
    INTERFACE_DESCRIPTOR,               // bDescriptorType
    0x00,                               // bInterfaceNumber
    0x00,                               // bAlternateSetting
    0x02,                               // bNumEndpoints
    0xFF,                               // bInterfaceClass: vendor specific
    0x00,                               // bInterfaceSubClass
    0x00,                               // bInterfaceProtocol
    0x05,                               // iInterface: Cypress MiniProg4 (CMSIS-DAP)

    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    DAP_BULK_OUT_EP,                    // EP1 OUT: commands
    USB_EPT_DESC_BULK,
    DAP_PACKET_SZ, 0x00,
    0x00,

    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    DAP_BULK_IN_EP,                     // EP2 IN: responses
    USB_EPT_DESC_BULK,
    DAP_PACKET_SZ, 0x00,
    0x00,

    /* ---- IF1 : Bridge bulk ---- */
    USBD_SIZE_INTERFACE_DESC,
    INTERFACE_DESCRIPTOR,
    0x01,                               // bInterfaceNumber
    0x00,
    0x02,
    0xFF,                               // vendor specific
    0x00,
    0x00,
    0x04,                               // iInterface: MiniProg4 bridge

    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    BRIDGE_BULK_IN_EP,                  // EP6 IN
    USB_EPT_DESC_BULK,
    BRIDGE_PACKET_SZ, 0x00,
    0x00,

    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    BRIDGE_BULK_OUT_EP,                 // EP7 OUT
    USB_EPT_DESC_BULK,
    BRIDGE_PACKET_SZ, 0x00,
    0x00,

    /* ---- CDC (IAD: IF2 + IF3) ---- */
    0x08,                               // bLength
    0x0B,                               // bDescriptorType: IAD
    0x02,                               // bFirstInterface
    0x02,                               // bInterfaceCount
    0x02,                               // bFunctionClass: CDC
    0x02,                               // bFunctionSubClass
    0x01,                               // bFunctionProtocol
    0x00,                               // iFunction

    /* IF2 : CDC control */
    USBD_SIZE_INTERFACE_DESC,
    INTERFACE_DESCRIPTOR,
    0x02,                               // bInterfaceNumber
    0x00,
    0x01,
    0x02,                               // CDC control class
    0x02,                               // ACM
    0x01,                               // AT commands
    0x06,                               // iInterface: MiniProg4 USBUART

    0x05, 0x24, 0x00, 0x10, 0x01,       // header, CDC 1.10
    0x05, 0x24, 0x01, 0x00, 0x03,       // call management (data IF 3)
    0x04, 0x24, 0x02, 0x00,             // ACM capabilities
    0x05, 0x24, 0x06, 0x02, 0x03,       // union (master IF2, slave IF3)

    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    CDC_INT_IN_EP,                      // EP3 IN interrupt
    USB_EPT_DESC_INTERRUPT,
    CDC_INT_IN_SZ, 0x00,
    2,                                  // bInterval

    /* IF3 : CDC data */
    USBD_SIZE_INTERFACE_DESC,
    INTERFACE_DESCRIPTOR,
    0x03,                               // bInterfaceNumber
    0x00,
    0x02,
    0x0A,                               // CDC data class
    0x00,
    0x00,
    0x07,                               // iInterface: CDC Data Interface

    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    CDC_BULK_IN_EP,                     // EP4 IN
    USB_EPT_DESC_BULK,
    CDC_BULK_SZ, 0x00,
    0x00,

    USBD_SIZE_ENDPOINT_DESC,
    ENDPOINT_DESCRIPTOR,
    CDC_BULK_OUT_EP,                    // EP5 OUT
    USB_EPT_DESC_BULK,
    CDC_BULK_SZ, 0x00,
    0x00,
};
#endif /* DAP_FW_V1 */

/* ---------------- String descriptors (shared between modes) ---------------- */

const uint8_t USBD_StringLangID[] = {
    USBD_SIZE_STRING_LANGID,
    STRING_DESCRIPTOR,
    0x09, 0x04
};

const uint8_t USBD_StringVendor[] = {
    USBD_SIZE_STRING_VENDOR,
    STRING_DESCRIPTOR,
    'C', 0, 'y', 0, 'p', 0, 'r', 0, 'e', 0, 's', 0, 's', 0, ' ', 0,
    'S', 0, 'e', 0, 'm', 0, 'i', 0, 'c', 0, 'o', 0, 'n', 0, 'd', 0,
    'u', 0, 'c', 0, 't', 0, 'o', 0, 'r', 0
};

const uint8_t USBD_StringProduct[] = {
    USBD_SIZE_STRING_PRODUCT,
    STRING_DESCRIPTOR,
    'C', 0, 'y', 0, 'p', 0, 'r', 0, 'e', 0, 's', 0, 's', 0, ' ', 0,
    'M', 0, 'i', 0, 'n', 0, 'i', 0, 'P', 0, 'r', 0, 'o', 0, 'g', 0,
    '4', 0, ' ', 0, '(', 0, 'C', 0, 'M', 0, 'S', 0, 'I', 0, 'S', 0,
    '-', 0, 'D', 0, 'A', 0, 'P', 0, ')', 0
};

/* filled at startup from chip 64-bit die UID (16 hex chars) */
uint8_t USBD_StringSerial[] = {
    USBD_SIZE_STRING_SERIAL,
    STRING_DESCRIPTOR,
    '0', 0, '1', 0, '2', 0, '3', 0, '4', 0, '5', 0, '6', 0, '7', 0,
    '8', 0, '9', 0, 'A', 0, 'B', 0
};

/* interface strings; indices referenced from configuration descriptor */
const uint8_t USBD_StringDAP[] = {          /* index 5 */
    USBD_SIZE_STRING_DAP,
    STRING_DESCRIPTOR,
    'C', 0, 'y', 0, 'p', 0, 'r', 0, 'e', 0, 's', 0, 's', 0, ' ', 0,
    'M', 0, 'i', 0, 'n', 0, 'i', 0, 'P', 0, 'r', 0, 'o', 0, 'g', 0,
    '4', 0, ' ', 0, '(', 0, 'C', 0, 'M', 0, 'S', 0, 'I', 0, 'S', 0,
    '-', 0, 'D', 0, 'A', 0, 'P', 0, ')', 0
};

const uint8_t USBD_StringBridge[] = {       /* index 4 */
    USBD_SIZE_STRING_BRIDGE,
    STRING_DESCRIPTOR,
    'M', 0, 'i', 0, 'n', 0, 'i', 0, 'P', 0, 'r', 0, 'o', 0, 'g', 0,
    '4', 0, ' ', 0, 'b', 0, 'r', 0, 'i', 0, 'd', 0, 'g', 0, 'e', 0
};

const uint8_t USBD_StringUART[] = {         /* index 6 */
    USBD_SIZE_STRING_UART,
    STRING_DESCRIPTOR,
    'M', 0, 'i', 0, 'n', 0, 'i', 0, 'P', 0, 'r', 0, 'o', 0, 'g', 0,
    '4', 0, ' ', 0, 'U', 0, 'S', 0, 'B', 0, 'U', 0, 'A', 0, 'R', 0,
    'T', 0
};

const uint8_t USBD_StringCDCData[] = {      /* index 7 */
    USBD_SIZE_STRING_CDCDATA,
    STRING_DESCRIPTOR,
    'C', 0, 'D', 0, 'C', 0, ' ', 0, 'D', 0, 'a', 0, 't', 0, 'a', 0,
    ' ', 0, 'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0,
    'c', 0, 'e', 0
};

#ifdef DAP_FW_V1
/* ---- HID descriptor accessors ---- */
uint8_t *USBD_GetReportDescriptor(uint16_t Length)
{
    static ONE_DESCRIPTOR report_descriptor =
    {
        (uint8_t*)USBD_HidReportDesc,
        sizeof(USBD_HidReportDesc)
    };
    return Standard_GetDescriptorData(Length, &report_descriptor);
}

uint8_t *USBD_GetHidDescriptor(uint16_t Length)
{
    /* The HID class descriptor is at offset 9+9=18 in the config descriptor */
    static ONE_DESCRIPTOR hid_descriptor =
    {
        (uint8_t*)&USBD_ConfigDescriptor[18],
        9
    };
    return Standard_GetDescriptorData(Length, &hid_descriptor);
}
#endif

/* ---------------- BOS + MS OS 2.0 (WinUSB for bridge IF) ---------------- */

uint8_t BOS_Descriptor[] =
{
    5,
    DESC_BOS,
    5+20+8, 0,                  // wTotalLength = 33
    1,                          // bNumDeviceCaps

    /*** MS OS 2.0 descriptor platform capability descriptor ***/
    28,
    DESC_CAPABILITY,
    5,                          // bDevCapabilityType: PLATFORM
    0x00,
    0xDF, 0x60, 0xDD, 0xD8,     // MS_OS_20_Platform_Capability_ID
    0x89, 0x45, 0xC7, 0x4C,
    0x9C, 0xD2, 0x65, 0x9D,
    0x9E, 0x64, 0x8A, 0x9F,

    0x00, 0x00, 0x03, 0x06,     // dwWindowsVersion: Windows 8.1+
#ifdef DAP_FW_V1
    0xAE, 0x00,                 // wTotalLength = 174: bridge only (HID uses hidusb)
#else
    0x4A, 0x01,                 // wTotalLength = 330: DAP + bridge
#endif
    WINUSB_VENDOR_CODE,         // bMS_VendorCode
    0x00                        // bAltEnumCmd
};

#define MS_OS_20_SET_HEADER_DESCRIPTOR        0x00
#define MS_OS_20_SUBSET_HEADER_CONFIGURATION  0x01
#define MS_OS_20_SUBSET_HEADER_FUNCTION       0x02
#define MS_OS_20_FEATURE_COMPATIBLE_ID        0x03
#define MS_OS_20_FEATURE_REG_PROPERTY         0x04

/* Descriptor set: WinUSB + DeviceInterfaceGUID */
uint8_t MS_OS_20_DescriptorSet[] =
{
    /*** header ***/
    10, 0,
    MS_OS_20_SET_HEADER_DESCRIPTOR, 0,
    0x00, 0x00, 0x03, 0x06,
#ifdef DAP_FW_V1
    0xAE, 0x00,                 // 174: bridge only (HID uses hidusb driver)
#else
    0x4A, 0x01,                 // 330: DAP + bridge
#endif

    /*** configuration subset ***/
    8, 0,
    MS_OS_20_SUBSET_HEADER_CONFIGURATION, 0,
    0,                          // bConfigurationValue
    0,
#ifdef DAP_FW_V1
    0xA4, 0x00,                 // 164: bridge only (8 + 156)
#else
    0x40, 0x01,                 // 320: DAP + bridge (8+156+156)
#endif

#ifndef DAP_FW_V1
    /*** function subset: IF0 CMSIS-DAP v2 (bulk only) ***/
    8, 0,
    MS_OS_20_SUBSET_HEADER_FUNCTION, 0,
    0,                          // bFirstInterface
    0,
    8+20+128, 0,                // 156

    20, 0,
    MS_OS_20_FEATURE_COMPATIBLE_ID, 0,
    'W',  'I',  'N',  'U',  'S',  'B',  0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    128, 0,
    MS_OS_20_FEATURE_REG_PROPERTY, 0,
    1, 0,                       // REG_SZ
    40, 0x00,
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0,
    't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0, 'G', 0,
    'U', 0, 'I', 0, 'D', 0,   0, 0,
    78, 0x00,
    '{', 0, '8', 0, '8', 0, 'B', 0, 'A', 0, 'E', 0, '3', 0, '2', 0,   /* {88BAE032-5A81-49f0-BC3D-A4FF138216D6} */
    '2', 0, '-', 0, '5', 0, 'A', 0, '8', 0, '1', 0, '-', 0, '4', 0,
    '9', 0, 'f', 0, '0', 0, '-', 0, 'B', 0, 'C', 0, '3', 0, 'D', 0,
    '-', 0, 'A', 0, '4', 0, 'F', 0, 'F', 0, '1', 0, '3', 0, '8', 0,
    '2', 0, '1', 0, '6', 0, 'D', 0, '6', 0, '}', 0,   0, 0,
#endif /* !DAP_FW_V1 */

    /*** function subset: bridge (always present) ***/
    8, 0,
    MS_OS_20_SUBSET_HEADER_FUNCTION, 0,
#ifdef DAP_FW_V1
    1,                          // bFirstInterface: bridge is IF1
#else
    1,                          // bFirstInterface: bridge is IF1
#endif
    0,
    8+20+128, 0,                // 156

    20, 0,
    MS_OS_20_FEATURE_COMPATIBLE_ID, 0,
    'W',  'I',  'N',  'U',  'S',  'B',  0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    128, 0,
    MS_OS_20_FEATURE_REG_PROPERTY, 0,
    1, 0,
    40, 0x00,
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0,
    't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0, 'G', 0,
    'U', 0, 'I', 0, 'D', 0,   0, 0,
    78, 0x00,
    '{', 0, 'C', 0, 'D', 0, 'B', 0, '3', 0, 'B', 0, '5', 0, 'A', 0,   /* {CDB3B5AD-293B-4663-AA36-1AAE46463776} */
    'D', 0, '-', 0, '2', 0, '9', 0, '3', 0, 'B', 0, '-', 0, '4', 0,
    '6', 0, '6', 0, '3', 0, '-', 0, 'A', 0, 'A', 0, '3', 0, '6', 0,
    '-', 0, '1', 0, 'A', 0, 'A', 0, 'E', 0, '4', 0, '6', 0, '4', 0,
    '6', 0, '3', 0, '7', 0, '7', 0, '6', 0, '}', 0,   0, 0
};

uint8_t *USBD_GetBOSDescriptor(uint16_t Length)
{
    static ONE_DESCRIPTOR bos_descriptor =
    {
        (uint8_t*)BOS_Descriptor,
        sizeof(BOS_Descriptor)
    };

    return Standard_GetDescriptorData(Length, &bos_descriptor);
}

uint8_t *USBD_MS_OS_20_DescriptorSet(uint16_t Length)
{
    static ONE_DESCRIPTOR ms_os_20_descriptorSet =
    {
        (uint8_t*)MS_OS_20_DescriptorSet,
        sizeof(MS_OS_20_DescriptorSet)
    };

    return Standard_GetDescriptorData(Length, &ms_os_20_descriptorSet);
}
