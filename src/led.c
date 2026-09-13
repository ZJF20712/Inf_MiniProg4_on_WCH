#include "ch32v20x.h"
#include "khpi.h"
#include "led.h"

/* Status LED on PA5 (DAPLink-CH32V203 pinout).
 * READY:       solid off (dim)        PROGRAMMING: solid on
 * SUCCESS:     short blink            ERROR:       fast blink
 * A real MiniProg4 has three LEDs (amber/green) - see README feature table. */

static volatile uint8_t ledMode = LED_STATE_READY;
static volatile uint32_t ledTick = 0;
static volatile uint32_t sysTicks = 0;

void SysTick_Increment(void)   /* hooked from main.c SysTick_Handler */
{
    sysTicks++;
}

void Led_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_5;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    GPIO_ResetBits(GPIOA, GPIO_Pin_5);
}

void Led_SetState(uint8_t state)
{
    ledMode = state;
}

void Led_Tick(void)
{
    if (sysTicks == ledTick) return;

    uint32_t period;
    switch (ledMode)
    {
    case LED_STATE_PROGRAMMING:
        GPIO_SetBits(GPIOA, GPIO_Pin_5);        /* solid on */
        return;
    case LED_STATE_SUCCESS:
        period = 400;                           /* blink 1Hz-ish */
        break;
    case LED_STATE_ERROR:
        period = 120;                           /* fast blink */
        break;
    case LED_STATE_READY:
    default:
        GPIO_ResetBits(GPIOA, GPIO_Pin_5);
        return;
    }

    if (sysTicks - ledTick >= period)
    {
        ledTick = sysTicks;
        GPIOA->OUTDR ^= GPIO_Pin_5;
    }
}
