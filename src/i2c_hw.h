#ifndef __I2C_HW_H
#define __I2C_HW_H

#include <stdint.h>

/* CH32V203 I2C1 hardware master on PB6(SCL)/PB7(SDA), APB1 @ 72 MHz.
 * 7-bit addressing; addr7 is shifted internally. All calls block with
 * a ~1 ms timeout per wait state and return 0 on success. */

void   I2C_HW_Init(void);
uint8_t I2C_HW_SetSpeed(uint32_t hz);                                   /* 0 = OK */
uint8_t I2C_HW_Write(uint8_t addr7, const uint8_t *d, uint8_t n, uint8_t stop);
uint8_t I2C_HW_Read (uint8_t addr7, uint8_t *d, uint8_t n, uint8_t stop);
void   I2C_HW_Reset(void);                                              /* bus recovery */

#endif /* __I2C_HW_H */
