/********************************** (C) COPYRIGHT *******************************
 * File Name          : vcom_serial.c
 * Description        : CDC data (EP4 IN / EP5 OUT) <-> USART2 (PA2/PA3) DMA
 *                      bridge, adapted from DAPLink-CH32V203 for the
 *                      MiniProg4 endpoint layout.
 *
 *  NOTE: the CDC UART maps to USART2 here so that USART1 (PA9) stays free
 *  for debug logging through the WCH-Link COM port. Pin assignment for the
 *  target-side UART is part of the hardware feature table (README).
*******************************************************************************/
#include <string.h>
#include "ch32v20x.h"
#include "usb_lib.h"
#include "usb_regs.h"
#include "usb_desc.h"
#include "vcom_serial.h"

volatile VCOM Vcom;

VCOM_LINE_CODING LineCfg = {115200, 0, 0, 8};   /* baud, stop, parity, data */

#define RXDMA_SZ  (CDC_BULK_SZ * 2)
static uint8_t RXBuffer[RXDMA_SZ] __attribute__((aligned(4)));
static uint8_t TXBuffer[CDC_BULK_SZ] __attribute__((aligned(4)));

void VCOM_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    DMA_InitTypeDef   DMA_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_AHBPeriphClockCmd (RCC_AHBPeriph_DMA1, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);              /* PA2 => USART2_TX */

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);              /* PA3 => USART2_RX */

    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&USART2->DATAR;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryBaseAddr = (u32)TXBuffer;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_BufferSize = 0;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel7, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel7, ENABLE);

    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&USART2->DATAR;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryBaseAddr = (u32)RXBuffer;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_BufferSize = RXDMA_SZ;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel6, &DMA_InitStructure);
    DMA_Cmd(DMA1_Channel6, ENABLE);

    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART2, &USART_InitStructure);

    USART_DMACmd(USART2, USART_DMAReq_Tx | USART_DMAReq_Rx, ENABLE);

    USART_Cmd(USART2, ENABLE);
}

void VCOM_LineCoding(VCOM_LINE_CODING *LineCfgx)
{
    USART_InitTypeDef USART_InitStructure;

    switch(LineCfgx->u8DataBits)
    {
    case 8:  USART_InitStructure.USART_WordLength = USART_WordLength_8b; break;
    default: USART_InitStructure.USART_WordLength = USART_WordLength_8b; break;
    }

    switch(LineCfgx->u8ParityType)
    {
    case 0:  USART_InitStructure.USART_Parity     = USART_Parity_No;     break;
    case 1:  USART_InitStructure.USART_Parity     = USART_Parity_Odd;
             USART_InitStructure.USART_WordLength = USART_WordLength_9b; break;
    case 2:  USART_InitStructure.USART_Parity     = USART_Parity_Even;
             USART_InitStructure.USART_WordLength = USART_WordLength_9b; break;
    default: USART_InitStructure.USART_Parity     = USART_Parity_No;     break;
    }

    switch(LineCfgx->u8CharFormat)
    {
    case 0:  USART_InitStructure.USART_StopBits = USART_StopBits_1;   break;
    case 1:  USART_InitStructure.USART_StopBits = USART_StopBits_1_5; break;
    case 2:  USART_InitStructure.USART_StopBits = USART_StopBits_2;   break;
    default: USART_InitStructure.USART_StopBits = USART_StopBits_1;   break;
    }

    USART_InitStructure.USART_BaudRate = LineCfgx->u32DTERate;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;

    /* NO global IRQ masking: this runs in USB request context (HPE fast
     * ISR) and forcing GIE here wedged the whole interrupt system */
    USART_Init(USART2, &USART_InitStructure);
}

void VCOM_TransferData(void)
{
    static uint32_t last_pos = 0;

    if (!Vcom.ep_in_ready) return;

    /* uart -> host */
    uint32_t pos = RXDMA_SZ - DMA_GetCurrDataCounter(DMA1_Channel6);
    if (pos != last_pos)
    {
        uint32_t len;
        if (pos > last_pos) len = pos - last_pos;
        else                len = RXDMA_SZ - last_pos;      /* wrapped */

        if (len > CDC_BULK_SZ) len = CDC_BULK_SZ;

        USB_SIL_Write(EP4_IN, &RXBuffer[last_pos], len);
        SetEPTxValid(ENDP4);
        Vcom.ep_in_ready = 0;

        last_pos = (last_pos + len) % RXDMA_SZ;
    }
    else if (last_pos == pos)
    {
        /* no new data: idle, keep endpoint ready for next poll */
    }

    /* host -> uart */
    if (Vcom.out_bytes && (DMA_GetCurrDataCounter(DMA1_Channel7) == 0))
    {
        memcpy(TXBuffer, (uint8_t *)Vcom.out_buff, Vcom.out_bytes);

        DMA_Cmd(DMA1_Channel7, DISABLE);
        DMA_SetCurrDataCounter(DMA1_Channel7, Vcom.out_bytes);
        DMA_Cmd(DMA1_Channel7, ENABLE);

        Vcom.out_bytes = 0;

        /* ready for next bulk OUT */
        SetEPRxValid(ENDP5);
    }
}
