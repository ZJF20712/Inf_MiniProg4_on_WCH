/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_prop.c
 * Description        : USB setup / request processing for WCH-MiniProg4.
*******************************************************************************/
#include <stdlib.h>
#include "usb_lib.h"
#include "usb_conf.h"
#include "usb_prop.h"
#include "usb_desc.h"
#include "usb_pwr.h"

#include "vcom_serial.h"

uint8_t Request = 0;

/* HID SET_REPORT / GET_REPORT support for CMSIS-DAP v1 (runtime mode) */
#define HID_SET_REPORT  0x09
#define HID_GET_REPORT  0x01
static uint8_t hid_report_buf[64];
static volatile uint8_t hid_report_len = 0;
static volatile uint8_t hid_report_valid = 0;

#include "dap_usb.h"
#include "DAP.h"

/* Host writes a DAP command via SET_REPORT (control transfer) */
uint8_t *USB_HID_SetReport(uint16_t Length)
{
    if (Length == 0)
    {
        pInformation->Ctrl_Info.Usb_wLength = 64;
        return NULL;
    }
    return hid_report_buf;
}

/* Host reads a DAP response via GET_REPORT (control transfer) */
uint8_t *USB_HID_GetReport(uint16_t Length)
{
    if (Length == 0)
    {
        pInformation->Ctrl_Info.Usb_wLength = 64;
        return NULL;
    }
    return hid_report_buf;
}

DEVICE Device_Table =
{
    EP_NUM,     /* bNumEndpoints (0..7 used) */
    1           /* bNumConfigurations */
};

DEVICE_PROP Device_Property =
{
    USBD_init,
    USBD_Reset,
    USBD_Status_In,
    USBD_Status_Out,
    USBD_Data_Setup,
    USBD_NoData_Setup,
    USBD_Get_Interface_Setting,
    USBD_GetDeviceDescriptor,
    USBD_GetConfigDescriptor,
    USBD_GetStringDescriptor,
    0,
    USB_MAX_EP0_SZ
};

USER_STANDARD_REQUESTS User_Standard_Requests =
{
    USBD_GetConfiguration,
    USBD_SetConfiguration,
    USBD_GetInterface,
    USBD_SetInterface,
    USBD_GetStatus,
    USBD_ClearFeature,
    USBD_SetEndPointFeature,
    USBD_SetDeviceFeature,
    USBD_SetDeviceAddress
};

ONE_DESCRIPTOR Device_Descriptor =
{
    (uint8_t*)USBD_DeviceDescriptor,
    USBD_SIZE_DEVICE_DESC
};

ONE_DESCRIPTOR Config_Descriptor =
{
    (uint8_t*)USBD_ConfigDescriptor_V1,   /* boot default; USB_Config()
                                             re-points it per the mode flag */
    USBD_SIZE_CONFIG_TOTAL
};

ONE_DESCRIPTOR String_Descriptor[8] =
{
    {(uint8_t*)USBD_StringLangID,   USBD_SIZE_STRING_LANGID},
    {(uint8_t*)USBD_StringVendor,   USBD_SIZE_STRING_VENDOR},
    {(uint8_t*)USBD_StringProduct,  USBD_SIZE_STRING_PRODUCT},
    {(uint8_t*)USBD_StringSerial,   USBD_SIZE_STRING_SERIAL},
    {(uint8_t*)USBD_StringBridge,   USBD_SIZE_STRING_BRIDGE},
    {(uint8_t*)USBD_StringDAP,      USBD_SIZE_STRING_DAP},
    {(uint8_t*)USBD_StringUART,     USBD_SIZE_STRING_UART},
    {(uint8_t*)USBD_StringCDCData,  USBD_SIZE_STRING_CDCDATA},
};

void USBD_SetConfiguration(void)
{
    DEVICE_INFO *pInfo = &Device_Info;

    if (pInfo->Current_Configuration != 0)
    {
        bDeviceState = CONFIGURED;
    }
}

void USBD_SetDeviceAddress (void)
{
    bDeviceState = ADDRESSED;
}

void USBD_SetDeviceFeature (void)
{
}

void USBD_ClearFeature(void)
{
}

void USBD_Status_In(void)
{
    uint32_t Request_No = pInformation->USBbRequest;
    if (Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
    {
        if (Request_No == CDC_SET_LINE_CODING)
        {
            VCOM_LineCoding(&LineCfg);
        }
    }
}

void USBD_Status_Out(void)
{
}

void USBD_init(void)
{
    uint8_t i;

    pInformation->Current_Configuration = 0;
    PowerOn();
    for (i=0;i<8;i++) _SetENDPOINT(i,_GetENDPOINT(i) & 0x7F7F & EPREG_MASK); /* all clear */
    _SetISTR((uint16_t)0x00FF);
    USB_SIL_Init();
    bDeviceState = UNCONNECTED;

    USB_Port_Set(DISABLE, DISABLE);
    Delay_Ms(20);
    USB_Port_Set(ENABLE, ENABLE);
}

void USBD_Reset(void)
{
    pInformation->Current_Configuration = 0;
    pInformation->Current_Feature = Config_Descriptor.Descriptor[7];
    pInformation->Current_Interface = 0;

    SetBTABLE(BTABLE_ADDRESS);

    /* Endpoint 0: control */
    SetEPType(ENDP0, EP_CONTROL);
    SetEPTxAddr(ENDP0, ENDP0_TXADDR);
    SetEPRxAddr(ENDP0, ENDP0_RXADDR);
    SetEPTxCount(ENDP0, Device_Property.MaxPacketSize);
    SetEPRxCount(ENDP0, Device_Property.MaxPacketSize);
    SetEPTxStatus(ENDP0, EP_TX_STALL);
    SetEPRxStatus(ENDP0, EP_RX_VALID);
    _ClearDTOG_RX(ENDP0);
    _ClearDTOG_TX(ENDP0);

    extern uint8_t g_dapV2Mode;
    if (!g_dapV2Mode)
    {
        /* Endpoint 1: CMSIS-DAP v1 HID IN (probe -> host responses, interrupt) */
        SetEPType(ENDP1, EP_INTERRUPT);
        SetEPTxAddr(ENDP1, ENDP1_TXADDR);
        SetEPTxCount(ENDP1, DAP_PACKET_SZ);
        SetEPTxStatus(ENDP1, EP_TX_NAK);
        SetEPRxStatus(ENDP1, EP_RX_STALL);
        _ClearDTOG_TX(ENDP1);

        /* Endpoint 2: CMSIS-DAP v1 HID OUT (host -> probe commands, interrupt) */
        SetEPType(ENDP2, EP_INTERRUPT);
        SetEPRxAddr(ENDP2, ENDP2_RXADDR);
        SetEPRxCount(ENDP2, DAP_PACKET_SZ);
        SetEPRxStatus(ENDP2, EP_RX_VALID);
        SetEPTxStatus(ENDP2, EP_TX_STALL);
        _ClearDTOG_RX(ENDP2);
    }
    else
    {
        /* Endpoint 1: CMSIS-DAP v2 bulk OUT (host -> probe commands) */
        SetEPType(ENDP1, EP_BULK);
        SetEPRxAddr(ENDP1, ENDP1_RXADDR);
        SetEPRxCount(ENDP1, DAP_PACKET_SZ);
        SetEPRxStatus(ENDP1, EP_RX_VALID);
        SetEPTxStatus(ENDP1, EP_TX_DIS);
        _ClearDTOG_RX(ENDP1);

        /* Endpoint 2: CMSIS-DAP v2 bulk IN (probe -> host responses) */
        SetEPType(ENDP2, EP_BULK);
        SetEPTxAddr(ENDP2, ENDP2_TXADDR);
        SetEPTxCount(ENDP2, DAP_PACKET_SZ);
        SetEPTxStatus(ENDP2, EP_TX_NAK);
        SetEPRxStatus(ENDP2, EP_RX_DIS);
        _ClearDTOG_TX(ENDP2);
    }

    /* Endpoint 3: CDC notification (interrupt IN) */
    SetEPType(ENDP3, EP_INTERRUPT);
    SetEPTxAddr(ENDP3, ENDP3_TXADDR);
    SetEPTxCount(ENDP3, CDC_INT_IN_SZ);
    SetEPTxStatus(ENDP3, EP_TX_NAK);
    SetEPRxStatus(ENDP3, EP_RX_DIS);
    _ClearDTOG_TX(ENDP3);

    /* Endpoint 4: CDC data IN */
    SetEPType(ENDP4, EP_BULK);
    SetEPTxAddr(ENDP4, ENDP4_TXADDR);
    SetEPTxCount(ENDP4, CDC_BULK_SZ);
    SetEPTxStatus(ENDP4, EP_TX_NAK);
    SetEPRxStatus(ENDP4, EP_RX_DIS);
    _ClearDTOG_TX(ENDP4);

    /* Endpoint 5: CDC data OUT */
    SetEPType(ENDP5, EP_BULK);
    SetEPRxAddr(ENDP5, ENDP5_RXADDR);
    SetEPRxCount(ENDP5, CDC_BULK_SZ);
    SetEPRxStatus(ENDP5, EP_RX_VALID);
    SetEPTxStatus(ENDP5, EP_TX_DIS);
    _ClearDTOG_RX(ENDP5);

    /* Endpoint 6: bridge bulk IN */
    SetEPType(ENDP6, EP_BULK);
    SetEPTxAddr(ENDP6, ENDP6_TXADDR);
    SetEPTxCount(ENDP6, BRIDGE_PACKET_SZ);
    SetEPTxStatus(ENDP6, EP_TX_NAK);
    SetEPRxStatus(ENDP6, EP_RX_DIS);
    _ClearDTOG_TX(ENDP6);

    /* Endpoint 7: bridge bulk OUT */
    SetEPType(ENDP7, EP_BULK);
    SetEPRxAddr(ENDP7, ENDP7_RXADDR);
    SetEPRxCount(ENDP7, BRIDGE_PACKET_SZ);
    SetEPRxStatus(ENDP7, EP_RX_VALID);
    SetEPTxStatus(ENDP7, EP_TX_DIS);
    _ClearDTOG_RX(ENDP7);

    SetDeviceAddress(0);

    bDeviceState = ATTACHED;
}

uint8_t *USBD_GetDeviceDescriptor(uint16_t Length)
{
    return Standard_GetDescriptorData(Length, &Device_Descriptor);
}

uint8_t *USBD_GetConfigDescriptor(uint16_t Length)
{
    return Standard_GetDescriptorData(Length, &Config_Descriptor);
}

uint8_t *USBD_GetStringDescriptor(uint16_t Length)
{
    uint8_t wValue0 = pInformation->USBwValue0;

    if (wValue0 > 7)
    {
        return NULL;
    }
    return Standard_GetDescriptorData(Length, &String_Descriptor[wValue0]);
}

RESULT USBD_Get_Interface_Setting(uint8_t Interface, uint8_t AlternateSetting)
{
    if (AlternateSetting > 0)
    {
        return USB_UNSUPPORT;
    }
    else if (Interface > 3)
    {
        return USB_UNSUPPORT;
    }

    return USB_SUCCESS;
}

uint8_t *USB_CDC_GetLineCoding(uint16_t Length)
{
    if (Length == 0)
    {
        pInformation->Ctrl_Info.Usb_wLength = 7;
        return NULL;
    }
    return (uint8_t *)&LineCfg;
}

uint8_t *USB_CDC_SetLineCoding(uint16_t Length)
{
    if (Length == 0)
    {
        pInformation->Ctrl_Info.Usb_wLength = 7;
        return NULL;
    }
    return (uint8_t *)&LineCfg;
}

RESULT USBD_Data_Setup(uint8_t RequestNo)
{
    uint32_t Request_No;
    uint8_t *(*CopyRoutine)(uint16_t);
    Request_No = pInformation->USBbRequest;
    CopyRoutine = NULL;

    extern uint8_t g_dapV2Mode;
    if (Type_Recipient == (STANDARD_REQUEST | INTERFACE_RECIPIENT) && !g_dapV2Mode)
    {
        uint8_t wValue1 = pInformation->USBwValue1;
        if (wValue1 == HID_REPORT_DESCRIPTOR)
        {
            CopyRoutine = USBD_GetReportDescriptor;
        }
        else if (wValue1 == HID_DESCRIPTOR)
        {
            CopyRoutine = USBD_GetHidDescriptor;
        }
        if (CopyRoutine)
        {
            pInformation->Ctrl_Info.CopyData = CopyRoutine;
            pInformation->Ctrl_Info.Usb_wOffset = 0;
            (*CopyRoutine)(0);
            return USB_SUCCESS;
        }
    }

    /* HID class requests on interface 0 (v1 mode only) */
    if (!g_dapV2Mode && Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
    {
        {
            (void)0;
            if (Request_No == HID_SET_REPORT)   /* 0x09: host sends DAP command */
            {
                CopyRoutine = &USB_HID_SetReport;
            }
            else if (Request_No == HID_GET_REPORT)  /* 0x01: host reads DAP response */
            {
                CopyRoutine = &USB_HID_GetReport;
            }
            if (CopyRoutine)
            {
                pInformation->Ctrl_Info.CopyData = CopyRoutine;
                pInformation->Ctrl_Info.Usb_wOffset = 0;
                (*CopyRoutine)(0);
                return USB_SUCCESS;
            }
        }
    }

    if (Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
    {
        if (Request_No == CDC_GET_LINE_CODING)
        {
            CopyRoutine = &USB_CDC_GetLineCoding;
        }
        else if (Request_No == CDC_SET_LINE_CODING)
        {
            CopyRoutine = &USB_CDC_SetLineCoding;
        }
        else
        {
            return USB_UNSUPPORT;
        }
    }
    else
    {
        return USB_UNSUPPORT;
    }

    if (CopyRoutine)
    {
        pInformation->Ctrl_Info.CopyData = CopyRoutine;
        pInformation->Ctrl_Info.Usb_wOffset = 0;
        (*CopyRoutine)(0);
    }
    else
    {
        return USB_UNSUPPORT;
    }

    return USB_SUCCESS;
}

RESULT USBD_NoData_Setup(uint8_t RequestNo)
{
    uint32_t Request_No = pInformation->USBbRequest;

    if (Type_Recipient == (CLASS_REQUEST | INTERFACE_RECIPIENT))
    {
        if (Request_No == CDC_SET_LINE_CTLSTE)
        {
        }
        else if (Request_No == CDC_SEND_BREAK)
        {
        }
        else
        {
            return USB_UNSUPPORT;
        }
    }
    return USB_SUCCESS;
}

/* runtime re-binding of the active configuration descriptor (v1/v2 mode) */
void USBD_SelectConfigDescriptor(const uint8_t *desc, uint16_t size)
{
    Config_Descriptor.Descriptor      = (uint8_t*)desc;
    Config_Descriptor.Descriptor_Size = size;
}
