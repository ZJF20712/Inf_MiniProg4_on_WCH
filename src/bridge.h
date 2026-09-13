#ifndef __BRIDGE_H
#define __BRIDGE_H

#include <stdint.h>

/* Bridge protocol command IDs (KitProg3 bridgesInterface.h) */
#define CMD_ID_SET_GET_INT_SPEED    0x86u   /* I2C/SPI speed set/get */
#define CMD_ID_RESTART_I2C_MSTR     0x87u
#define CMD_ID_I2C_TRANSACTION      0x88u
#define CMD_ID_SPI_DATA_TRANSFER    0x89u

#define PROTOCOL_I2C                0x00u
#define PROTOCOL_SPI                0x01u
#define PROTOCOL_SET                0x00u
#define PROTOCOL_GET                0x01u

#define CMD_STAT_SUCCESS            0x00u
#define CMD_STAT_FAIL_INV_PAR       0x81u
#define CMD_STAT_FAIL_OP_FAIL       0x82u

void Bridge_EndpointOut(void);      /* EP7 OUT CTR callback */
void Bridge_Process(void);          /* main loop */

/* KHPI 0x94 handler */
void Bridge_OnOff(const uint8_t *request, uint8_t *response);

#endif /* __BRIDGE_H */
