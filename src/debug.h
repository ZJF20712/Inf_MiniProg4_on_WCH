#ifndef __DEBUG_H
#define __DEBUG_H

#include <stdint.h>

/* Debug log on USART1 TX = PA9 (routed to the PC through WCH-Link, COM26). */
void Debug_Init(uint32_t baud);
void Debug_Print(const char *fmt, ...);

#endif
