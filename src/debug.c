#include "ch32v20x.h"
#include "debug.h"
#include <stdarg.h>

static char nibble(uint32_t v) { return (v < 10) ? ('0' + v) : ('A' + v - 10); }

void Debug_Init(uint32_t baud)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_9;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = baud;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_Mode                = USART_Mode_Tx;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &usart);

    USART_Cmd(USART1, ENABLE);
}

static void debug_putc(char c)
{
    while (!USART_GetFlagStatus(USART1, USART_FLAG_TXE)) ;
    USART_SendData(USART1, (uint16_t)c);
}

static void debug_puts(const char *s)
{
    while (*s) debug_putc(*s++);
}

void Debug_Print(const char *fmt, ...)
{
    /* tiny formatter: %s %x %X %d %u %c %% and zero-padded widths (%02d) */
    char buf[12];
    va_list ap;
    va_start(ap, fmt);

    while (*fmt)
    {
        if (*fmt != '%')
        {
            debug_putc(*fmt++);
            continue;
        }
        fmt++;

        /* optional flags/width: only '0' + digits */
        uint8_t zeroPad = 0, width = 0;
        while (*fmt == '0' || (*fmt >= '1' && *fmt <= '9'))
        {
            if (*fmt == '0' && width == 0) zeroPad = 1;
            else width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt)
        {
        case 's':
            debug_puts(va_arg(ap, const char *));
            break;
        case 'c':
            debug_putc((char)va_arg(ap, int));
            break;
        case 'x':
        case 'X':
        {
            uint32_t v = va_arg(ap, uint32_t);
            int i;
            for (i = 28; i >= 0; i -= 4) debug_putc(nibble((v >> i) & 0xF));
            break;
        }
        case 'd':
        case 'u':
        {
            uint32_t v = va_arg(ap, uint32_t);
            int i = sizeof(buf) - 1;
            buf[i] = 0;
            if (v == 0) buf[--i] = '0';
            while (v) { buf[--i] = '0' + (v % 10); v /= 10; }
            if (width && zeroPad)
            {
                while ((uint32_t)(sizeof(buf) - 1 - i) < width) buf[--i] = '0';
            }
            debug_puts(&buf[i]);
            break;
        }
        case '%':
            debug_putc('%');
            break;
        case 0:
            va_end(ap);
            return;
        default:
            debug_putc(*fmt);
            break;
        }
        fmt++;
    }

    va_end(ap);
}
