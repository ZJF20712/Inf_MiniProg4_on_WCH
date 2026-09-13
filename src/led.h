#ifndef __LED_H
#define __LED_H

#include <stdint.h>

/* LED states are the KHPI 0x83 sub-codes (khpi.h): 0 ready, 1 programming,
 * 2 success, 3 error */

void Led_Init(void);
void Led_SetState(uint8_t state);   /* KHPI LED state */
void Led_Tick(void);                /* call from main loop: blink patterns */

#endif
