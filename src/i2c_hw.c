/********************************** (C) COPYRIGHT *******************************
 * File Name          : i2c_hw.c
 * Description        : CH32V203 I2C1 hardware master driver for the USB-I2C
 *                      bridge. SCL = PB6, SDA = PB7 (I2C1 default mapping,
 *                      APB1 @ 72 MHz). Register-level master: START/STOP,
 *                      7-bit addressing, blocking with timeouts.
 *
 *  Wait-state cheat sheet (STAR1/STAR2, STM32F1-style):
 *    STAR1: SB(0) ADDR(1) BTF(2) TXE(7) RxNE(6) AF(10) BERR(8) ARLO(9)
 *    STAR2: MSL(0) BUSY(1) TRA(2) - read STAR1 then STAR2 clears ADDR
*******************************************************************************/
#include <stdint.h>
#include "i2c_hw.h"

/* I2C1 (APB1, 0x40005400) - register offsets in the peripheral map */
#define I2C1_BASE           0x40005400u
#define I2C_CTLR1           (*(volatile uint16_t *)(I2C1_BASE + 0x00))
#define I2C_CTLR2           (*(volatile uint16_t *)(I2C1_BASE + 0x04))
#define I2C_OADDR1          (*(volatile uint16_t *)(I2C1_BASE + 0x08))
#define I2C_DATAR           (*(volatile uint16_t *)(I2C1_BASE + 0x10))
#define I2C_STAR1           (*(volatile uint16_t *)(I2C1_BASE + 0x14))
#define I2C_STAR2           (*(volatile uint16_t *)(I2C1_BASE + 0x18))
#define I2C_CKCFGR          (*(volatile uint16_t *)(I2C1_BASE + 0x1C))
#define I2C_RTR             (*(volatile uint16_t *)(I2C1_BASE + 0x20))

/* CTLR1 bits */
#define I2C_PE              (1u << 0)
#define I2C_START           (1u << 8)
#define I2C_STOP            (1u << 9)
#define I2C_ACK             (1u << 10)
#define I2C_SWRST           (1u << 15)
/* STAR1 bits */
#define I2C_SB              (1u << 0)
#define I2C_ADDR            (1u << 1)
#define I2C_BTF             (1u << 2)
#define I2C_RXNE            (1u << 6)
#define I2C_TXE             (1u << 7)
#define I2C_BERR            (1u << 8)
#define I2C_ARLO            (1u << 9)
#define I2C_AF              (1u << 10)
/* STAR2 bits */
#define I2C_MSL             (1u << 0)

/* GPIOB PB6/PB7 - AF open-drain 50 MHz (nibble: CNF=11 MODE=11) */
#define GPIOB_CFGLR         (*(volatile uint32_t *)0x40010C00)
#define RCC_APB2ENR         (*(volatile uint32_t *)0x40021018)
#define RCC_APB1ENR         (*(volatile uint32_t *)0x4002101C)
#define RCC_APB2ENR_GPIOB   (1u << 3)
#define RCC_APB2ENR_AFIO    (1u << 0)
#define RCC_APB1ENR_I2C1    (1u << 21)

#define PCLK1_HZ            72000000u   /* APB1 = HCLK/2 = 72 MHz */

#define I2C_TIMEOUT         72000u      /* ~1 ms spin @ 72 MHz */

static uint8_t hwReady = 0;

static uint8_t wait_set(volatile uint16_t *reg, uint16_t mask)
{
    uint32_t t = I2C_TIMEOUT;
    while (--t)
        if (*reg & mask)
            return 0;
    return 1;                                   /* timeout */
}

void I2C_HW_Init(void)
{
    RCC_APB2ENR |= RCC_APB2ENR_GPIOB | RCC_APB2ENR_AFIO;
    RCC_APB1ENR |= RCC_APB1ENR_I2C1;

    /* PB6/PB7: alternate-function open-drain, 50 MHz */
    GPIOB_CFGLR = (GPIOB_CFGLR & ~(0xFu << (6 * 4))) | (0xFu << (6 * 4));
    GPIOB_CFGLR = (GPIOB_CFGLR & ~(0xFu << (7 * 4))) | (0xFu << (7 * 4));

    I2C_CTLR2 = (I2C_CTLR2 & 0xFFC0u) | 72u;    /* FREQ = APB1 MHz */
    I2C_HW_SetSpeed(100000u);                   /* safe default */
    I2C_CTLR1 |= I2C_ACK;
    I2C_CTLR1 |= I2C_PE;
    hwReady = 1;
}

uint8_t I2C_HW_SetSpeed(uint32_t hz)
{
    uint32_t ccr;
    uint16_t fs = 0;

    if (hz == 0u)
        hz = 100000u;
    if (hz > 400000u)
    {
        fs = 1u;                                /* fast mode */
        ccr = PCLK1_HZ / (3u * hz);             /* DUTY=0: t_low=t_high */
    }
    else
    {
        ccr = PCLK1_HZ / (2u * hz);
    }
    if (ccr < 4u)
        ccr = 4u;
    if (ccr > 0xFFEu)
        ccr = 0xFFEu;

    I2C_CKCFGR = (uint16_t)((fs << 15) | ccr);
    I2C_RTR = (PCLK1_HZ / 1000000u) + 1u;       /* TRISE = APB1 MHz + 1 */
    return 0u;
}

/* START generation + 7-bit address phase (rw: 0=write, 1=read) */
static uint8_t i2c_start_addr(uint8_t addr7, uint8_t rw)
{
    uint32_t t = I2C_TIMEOUT;

    I2C_CTLR1 |= I2C_START;
    while (--t)
        if (I2C_STAR1 & I2C_SB)
            break;
    if (t == 0u)
        return 1u;

    I2C_DATAR = (uint16_t)((addr7 << 1) | rw);  /* clears SB */

    t = I2C_TIMEOUT;
    while (--t)
        if (I2C_STAR1 & I2C_ADDR)
            break;
    if (t == 0u)
        return 2u;                              /* no ACK on address */

    (void)I2C_STAR1;                            /* read STAR1 then STAR2 */
    (void)I2C_STAR2;                            /* clears ADDR */
    return 0u;
}

static void i2c_stop(void)
{
    I2C_CTLR1 |= I2C_STOP;
    {
        uint32_t t = I2C_TIMEOUT;
        while (--t)
            if ((I2C_CTLR1 & I2C_STOP) == 0u)
                break;
    }
}

uint8_t I2C_HW_Write(uint8_t addr7, const uint8_t *d, uint8_t n, uint8_t stop)
{
    if (!hwReady)
        return 1u;

    uint8_t st = i2c_start_addr(addr7, 0u);
    if (st)
    {
        i2c_stop();
        return st ? 2u : 3u;
    }

    while (n--)
    {
        if (wait_set(&I2C_STAR1, I2C_TXE))
        {
            i2c_stop();
            return 4u;
        }
        I2C_DATAR = *d++;
    }
    if (wait_set(&I2C_STAR1, I2C_BTF))          /* last byte fully shifted */
    {
        i2c_stop();
        return 4u;
    }

    if (stop)
        i2c_stop();
    return 0u;
}

uint8_t I2C_HW_Read(uint8_t addr7, uint8_t *d, uint8_t n, uint8_t stop)
{
    if (!hwReady)
        return 1u;
    if (n == 0u)
    {
        if (stop)
            i2c_stop();
        return 0u;
    }

    uint8_t st = i2c_start_addr(addr7, 1u);
    if (st)
    {
        i2c_stop();
        return st ? 2u : 3u;
    }

    for (uint8_t i = 0; i < n; i++)
    {
        if (i == n - 1u)
            I2C_CTLR1 &= ~I2C_ACK;              /* NACK the last byte */
        if (wait_set(&I2C_STAR1, I2C_RXNE))
        {
            I2C_CTLR1 |= I2C_ACK;
            i2c_stop();
            return 4u;
        }
        d[i] = (uint8_t)I2C_DATAR;
    }
    I2C_CTLR1 &= ~I2C_ACK;

    if (stop)
        i2c_stop();
    I2C_CTLR1 |= I2C_ACK;                       /* restore ACK for next txn */
    return 0u;
}

/* full bus recovery: SWRST + re-init */
void I2C_HW_Reset(void)
{
    I2C_CTLR1 |= I2C_SWRST;
    I2C_CTLR1 &= ~I2C_SWRST;
    I2C_CTLR2 = (I2C_CTLR2 & 0xFFC0u) | 72u;
    I2C_HW_SetSpeed(100000u);
    I2C_CTLR1 |= I2C_ACK;
    I2C_CTLR1 |= I2C_PE;
}
