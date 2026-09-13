/********************************** (C) COPYRIGHT *******************************
 * File Name          : khpi.h
 * Description        : KHPI (KitProg Host Protocol Interface) v2.06
 *                      CMSIS-DAP vendor commands 0x80-0x9F, as implemented
 *                      by KitProg3/MiniProg4 firmware (DAP_vendor.c).
*******************************************************************************/
#ifndef __KHPI_H
#define __KHPI_H

#include <stdint.h>

/* KHPI protocol version reported by 0x80 */
#define KHPI_VER_MAJOR      2u
#define KHPI_VER_MINOR      6u

/* Firmware version reported by 0x80 (matches PSoC Programmer's KitProg3 v2.40) */
#define FW_VER_MAJOR        2u
#define FW_VER_MINOR        40u
#define FW_BUILD_NUMBER     1241u

/* MiniProg4 hardware ID (hwid_data.c: HWID 0x05, standalone programmer) */
#define KHPI_HWID           0x05u

/* status codes */
#define KHPI_STAT_SUCCESS       0x00u
#define KHPI_STAT_WAIT          0x01u
#define KHPI_STAT_VOLT_FAILED   0x80u
#define KHPI_STAT_INV_PAR       0x81u
#define KHPI_STAT_OP_FAIL       0x82u

/* 0x82 mode switch sub-commands */
#define MODE_BOOTLOADER         0x00u
#define MODE_CMSIS_DAP2X        0x01u   /* bulk */
#define MODE_CMSIS_DAP1X        0x02u   /* HID  */
#define MODE_CUSTOM_APP         0x03u
#define MODE_CMSIS_DAP2X_2UART  0x04u

/* 0x83 LED states */
#define LED_STATE_READY         0x00u
#define LED_STATE_PROGRAMMING   0x01u
#define LED_STATE_SUCCESS       0x02u
#define LED_STATE_ERROR         0x03u

/* 0x84 power sub-commands */
#define CMD_POWER_SET           0x10u
#define CMD_POWER_GET           0x11u
#define CMD_POWER_OFF           0x00u
#define CMD_POWER_ON            0x01u
#define CMD_POWER_VOLT_SET      0x02u

/* 0x91 acquire options */
#define SET_ACQUIRE_TIMEOUT     0x00u
#define SET_ACQUIRE_HANDSHAKE   0x01u
#define SET_ACQUIRE_AP          0x02u

/* capability bit masks (command 0x90) */
#define I2C_AVAILABILITY_MASK       0x01u
#define SPI_AVAILIBILITY_MASK       0x02u
#define DAPH_AVAILIBILITY_MASK      0x04u
#define DAPB_AVAILIBILITY_MASK      0x08u
#define ON_OFF_SW_AVAILIBILITY_MASK 0x10u
#define VMEAS_AVAILIBILITY_MASK     0x20u
#define GPIO_AVAILIBILITY_MASK      0x40u

#define ONE_LED_KIT_MASK        0x01u
#define THREE_LED_KIT_MASK      0x03u
#define ONE_UART_MASK           0x10u
#define TWO_UART_MASK           0x20u

/* software feature switches - match the real MiniProg4 (HWID 5) config;
 * power/voltage answer virtually until the regulator hardware lands */
#define KHPI_HAS_I2C_BRIDGE     1   /* USB-I2C bridge (protocol answered)   */
#define KHPI_HAS_SPI_BRIDGE     1   /* USB-SPI bridge (protocol answered)   */
#define KHPI_HAS_GPIO_BRIDGE    0   /* GPIO bridge               (HW)  */
#define KHPI_HAS_POWER_CONTROL  1   /* target power switch (virtual)        */
#define KHPI_HAS_VOLT_MEAS      1   /* VTARG measurement (virtual 3.3V)     */

/* capability field values matching hwid_data.c HWID 0x05 */
#define I2C_SPEEDS_MASK         0x0Fu   /* 50k/100k/400k/1M               */
#define VOLT_SUPPORT_MASK       0x0Fu   /* 1.8|2.5|3.3|5.0 V              */
#define SPI_SS_LINES_MASK       0x07u   /* SS0..SS2                       */
#define SPI_MIN_HZ              366u    /* 24MHz/2/32767                  */
#define SPI_MAX_HZ              6000000u/* 24MHz/2/2                      */

#define PROBE_CAP_RESP_LEN      15u

extern volatile uint8_t bridgeEnabled;

uint32_t DAP_ProcessVendorCommand(const uint8_t *request, uint8_t *response);

#endif /* __KHPI_H */
