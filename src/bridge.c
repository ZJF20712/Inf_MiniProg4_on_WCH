/********************************** (C) COPYRIGHT *******************************
 * File Name          : bridge.c
 * Description        : USB-I2C/SPI bridge protocol (interface 1, EP7 OUT ->
 *                      EP6 IN), KitProg3 bridge-protocol compatible command
 *                      framing. The protocol/set/get layer is complete; the
 *                      actual I2C/SPI bus transactions need hardware support
 *                      (see README feature table) and currently report
 *                      CMD_STAT_FAIL_OP_FAIL.
 *
 *  Command framing (request[0]):
 *   0x86 [iface][set/get][speed u32 LE]  -> [0x86][status][speed u32 on GET]
 *   0x87 [iface]                         -> [0x87][status]
 *   0x88 [flags][len][addr][data...]     -> [0x88][status][data...] (I2C)
 *   0x89 [len][ss][ctrl][data...]        -> [0x89][status][data...] (SPI)
 *   0x8A-0x8D GPIO commands              -> via DAP channel (KHPI)
*******************************************************************************/
#include "usb_lib.h"
#include "usb_regs.h"
#include "usb_sil.h"
#include "bridge.h"
#include "khpi.h"
#include "i2c_hw.h"

volatile uint8_t bridgeEnabled = 0;

static uint8_t  bridgeReq[64];
static volatile uint8_t bridgeReqReady = 0;
static uint32_t i2cSpeed = 0;
static uint32_t spiSpeed = 0;

void Bridge_EndpointOut(void)
{
    USB_SIL_Read(EP7_OUT, bridgeReq);
    SetEPRxStatus(ENDP7, EP_RX_VALID);
    bridgeReqReady = 1;
}

static uint32_t Bridge_SendResponse(const uint8_t *resp, uint32_t len)
{
    if (GetEPTxStatus(ENDP6) != EP_TX_NAK)
        return 0;

    USB_SIL_Write(EP6_IN, (uint8_t *)resp, len);
    SetEPTxValid(ENDP6);
    return 1;
}

void Bridge_OnOff(const uint8_t *request, uint8_t *response)
{
    if (request[1] == 0x00u)        /* BRIDGE_ENABLE  */
    {
        I2C_HW_Init();              /* I2C1 on PB6/PB7, 100k default */
        if (i2cSpeed)
            I2C_HW_SetSpeed(i2cSpeed);
        bridgeEnabled = 1;
        response[1] = CMD_STAT_SUCCESS;
    }
    else if (request[1] == 0x01u)   /* BRIDGE_DISABLE */
    {
        bridgeEnabled = 0;
        response[1] = CMD_STAT_SUCCESS;
    }
    else
    {
        response[1] = CMD_STAT_FAIL_INV_PAR;
    }
}

void Bridge_Process(void)
{
    uint8_t resp[64];

    if (!bridgeReqReady)
        return;

    if (!bridgeEnabled)
    {
        /* bridge turned off: only wake the endpoint, drop the packet */
        bridgeReqReady = 0;
        return;
    }

    resp[0] = bridgeReq[0];

    switch (bridgeReq[0])
    {
    case CMD_ID_SET_GET_INT_SPEED:
        if (bridgeReq[2] == PROTOCOL_GET)
        {
            uint32_t speed = (bridgeReq[1] == PROTOCOL_SPI) ? spiSpeed : i2cSpeed;
            resp[1] = CMD_STAT_SUCCESS;
            resp[2] = (uint8_t)(speed);
            resp[3] = (uint8_t)(speed >> 8);
            resp[4] = (uint8_t)(speed >> 16);
            resp[5] = (uint8_t)(speed >> 24);
            Bridge_SendResponse(resp, 6);
        }
        else if (bridgeReq[2] == PROTOCOL_SET)
        {
            uint32_t speed = (uint32_t)bridgeReq[3] |
                             ((uint32_t)bridgeReq[4] << 8) |
                             ((uint32_t)bridgeReq[5] << 16) |
                             ((uint32_t)bridgeReq[6] << 24);
            if (bridgeReq[1] == PROTOCOL_SPI)
            {
                spiSpeed = speed;
            }
            else
            {
                i2cSpeed = speed;
                I2C_HW_SetSpeed(i2cSpeed);      /* live reconfigure (0 = 100k) */
            }
            resp[1] = CMD_STAT_SUCCESS;
            Bridge_SendResponse(resp, 2);
        }
        else
        {
            resp[1] = CMD_STAT_FAIL_INV_PAR;
            Bridge_SendResponse(resp, 2);
        }
        break;

    case CMD_ID_RESTART_I2C_MSTR:
        I2C_HW_Reset();                         /* bus recovery + re-init at set speed */
        if (i2cSpeed)
            I2C_HW_SetSpeed(i2cSpeed);
        resp[1] = CMD_STAT_SUCCESS;
        Bridge_SendResponse(resp, 2);
        break;

    case CMD_ID_I2C_TRANSACTION:
    {
        /* 0x88 [flags][len][addr][data...] -> [0x88][status][data...]
         * flags bits[6:4]: 1/3=write (3 = no stop), 2/4=read;
         * bit0 START, bit1 STOP, bit2 RESTART, bit3 READ */
        uint8_t flags = bridgeReq[1];
        uint8_t len   = bridgeReq[2];
        uint8_t addr7 = bridgeReq[3];
        uint8_t type  = (flags & 0x70u) >> 4;
        uint8_t isRead = (type == 2u || type == 4u || (flags & 0x08u)) ? 1u : 0u;
        uint8_t stop   = (flags & 0x02u) ? 1u : 0u;
        uint8_t addr7e = (uint8_t)(addr7 >> 1); /* accept 8-bit form too */

        if (isRead && len == 0u)
        {
            resp[1] = CMD_STAT_FAIL_INV_PAR;
            Bridge_SendResponse(resp, 2);
        }
        else if (isRead)
        {
            uint8_t st = I2C_HW_Read(addr7e, &resp[2], len, stop);
            resp[1] = st ? CMD_STAT_FAIL_OP_FAIL : CMD_STAT_SUCCESS;
            Bridge_SendResponse(resp, 2u + len);
        }
        else
        {
            uint8_t st = I2C_HW_Write(addr7e, &bridgeReq[4], len, stop);
            resp[1] = st ? CMD_STAT_FAIL_OP_FAIL : CMD_STAT_SUCCESS;
            Bridge_SendResponse(resp, 2);
        }
        break;
    }

    case CMD_ID_SPI_DATA_TRANSFER:
        /* SPI bridge needs SPI master wiring (hardware table) */
        resp[1] = CMD_STAT_FAIL_OP_FAIL;
        Bridge_SendResponse(resp, 2);
        break;

    default:
        resp[0] = 0xFF;
        resp[1] = CMD_STAT_FAIL_INV_PAR;
        Bridge_SendResponse(resp, 2);
        break;
    }

    bridgeReqReady = 0;
}
