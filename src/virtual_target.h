/********************************** (C) COPYRIGHT *******************************
 * File Name          : virtual_target.h
 * Description        : Virtual SWD target for protocol bring-up.
 *
 *  While DAP_VIRTUAL_TARGET is defined, SWD transfers do NOT drive the real
 *  SWDIO/SWCLK pins. Instead a simulated ARM ADIv5 target answers:
 *    - SW-DP   : IDCODE 0x2BA01477, CTRL/STAT power-up handshake, SELECT
 *    - AHB-AP  : IDR 0x24770011, Base 0xE00FF003, CSW/TAR/DRW
 *    - memory  : Cortex-M4 ROM table @0xE00FF000, CPUID @0xE000ED00,
 *                DHCSR halt state machine, everything else reads 0x00000000
 *
 *  This lets the host side (openocd / pyOCD / PSoC Programmer) run the full
 *  protocol stack against the probe. Real pin signalling will be validated
 *  with a logic analyzer later (README hardware feature table).
*******************************************************************************/
#ifndef __VIRTUAL_TARGET_H
#define __VIRTUAL_TARGET_H

#include <stdint.h>

uint8_t Virtual_SWD_Transfer(uint32_t request, uint32_t *data);
void    Virtual_SWJ_Sequence(uint32_t count, const uint8_t *data);
void    Virtual_SWD_Sequence(uint32_t info, const uint8_t *swdo, uint8_t *swdi);

#endif /* __VIRTUAL_TARGET_H */
