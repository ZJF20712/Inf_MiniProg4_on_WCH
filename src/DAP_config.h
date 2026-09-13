/*
 * DAP_config.h - CMSIS-DAP configuration for WCH-MiniProg4 (CH32V203G6U6)
 *
 * Pin assignment follows the DAPLink-CH32V203 reference design:
 *   SWCLK/TCK : PA1        SWDIO/TMS : PA0        nRESET : PA4 (open drain)
 *   TDI       : PA7        TDO       : PA6        LED    : PA5
 *   USB       : PA11/PA12  Debug log : PA9 (USART1 TX -> WCH-Link COM26)
 *   CDC UART  : PA2/PA3 (USART2)
 *
 * Version strings report KitProg3 v2.40 so that PSoC Programmer / fw-loader
 * treat the probe as a current MiniProg4 firmware.
 */

#ifndef __DAP_CONFIG_H__
#define __DAP_CONFIG_H__

#ifdef _RTE_
#include "RTE_Components.h"
#include CMSIS_device_header
#else
#include "ch32v20x.h"
#include "cmsis_compiler.h"
#endif

/// Processor Clock of the MCU used in the Debug Unit.
#define CPU_CLOCK               144000000U      /* CH32V203 @ 144 MHz (HSI PLL) */

/// Number of processor cycles for I/O Port write operations.
#define IO_PORT_WRITE_CYCLES    2U              /* QingKe V4: 2 cycles */

/// Serial Wire Debug mode available.
#define DAP_SWD                 1

/// JTAG mode available (MiniProg4 10-pin).
#define DAP_JTAG                1

/// Maximum number of JTAG devices on scan chain.
#define DAP_JTAG_DEV_CNT        8U

/// Default JTAG/SWJ Port Mode: 1 = SWD, 2 = JTAG.
#define DAP_DEFAULT_PORT        1U

/// Default SWD/JTAG clock frequency in Hz.
#define DAP_DEFAULT_SWJ_CLOCK   1000000U

/// Maximum Package Size in bytes (full-speed bulk = 64).
#define DAP_PACKET_SIZE         64U

/// Number of packet buffers (RAM budget: CH32V203G6 has 10KB).
/// Matches the official MiniProg4/KitProg3 capacity; PSoC Programmer's
/// library batches up to this many commands per burst.
#define DAP_PACKET_COUNT        8U

/// SWO trace not implemented yet (hardware feature table).
#define SWO_UART                0
#define SWO_UART_DRIVER         0
#define SWO_UART_MAX_BAUDRATE   10000000U
#define SWO_MANCHESTER          0
#define SWO_BUFFER_SIZE         4096U
#define SWO_STREAM              0

/// Timestamp clock (0 = not supported).
/* ms-tick derived, 1us nominal granularity - bounds the DAP_SWJ_Pins wait
 * loop (a constant-0 timestamp made that loop infinite when the waited-for
 * pin state never occurred, wedging the whole probe) */
#define TIMESTAMP_CLOCK         1000000U

/// DAP command-channel UART not implemented.
#define DAP_UART                0
#define DAP_UART_DRIVER         1
#define DAP_UART_RX_BUFFER_SIZE 1024U
#define DAP_UART_TX_BUFFER_SIZE 1024U
#define DAP_UART_USB_COM_PORT   0

/// Not connected to a fixed target.
#define TARGET_FIXED            0

#include <string.h>

/** Get Vendor Name string. */
__STATIC_INLINE uint8_t DAP_GetVendorString (char *str) {
  const char v[] = "Cypress";
  memcpy(str, v, sizeof(v));
  return sizeof(v);
}

/** Get Product Name string. */
__STATIC_INLINE uint8_t DAP_GetProductString (char *str) {
  const char v[] = "MiniProg4";
  memcpy(str, v, sizeof(v));
  return sizeof(v);
}

/** Get Serial Number string (USB serial is die-ID based; keep consistent). */
__STATIC_INLINE uint8_t DAP_GetSerNumString (char *str) {
  const char v[] = "0000000000000000";
  memcpy(str, v, sizeof(v));
  return sizeof(v);
}

/** Get Product Firmware Version string (KitProg3 firmware version). */
__STATIC_INLINE uint8_t DAP_GetProductFirmwareVersionString (char *str) {
  const char v[] = "2.40";
  memcpy(str, v, sizeof(v));
  return sizeof(v);
}

/** Target device/board strings: not a fixed-target probe. */
__STATIC_INLINE uint8_t DAP_GetTargetDeviceVendorString (char *str) {
  (void)str; return (0U);
}
__STATIC_INLINE uint8_t DAP_GetTargetDeviceNameString (char *str) {
  (void)str; return (0U);
}
__STATIC_INLINE uint8_t DAP_GetTargetBoardVendorString (char *str) {
  (void)str; return (0U);
}
__STATIC_INLINE uint8_t DAP_GetTargetBoardNameString (char *str) {
  (void)str; return (0U);
}

//**************************************************************************************************
//  Hardware I/O Pin Access
//**************************************************************************************************

// Configure DAP I/O pins ------------------------------
#define SWCLK_PORT          GPIOA
#define SWCLK_PIN           GPIO_Pin_1
#define SWDIO_PORT          GPIOA
#define SWDIO_PIN           GPIO_Pin_0
#define SWDIO_PIN_INDEX     0

#define JTAG_TCK_PORT       SWCLK_PORT
#define JTAG_TCK_PIN        SWCLK_PIN
#define JTAG_TMS_PORT       SWDIO_PORT
#define JTAG_TMS_PIN        SWDIO_PIN
#define JTAG_TDI_PORT       GPIOA
#define JTAG_TDI_PIN        GPIO_Pin_7
#define JTAG_TDO_PORT       GPIOA
#define JTAG_TDO_PIN        GPIO_Pin_6

#define nRESET_PORT         GPIOA
#define nRESET_PIN          GPIO_Pin_4

#define LED_CONNECTED_PORT  GPIOA
#define LED_CONNECTED_PIN   GPIO_Pin_5
#define LED_RUNNING_PORT    GPIOA
#define LED_RUNNING_PIN     GPIO_Pin_5

/** Setup JTAG I/O pins: TCK, TMS, TDI to output high, TDO input. */
__STATIC_INLINE void PORT_JTAG_SETUP (void) {
  GPIO_InitTypeDef GPIO_InitStruct;

  GPIO_SetBits(JTAG_TCK_PORT, JTAG_TCK_PIN);
  GPIO_SetBits(JTAG_TMS_PORT, JTAG_TMS_PIN);
  GPIO_SetBits(JTAG_TDI_PORT, JTAG_TDI_PIN);

  GPIO_InitStruct.GPIO_Pin = JTAG_TCK_PIN;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(JTAG_TCK_PORT, &GPIO_InitStruct);

  GPIO_InitStruct.GPIO_Pin = JTAG_TMS_PIN;
  GPIO_Init(JTAG_TMS_PORT, &GPIO_InitStruct);

  GPIO_InitStruct.GPIO_Pin = JTAG_TDI_PIN;
  GPIO_Init(JTAG_TDI_PORT, &GPIO_InitStruct);

  GPIO_InitStruct.GPIO_Pin = JTAG_TDO_PIN;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IPU;
  GPIO_Init(JTAG_TDO_PORT, &GPIO_InitStruct);
}

/** Setup SWD I/O pins: SWCLK, SWDIO to output high. */
__STATIC_INLINE void PORT_SWD_SETUP (void) {
  GPIO_InitTypeDef GPIO_InitStruct;

  GPIO_SetBits(SWCLK_PORT, SWCLK_PIN);
  GPIO_SetBits(SWDIO_PORT, SWDIO_PIN);

  GPIO_InitStruct.GPIO_Pin = SWCLK_PIN;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(SWCLK_PORT, &GPIO_InitStruct);

  GPIO_InitStruct.GPIO_Pin = SWDIO_PIN;
  GPIO_Init(SWDIO_PORT, &GPIO_InitStruct);
}

/** Disable JTAG/SWD I/O pins (High-Z). */
__STATIC_INLINE void PORT_OFF (void) {
  GPIO_InitTypeDef GPIO_InitStruct;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

  GPIO_InitStruct.GPIO_Pin   = SWCLK_PIN | SWDIO_PIN | JTAG_TDI_PIN | JTAG_TDO_PIN;
  GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(GPIOA, &GPIO_InitStruct);
}

// SWCLK/TCK I/O pin -------------------------------------

/* runtime pin-role swap: 0 = PA1 SWCLK / PA0 SWDIO, 1 = PA0 SWCLK / PA1 SWDIO */
extern uint8_t g_swdSwap;
#define SWCLK_PIN_INDEX     1
#define SWCLK_ACT_PIN       (g_swdSwap ? SWDIO_PIN : SWCLK_PIN)
#define SWDIO_ACT_PIN       (g_swdSwap ? SWCLK_PIN : SWDIO_PIN)
#define SWDIO_ACT_INDEX     (g_swdSwap ? SWCLK_PIN_INDEX : SWDIO_PIN_INDEX)

__STATIC_FORCEINLINE uint32_t PIN_SWCLK_TCK_IN  (void) {
  return (SWCLK_PORT->INDR & SWCLK_ACT_PIN) ? 1 : 0;
}

__STATIC_FORCEINLINE void     PIN_SWCLK_TCK_SET (void) {
  SWCLK_PORT->BSHR = SWCLK_ACT_PIN;
}

__STATIC_FORCEINLINE void     PIN_SWCLK_TCK_CLR (void) {
  SWCLK_PORT->BCR = SWCLK_ACT_PIN;
}

// SWDIO/TMS Pin I/O --------------------------------------

__STATIC_FORCEINLINE uint32_t PIN_SWDIO_TMS_IN  (void) {
  return (SWDIO_PORT->INDR & SWDIO_ACT_PIN) ? 1 : 0;
}

__STATIC_FORCEINLINE void     PIN_SWDIO_TMS_SET (void) {
  SWDIO_PORT->BSHR = SWDIO_ACT_PIN;
}

__STATIC_FORCEINLINE void     PIN_SWDIO_TMS_CLR (void) {
  SWDIO_PORT->BCR = SWDIO_ACT_PIN;
}

__STATIC_FORCEINLINE uint32_t PIN_SWDIO_IN      (void) {
  return (SWDIO_PORT->INDR & SWDIO_ACT_PIN) ? 1 : 0;
}

__STATIC_FORCEINLINE void     PIN_SWDIO_OUT     (uint32_t bit) {
  if (bit & 1) SWDIO_PORT->BSHR = SWDIO_ACT_PIN;
  else         SWDIO_PORT->BCR  = SWDIO_ACT_PIN;
}

__STATIC_FORCEINLINE void     PIN_SWDIO_OUT_ENABLE  (void) {
  SWDIO_PORT->BCR = SWDIO_ACT_PIN;
  SWDIO_PORT->CFGLR = (SWDIO_PORT->CFGLR & ~(0xF <<  SWDIO_ACT_INDEX * 4))
                                        |  (0x3 <<  SWDIO_ACT_INDEX * 4);
}

__STATIC_FORCEINLINE void     PIN_SWDIO_OUT_DISABLE (void) {
  /* DM_DIG_HIZ equivalent (matches official MiniProg4): pure high-Z input,
   * no internal pull-up, so the target's weak DAP drive is not masked */
  SWDIO_PORT->CFGLR = (SWDIO_PORT->CFGLR & ~(0xF <<  SWDIO_ACT_INDEX * 4))
                                        |  (0x4 <<  SWDIO_ACT_INDEX * 4);
}

// TDI Pin I/O ---------------------------------------------

__STATIC_FORCEINLINE uint32_t PIN_TDI_IN  (void) {
  return (JTAG_TDI_PORT->INDR & JTAG_TDI_PIN) ? 1 : 0;
}

__STATIC_FORCEINLINE void     PIN_TDI_OUT (uint32_t bit) {
  if (bit & 1) JTAG_TDI_PORT->BSHR = JTAG_TDI_PIN;
  else         JTAG_TDI_PORT->BCR  = JTAG_TDI_PIN;
}

// TDO Pin I/O ---------------------------------------------

__STATIC_FORCEINLINE uint32_t PIN_TDO_IN  (void) {
  return (JTAG_TDO_PORT->INDR & JTAG_TDO_PIN) ? 1 : 0;
}

// nTRST Pin I/O (not wired) -------------------------------

__STATIC_FORCEINLINE uint32_t PIN_nTRST_IN   (void) {
  return (0U);
}

__STATIC_FORCEINLINE void     PIN_nTRST_OUT  (uint32_t bit) {
  (void)bit;
}

// nRESET Pin I/O ------------------------------------------

__STATIC_FORCEINLINE uint32_t PIN_nRESET_IN  (void) {
  return (nRESET_PORT->INDR & nRESET_PIN) ? 1 : 0;
}

__STATIC_FORCEINLINE void     PIN_nRESET_OUT (uint32_t bit) {
  if (bit & 1) nRESET_PORT->BSHR = nRESET_PIN;
  else         nRESET_PORT->BCR  = nRESET_PIN;
}

//**************************************************************************************************
//  Status LEDs
//**************************************************************************************************

__STATIC_INLINE void LED_CONNECTED_OUT (uint32_t bit) {
  if (bit & 1) LED_CONNECTED_PORT->BSHR = LED_CONNECTED_PIN;
  else         LED_CONNECTED_PORT->BCR  = LED_CONNECTED_PIN;
}

__STATIC_INLINE void LED_RUNNING_OUT (uint32_t bit) {
  if (bit & 1) LED_RUNNING_PORT->BSHR = LED_RUNNING_PIN;
  else         LED_RUNNING_PORT->BCR  = LED_RUNNING_PIN;
}

//**************************************************************************************************
//  Timestamp
//**************************************************************************************************

extern volatile uint32_t SysTick_ms;
__STATIC_INLINE uint32_t TIMESTAMP_GET (void) {
  return SysTick_ms * 1000u;                /* ms tick -> us-scale stamp */
}

//**************************************************************************************************
//  Initialization
//**************************************************************************************************

__STATIC_INLINE void DAP_SETUP (void) {
  PORT_OFF();

  GPIO_InitTypeDef GPIO_InitStruct;

  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

  GPIO_InitStruct.GPIO_Pin   = LED_CONNECTED_PIN;
  GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_Out_PP;
  GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_Init(LED_CONNECTED_PORT, &GPIO_InitStruct);

  GPIO_ResetBits(LED_CONNECTED_PORT, LED_CONNECTED_PIN);

  GPIO_SetBits(nRESET_PORT, nRESET_PIN);

  GPIO_InitStruct.GPIO_Pin = nRESET_PIN;
  GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_OD;
  GPIO_Init(nRESET_PORT, &GPIO_InitStruct);
}

__STATIC_INLINE uint8_t RESET_TARGET (void) {
  return (0U);
}

#endif /* __DAP_CONFIG_H__ */
