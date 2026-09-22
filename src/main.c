/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Description        : WCH-MiniProg4 - MiniProg4 (KitProg3) compatible
 *                      programmer/debugger on CH32V203G6U6.
 *
 *  USB: composite device, VID 0x04B4 / PID 0xF151 (MiniProg4 bulk mode)
 *    IF0  CMSIS-DAP v2 bulk (WinUSB)   EP1 IN / EP2 OUT
 *    IF1  Bridge bulk (WinUSB)         EP6 IN / EP7 OUT
 *    IF2/3 CDC-UART                    EP3 int, EP4 IN / EP5 OUT
 *
 *  KHPI vendor commands (0x80-0x94) ride the DAP endpoint.
*******************************************************************************/
#include "ch32v20x.h"
#include "usb_lib.h"
#include "usb_istr.h"
#include "usb_desc.h"

#include "DAP.h"
#include "dap_usb.h"
#include "bridge.h"
#include "khpi.h"
#include "led.h"
#include "debug.h"
#include "vcom_serial.h"

volatile uint32_t SysTick_ms = 0;

void SysTick_Config(uint32_t ticks);
void USB_Config(void);
void Led_SysTickHook(void);
void SysTick_Increment(void);

int main(void)
{
    SystemCoreClockUpdate();

    /* SysTick first: Delay_Ms and TIMESTAMP_GET derive from STK CNT, and
     * USB_Config already delays (usb_prop) before its former position */
    SysTick_Config(SystemCoreClock / 1000);

    Debug_Init(115200);
    Debug_Print("\r\n=== WCH-MiniProg4 ===\r\nfw %d.%02d build %d | KHPI %d.%d | HWID 0x%02X | %s\r\n",
                FW_VER_MAJOR, FW_VER_MINOR, FW_BUILD_NUMBER,
                KHPI_VER_MAJOR, KHPI_VER_MINOR, KHPI_HWID,
#ifdef DAP_VIRTUAL_TARGET
                "VIRTUAL-TARGET"
#else
                "REAL-TARGET"
#endif
               );

    DAP_Setup();
    Debug_Print("init: DAP ok\r\n");

    Led_Init();
    Debug_Print("init: led ok\r\n");

    VCOM_Init();
    Debug_Print("init: vcom ok\r\n");

    USB_Config();
    Debug_Print("init: usb configured, main loop start\r\n");

    while (1)
    {
        DAP_Process();
        Bridge_Process();
        VCOM_TransferData();
        Led_Tick();

        /* heartbeat: proves the main loop is alive + endpoint states */
        static uint32_t lastMs = 0;
        if ((SysTick_ms - lastMs) >= 3000)
        {
            lastMs = SysTick_ms;
            {
                extern volatile uint32_t istrCount;
                extern volatile uint32_t resetCount;
                extern volatile uint32_t setupCount;
                Debug_Print("alive %d istr=%d rst=%d setup=%d log:",
                            SysTick_ms, istrCount, resetCount, setupCount);
                {
                    extern volatile uint8_t ep0log[8][6];
                    extern volatile uint8_t ep0logIdx;
                    for (int i = 0; i < 8; i++) {
                        uint8_t idx = (ep0logIdx - 1 - i) & 7;
                        volatile uint8_t *e = ep0log[idx];
                        if (e[0] != 0 || e[1] != 0 || e[2] != 0)
                            Debug_Print(" [%02X %02X%02X %02X%02X %02X]", e[0], e[1], e[2], e[3], e[4], e[5]);
                    }
                    Debug_Print("\r\n");
                }
            }
        }
    }
}

void SysTick_Config(uint32_t ticks)
{
    SysTick->CTLR = 0;
    SysTick->SR   = 0;
    SysTick->CNT  = 0;
    SysTick->CMP  = ticks;
    SysTick->CTLR = 0x0F;

    NVIC_EnableIRQ(SysTicK_IRQn);
}

void SysTick_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SysTick_Handler(void)
{
    SysTick->SR = 0;
    SysTick_ms++;
    SysTick_Increment();
}

void int_to_unicode(uint32_t value, uint8_t *pbuf, uint8_t len);
void USB_Config(void)
{
    /* virtual bootloader mode: SRAM flag persists across the warm reset -
     * re-enumerate as PID 0xF146 until the flag is cleared */
    uint16_t pid = 0;
    if ((*(volatile uint32_t *)0x20001800u & 0xFFFF0000u) == 0xB0070000u)
    {
        extern uint8_t USBD_DeviceDescriptor[];
        pid = *(volatile uint32_t *)0x20001800u & 0xFFFFu;
        USBD_DeviceDescriptor[10] = (uint8_t)(pid & 0xFFu);   /* idProduct LSB */
        USBD_DeviceDescriptor[11] = (uint8_t)(pid >> 8);      /* idProduct MSB */
    }

    /* runtime DAP mode: 0x82 mode switch persists PID F152(v1 HID)/F151(v2
     * bulk) in the same flag; select the descriptor set + EP roles + BOS
     * MS-OS-2.0 total length to match.
     * Default (no flag, e.g. after power-up) = v2 bulk for PSoC Creator;
     * the virtual bootloader (F146) always stays v1 HID for fw-loader. */
    extern uint8_t g_dapV2Mode;
    g_dapV2Mode = (pid == 0xF151u) || (pid == 0x0000u);
    extern const uint8_t USBD_ConfigDescriptor_V1[];
    extern const uint8_t USBD_ConfigDescriptor_V2[];
    extern void USBD_SelectConfigDescriptor(const uint8_t *desc, uint16_t size);
    extern uint8_t BOS_Descriptor[];
    USBD_SelectConfigDescriptor(g_dapV2Mode ? USBD_ConfigDescriptor_V2 : USBD_ConfigDescriptor_V1,
                                g_dapV2Mode ? USBD_SIZE_CONFIG_TOTAL_V2 : USBD_SIZE_CONFIG_TOTAL_V1);
    BOS_Descriptor[29] = g_dapV2Mode ? 0x4A : 0xAE;   /* MS OS 2.0 set length */
    BOS_Descriptor[30] = g_dapV2Mode ? 0x01 : 0x00;

    NVIC_InitTypeDef NVIC_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;

    RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_Div3);     /* 144/3 = 48 MHz */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, ENABLE);

    /* fill serial number string with chip unique ID (16 hex chars) */
    extern uint8_t USBD_StringSerial[];
    uint32_t serial0 = *(uint32_t*)0x1FFFF7E8;
    uint32_t serial1 = *(uint32_t*)0x1FFFF7EC;
    uint32_t serial2 = *(uint32_t*)0x1FFFF7F0;

    /* XOR keeps it unique per chip but changes the string, so Windows
     * installs fresh devnodes and (re)reads the MS OS 2.0 registry
     * properties (DeviceInterfaceGUID for the WinUSB bridge). */
    int_to_unicode(serial0 ^ serial2, &USBD_StringSerial[2],  6);
    int_to_unicode(serial1 ^ serial2, &USBD_StringSerial[14], 6);

    USB_Init();

    NVIC_InitStructure.NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    EXTI_ClearITPendingBit(EXTI_Line18);
    EXTI_InitStructure.EXTI_Line = EXTI_Line18;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = USBWakeUp_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_Init(&NVIC_InitStructure);
}

void USB_LP_CAN1_RX0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBWakeUp_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    USB_Istr();
}

void USBWakeUp_IRQHandler(void)
{
    EXTI_ClearITPendingBit(EXTI_Line18);
}

/*******************************************************************************
 * @fn         USB_Port_Set
 *
 * @brief      Set USB IO port.
 */
void USB_Port_Set(FunctionalState NewState, FunctionalState Pin_In_IPU)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    if (NewState)
    {
        _SetCNTR(_GetCNTR()&(~(1<<1)));
        GPIOA->CFGHR&=0XFFF00FFF;
        GPIOA->OUTDR&=~(3<<11);     /* PA11/12=0 */
        GPIOA->CFGHR|=0X00044000;   /* float */
    }
    else
    {
        _SetCNTR(_GetCNTR()|(1<<1));
        GPIOA->CFGHR&=0XFFF00FFF;
        GPIOA->OUTDR&=~(3<<11);     /* PA11/12=0 */
        GPIOA->CFGHR|=0X00033000;   /* LOW */
    }

    if (Pin_In_IPU) EXTEN->EXTEN_CTR |=  EXTEN_USBD_PU_EN;
    else            EXTEN->EXTEN_CTR &= ~EXTEN_USBD_PU_EN;
}

void Delay_Ms(uint32_t n)
{
    /* Poll the STK CNT hardware counter (free-running at HCLK, keeps counting
     * with interrupts disabled). The old NOP loop was codegen-calibrated and
     * ran ~5x slow, which pushed the acquire hammer past the CCG5 DAP window.
     * Requires SysTick_Config to have run (it now leads main()). */
    uint64_t t0    = SysTick->CNT;
    uint64_t ticks = (uint64_t)n * (SystemCoreClock / 1000u);
    while ((SysTick->CNT - t0) < ticks) { }
}

void int_to_unicode(uint32_t value, uint8_t *pbuf, uint8_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        if ((value >> 28) < 0xA)
        {
            pbuf[2 * i] = (value >> 28) + '0';
        }
        else
        {
            pbuf[2 * i] = (value >> 28) + 'A' - 10;
        }

        pbuf[2 * i + 1] = 0;

        value = value << 4;
    }
}
