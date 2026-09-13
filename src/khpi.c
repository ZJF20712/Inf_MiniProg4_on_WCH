/********************************** (C) COPYRIGHT *******************************
 * File Name          : khpi.c
 * Description        : KHPI vendor command implementation (KitProg3/MiniProg4
 *                      compatible). Response layouts follow the official
 *                      Infineon KitProg3 firmware DAP_vendor.c v2.06.
 *
 *  All commands ride the CMSIS-DAP command channel (interface 0), so they
 *  work identically in v2 bulk and v1 HID transports.
 *
 *  request[0] = vendor command (0x80 + n)
 *  request[1] = sub-command / parameter
 *  response[0] = echoed command, response[1] = status, response[2] = result
*******************************************************************************/
#include <string.h>
#include "khpi.h"
#include "led.h"
#include "bridge.h"
#include "vcom_serial.h"

#include "DAP.h"        /* DAP_TransferAbort, DAP_Data, ID_DAP_Invalid */
#include "DAP_config.h" /* PORT_SWD_SETUP, pin control                 */
#include "debug.h"

/* Acquire state */
static uint16_t customAcquireTimeout = 0;   /* in 0.8ms ticks, 0 = default */
static uint8_t  acquireDapHandshake = 0;
static uint8_t  acquireApSelect = 0;

/* runtime SWD pin-role swap state (0 = PA1 clk / PA0 dio, 1 = swapped) */
uint8_t g_swdSwap = 0;

static void SwdRestoreResetPin(void);
static void SwdRestoreResetPin(void)
{
    GPIO_InitTypeDef gi;
    extern void Delay_Ms(uint32_t n);
    gi.GPIO_Pin   = nRESET_PIN;
    gi.GPIO_Mode  = GPIO_Mode_Out_OD;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(nRESET_PORT, &gi);
    GPIO_SetBits(nRESET_PORT, nRESET_PIN);
    Delay_Ms(1);
}

/* One acquire attempt on the CURRENT pin mapping (g_swdSwap): XRES pulse ->
 * immediate line reset + IDCODE -> official Cypress PSoC4 INIT incl. TMR unlock. */
static uint8_t SWD_AcquireOnce(void)
{
    extern volatile uint32_t SysTick_ms;
    uint32_t idr = 0;
    uint32_t t0 = 0;
    uint8_t ack;
    static const uint8_t lineResetBits[7] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x03};

    {
        extern void Delay_Ms(uint32_t n);
        GPIO_InitTypeDef gi;
        gi.GPIO_Pin   = nRESET_PIN;
        gi.GPIO_Mode  = GPIO_Mode_Out_PP;
        gi.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(nRESET_PORT, &gi);
        GPIO_ResetBits(nRESET_PORT, nRESET_PIN);   /* assert reset (official 400us) */
        Delay_Ms(1);
        GPIO_SetBits(nRESET_PORT, nRESET_PIN);     /* release -> SWD window opens NOW */

        /* run at the official 1 MHz during the acquire: the CCG5 DAP window
         * after reset is only ~3 ms, and the INIT must finish inside it */
        DAP_Data.clock_delay = 24U;

        /* official PSoC4 acquire hammers handshake+IDCODE for ~2.5ms after
         * reset; the CCG5 DAP answers around 3 ms, cover 12ms to be safe */
        t0 = SysTick_ms;
        while ((SysTick_ms - t0) < 12u)
        {
            /* handshake: line reset + JTAG-to-SWD + line reset */
            SWJ_Sequence(51 + 1, lineResetBits);
            {
                static const uint8_t jtagToSwd[2] = {0x9Eu, 0xE7u};
                SWJ_Sequence(16, jtagToSwd);
                SWJ_Sequence(51 + 1, lineResetBits);
            }
            /* official sends 20 idle cycles (SWDIO low) before the header */
            SWJ_Sequence(20, (const uint8_t *)"\x00\x00");

            ack = SWD_Transfer(DAP_TRANSFER_RnW, &idr);
            if ((ack == DAP_TRANSFER_OK) && (idr != 0u) && (idr != 0xFFFFFFFFu))
            {
                goto acquired;
            }
        }
        Debug_Print("[SWD] window miss: last ack=%u idr=%08X\r\n", ack, idr);
        SWJ_Sequence(2, (const uint8_t *)"\x00");
        return 0u;
    }

acquired:;

    /* the window closes immediately after the first response - run the
     * INIT NOW, with NO prints in between (each print costs ~ms of UART) */
    uint8_t aCtrl, aSel, aCsw, aTar, aDrw, aRb;
    uint32_t rbVal;
    {
        uint32_t v;
        v = 0x54000000u; aCtrl = SWD_Transfer(0x04u, &v);   /* DP_W_CTRL_STAT */
        v = 0x00000000u; aSel  = SWD_Transfer(0x08u, &v);   /* DP_W_SELECT    */
        v = 0x00000002u; aCsw  = SWD_Transfer(0x01u, &v);   /* AP_W_CSW 32b   */
        v = 0x40030014u; aTar  = SWD_Transfer(0x05u, &v);   /* AP_W_TAR = TMR */
        v = 0x80000000u; aDrw  = SWD_Transfer(0x0Du, &v);   /* AP_W_DRW = TMR on */

        SWJ_Sequence(8, (const uint8_t *)"\x00");           /* idle */

        rbVal = 0u; aRb = SWD_Transfer(0x0Eu, &rbVal);      /* DP_R_RDBUFF    */
    }

    /* verify: one more IDCODE read */
    uint8_t ack2;
    idr = 0u;
    ack2 = SWD_Transfer(DAP_TRANSFER_RnW, &idr);
    SWJ_Sequence(2, (const uint8_t *)"\x00");

    Debug_Print("[SWD] IDCODE idr=%08X (t=%ums) init:%u,%u,%u,%u,%u rb:%u=%08X re-idr ack=%u %08X\r\n",
                idr, SysTick_ms - t0, aCtrl, aSel, aCsw, aTar, aDrw, aRb, rbVal, ack2, idr);

    return ((ack2 == DAP_TRANSFER_OK) && (idr != 0u) && (idr != 0xFFFFFFFFu)) ? 1u : 0u;
}

/* Acquire driver: try normal pin mapping, then swapped; keep the working one */
static uint8_t SWD_AcquireTarget(void)
{
    PORT_SWD_SETUP();
    DAP_Data.debug_port = DAP_PORT_SWD;

    Debug_Print("[SWD] idle: swdio=%u nreset=%u clock_delay=%u\r\n",
                PIN_SWDIO_IN(), PIN_nRESET_IN(), DAP_Data.clock_delay);

    for (uint8_t phase = 0; phase < 2; phase++)
    {
        g_swdSwap = phase;
        Debug_Print("[SWD] --- phase swap=%u ---\r\n", phase);
        if (SWD_AcquireOnce())
        {
            SwdRestoreResetPin();
            Debug_Print("[SWD] acquired with swap=%u\r\n", phase);
            return 1u;   /* keep working mapping for later transfers */
        }
    }

    g_swdSwap = 0u;
    SwdRestoreResetPin();
    return 0u;
}

/* UID record returned by 0x92 (layout: KitProg3 EEPROM unique-ID record) */
static const uint8_t khpiUidRecord[] = {
    0x00,                               /* [0] record valid flag */
    'W','C','H','-','M','P','4',' ',    /* [1..16]  board / custom name */
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,                               /* [17] checksum */
    0x00,0x00,                          /* [18..19] programming opt */
};

/* response offsets for 0x80 Get Info */
enum {
    V0R_CMD_STATUS = 1, V0R_MAJOR_VERSION, V0R_BLANK, V0R_MINOR_VERSION,
    V0R_BLANK_2, V0R_HWID, V0R_BLANK_3, V0R_PROTOCOL_MAJOR_VERSION,
    V0R_PROTOCOL_MINOR_VERSION, V0R_BUILD_LO, V0R_BUILD_HI
};

/* response offsets for 0x90 capabilities */
enum {
    V16R_CMD_STATUS = 1, V16R_SUPPORTED_INTERFACES, V16R_UART_LED,
    V16R_I2C_CLOCK, V16R_MIN_SPI_BYTE1, V16R_MIN_SPI_BYTE2,
    V16R_MIN_SPI_BYTE3, V16R_MIN_SPI_BYTE4, V16R_MAX_SPI_BYTE1,
    V16R_MAX_SPI_BYTE2, V16R_MAX_SPI_BYTE3, V16R_MAX_SPI_BYTE4,
    V16R_SPI_SS, V16R_VOLTAGES
};

/* response offsets for 0x84 get power */
enum {
    V4R_CMD_STATUS = 1, V4R_POWER_SUPPLY, V4R_VTARG_BYTE1, V4R_VTARG_BYTE2,
    V4R_POT_BYTE1, V4R_POT_BYTE2, V4R_POT_STATUS
};

/* request offsets for 0x84 set power */
enum { V4R_SUBCOMMAND = 1, V4R_POWERMODE, V4R_VOLTAGE_BYTE1, V4R_VOLTAGE_BYTE2 };

static void HandleReset(void)
{
    NVIC_SystemReset();
}

static void HandleModeSwitch(const uint8_t *request, uint8_t *response)
{
    switch (request[1])
    {
    case MODE_BOOTLOADER:
        /* No bootloader on this device: reset into the regular firmware */
        HandleReset();
        break;

    case MODE_CMSIS_DAP2X:
    case MODE_CMSIS_DAP1X:
    case MODE_CMSIS_DAP2X_2UART:
        /* Single-firmware implementation: acknowledge and reset so the
         * host re-enumerates. Mode is always bulk+bridge+CDC. */
        response[1] = KHPI_STAT_SUCCESS;
        HandleReset();
        break;

    case MODE_CUSTOM_APP:
        response[1] = KHPI_STAT_SUCCESS;
        HandleReset();
        break;

    default:
        response[1] = KHPI_STAT_INV_PAR;
        break;
    }
}

static uint32_t HandleLedCmd(const uint8_t *request, uint8_t *response)
{
    Led_SetState(request[1]);
    response[1] = KHPI_STAT_SUCCESS;
    return 2;   /* request 2 bytes, response 2 bytes */
}

static uint32_t GetSetPower(const uint8_t *request, uint8_t *response)
{
#if KHPI_HAS_POWER_CONTROL
    /* Virtual power until the regulator hardware is wired: always report
     * supply on with VTARG present at 3.3 V so host-side power-cycle
     * acquire flows see a healthy target rail. */
    if (request[1] == CMD_POWER_SET)
    {
        response[1] = KHPI_STAT_SUCCESS;
        return 2;
    }
    else if (request[1] == CMD_POWER_GET)
    {
        uint16_t vtarg_mV = 3300;
        response[V4R_CMD_STATUS]   = KHPI_STAT_SUCCESS;
        response[V4R_POWER_SUPPLY] = 1;         /* power switch on       */
        response[V4R_VTARG_BYTE1]  = (uint8_t)(vtarg_mV);
        response[V4R_VTARG_BYTE2]  = (uint8_t)(vtarg_mV >> 8);
        response[V4R_POT_BYTE1]    = 0;         /* no requested voltage  */
        response[V4R_POT_BYTE2]    = 0;
        response[V4R_POT_STATUS]   = 0;         /* no programmable supply */
        return 8;
    }
    else if (request[1] == CMD_POWER_VOLT_SET)
    {
        response[1] = KHPI_STAT_SUCCESS;
        return 2;
    }
    response[1] = KHPI_STAT_INV_PAR;
    return 2;
#else
    if (request[1] == CMD_POWER_SET)
    {
        /* No power control hardware: report operation failure */
        response[1] = KHPI_STAT_OP_FAIL;
        response[2] = KHPI_STAT_INV_PAR;
        return 3;
    }
    else if (request[1] == CMD_POWER_GET)
    {
        response[V4R_CMD_STATUS]   = KHPI_STAT_SUCCESS;
        response[V4R_POWER_SUPPLY] = 0;         /* power switch off */
        response[V4R_VTARG_BYTE1]  = 0;         /* VTARG = 0 mV     */
        response[V4R_VTARG_BYTE2]  = 0;
        response[V4R_POT_BYTE1]    = 0;         /* no requested voltage */
        response[V4R_POT_BYTE2]    = 0;
        response[V4R_POT_STATUS]   = 0;         /* no programmable supply */
        return 8;
    }
    response[1] = KHPI_STAT_INV_PAR;
    return 2;
#endif
}

static uint32_t HandleAcquire(const uint8_t *request, uint8_t *response)
{
    /* request: [0]=0x85 [1]=mode [2]=DUT [3]=retries ([4..]=custom sequence) */
    uint8_t acquireMode = request[1];
    uint8_t dut         = request[2];
    uint8_t retries     = request[3];
    uint8_t acquired    = 0;

    (void)acquireMode;
    (void)dut;

    while (retries-- && !acquired)
    {
        extern void Delay_Ms(uint32_t n);
        Delay_Ms(150);   /* pacing: repeatable pattern for logic-analyzer capture */
        acquired = SWD_AcquireTarget();
    }

    response[1] = KHPI_STAT_SUCCESS;
    response[2] = acquired ? 1u : 0u;
    return 3;
}

static uint32_t GetCapabilities(const uint8_t *request, uint8_t *response)
{
    (void)request;

    response[V16R_CMD_STATUS] = KHPI_STAT_SUCCESS;
    response[V16R_SUPPORTED_INTERFACES] =
            (KHPI_HAS_I2C_BRIDGE    ? I2C_AVAILABILITY_MASK    : 0) |
            (KHPI_HAS_SPI_BRIDGE    ? SPI_AVAILIBILITY_MASK    : 0) |
            DAPH_AVAILIBILITY_MASK | DAPB_AVAILIBILITY_MASK |
            (KHPI_HAS_POWER_CONTROL ? ON_OFF_SW_AVAILIBILITY_MASK : 0) |
            (KHPI_HAS_VOLT_MEAS     ? VMEAS_AVAILIBILITY_MASK  : 0) |
            (KHPI_HAS_GPIO_BRIDGE   ? GPIO_AVAILIBILITY_MASK   : 0);

    /* LED byte + UART byte (KitProg3 packs both into one field) */
    response[V16R_UART_LED]    = THREE_LED_KIT_MASK | ONE_UART_MASK;
    response[V16R_I2C_CLOCK]   = I2C_SPEEDS_MASK;   /* 50k/100k/400k/1M   */
    response[V16R_MIN_SPI_BYTE1] = (uint8_t)(SPI_MIN_HZ);
    response[V16R_MIN_SPI_BYTE2] = (uint8_t)(SPI_MIN_HZ >> 8);
    response[V16R_MIN_SPI_BYTE3] = (uint8_t)(SPI_MIN_HZ >> 16);
    response[V16R_MIN_SPI_BYTE4] = (uint8_t)(SPI_MIN_HZ >> 24);
    response[V16R_MAX_SPI_BYTE1] = (uint8_t)(SPI_MAX_HZ);
    response[V16R_MAX_SPI_BYTE2] = (uint8_t)(SPI_MAX_HZ >> 8);
    response[V16R_MAX_SPI_BYTE3] = (uint8_t)(SPI_MAX_HZ >> 16);
    response[V16R_MAX_SPI_BYTE4] = (uint8_t)(SPI_MAX_HZ >> 24);
    response[V16R_SPI_SS]      = SPI_SS_LINES_MASK;
    response[V16R_VOLTAGES]    = VOLT_SUPPORT_MASK; /* 1.8/2.5/3.3/5.0 V  */

    return PROBE_CAP_RESP_LEN;
}

static uint32_t SetAcquireOption(const uint8_t *request, uint8_t *response)
{
    switch (request[1])
    {
    case SET_ACQUIRE_TIMEOUT:   /* seconds, capped at 30 like KitProg3 */
    {
        uint8_t t = request[2];
        if (t > 30u) t = 30u;
        customAcquireTimeout = (uint16_t)t * 1000u / 4u;  /* 0.8ms ticks */
        response[1] = KHPI_STAT_SUCCESS;
        break;
    }
    case SET_ACQUIRE_HANDSHAKE:
        acquireDapHandshake = request[2];
        response[1] = KHPI_STAT_SUCCESS;
        break;
    case SET_ACQUIRE_AP:
        acquireApSelect = request[2];
        response[1] = KHPI_STAT_SUCCESS;
        break;
    default:
        response[1] = KHPI_STAT_INV_PAR;
        break;
    }
    (void)acquireDapHandshake;
    (void)acquireApSelect;
    return 2;
}

static uint32_t GetUidData(const uint8_t *request, uint8_t *response)
{
    (void)request;
    response[1] = KHPI_STAT_SUCCESS;
    memcpy(&response[2], khpiUidRecord, sizeof(khpiUidRecord));
    return 2 + sizeof(khpiUidRecord);
}

static uint32_t GetSetUartConfig(const uint8_t *request, uint8_t *response)
{
    /* request: [1]=get/set, [2]=port, [3]=value  (KitProg3 layout) */
    switch (request[1])
    {
    case 0:     /* get flow control */
        response[1] = KHPI_STAT_SUCCESS;
        response[2] = Vcom.flowControl;
        return 3;
    case 1:     /* set flow control */
        Vcom.flowControl = request[3];
        response[1] = KHPI_STAT_SUCCESS;
        return 2;
    case 2:     /* get UART mode */
        response[1] = KHPI_STAT_SUCCESS;
        response[2] = Vcom.uartMode;
        return 3;
    case 3:     /* set UART mode */
        Vcom.uartMode = request[3];
        response[1] = KHPI_STAT_SUCCESS;
        return 2;
    default:
        response[1] = KHPI_STAT_INV_PAR;
        return 2;
    }
}

uint32_t DAP_ProcessVendorCommand(const uint8_t *request, uint8_t *response)
{
    uint32_t num = 1;   /* default: echo command byte only */

    response[0] = request[0];
    Debug_Print("[KHPI] cmd=%02X sub=%02X\r\n", request[0], request[1]);

    switch (request[0])
    {
    case ID_DAP_Vendor0:                    /* 0x80: Get Info */
        response[V0R_CMD_STATUS] = KHPI_STAT_SUCCESS;
        response[V0R_MAJOR_VERSION] = FW_VER_MAJOR;
        response[V0R_BLANK] = 0;
        response[V0R_MINOR_VERSION] = FW_VER_MINOR;
        response[V0R_BLANK_2] = 0;
        response[V0R_HWID] = KHPI_HWID;
        response[V0R_BLANK_3] = 0;
        response[V0R_PROTOCOL_MAJOR_VERSION] = KHPI_VER_MAJOR;
        response[V0R_PROTOCOL_MINOR_VERSION] = KHPI_VER_MINOR;
        response[V0R_BUILD_LO] = (uint8_t)(FW_BUILD_NUMBER & 0xFF);
        response[V0R_BUILD_HI] = (uint8_t)(FW_BUILD_NUMBER >> 8);
        num = 12;
        break;

    case ID_DAP_Vendor1:                    /* 0x81: Reset */
        HandleReset();
        break;

    case ID_DAP_Vendor2:                    /* 0x82: Mode switch */
        HandleModeSwitch(request, response);
        num = 2;
        break;

    case ID_DAP_Vendor3:                    /* 0x83: LED control */
        num = HandleLedCmd(request, response);
        break;

    case ID_DAP_Vendor4:                    /* 0x84: Get/Set power */
        num = GetSetPower(request, response);
        break;

    case ID_DAP_Vendor5:                    /* 0x85: SWD acquire */
        if (DAP_Data.debug_port == DAP_PORT_SWD)
        {
            num = HandleAcquire(request, response);
        }
        else
        {
            response[0] = ID_DAP_Invalid;
            num = 1;
        }
        break;

    case ID_DAP_Vendor10:                   /* 0x8A: GPIO set mode  */
    case ID_DAP_Vendor11:                   /* 0x8B: GPIO set state */
    case ID_DAP_Vendor12:                   /* 0x8C: GPIO read      */
    case ID_DAP_Vendor13:                   /* 0x8D: GPIO changed   */
#if KHPI_HAS_GPIO_BRIDGE
        num = Bridge_GpioCommand(request, response);
#else
        response[0] = ID_DAP_Invalid;
        num = 1;
#endif
        break;

    case ID_DAP_Vendor16:                   /* 0x90: capabilities */
        num = GetCapabilities(request, response);
        break;

    case ID_DAP_Vendor17:                   /* 0x91: acquire option */
        num = SetAcquireOption(request, response);
        break;

    case ID_DAP_Vendor18:                   /* 0x92: UID data */
        num = GetUidData(request, response);
        break;

    case ID_DAP_Vendor19:                   /* 0x93: UART config */
        num = GetSetUartConfig(request, response);
        break;

    case ID_DAP_Vendor20:                   /* 0x94: Bridge On/Off */
        Bridge_OnOff(request, response);
        num = 2;
        break;

    case ID_DAP_Vendor21:                   /* 0x95: reset-line visibility test */
    {
        /* Pulse nRESET 8x at ~1.25Hz so a human can SEE the target reset
         * (LED blink / USB re-enumeration). Confirms XRES wiring. */
        extern void Delay_Ms(uint32_t n);
        GPIO_InitTypeDef gi;
        gi.GPIO_Pin   = nRESET_PIN;
        gi.GPIO_Mode  = GPIO_Mode_Out_PP;
        gi.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(nRESET_PORT, &gi);
        for (int i = 0; i < 8; i++)
        {
            GPIO_ResetBits(nRESET_PORT, nRESET_PIN);
            Delay_Ms(400);
            GPIO_SetBits(nRESET_PORT, nRESET_PIN);
            Delay_Ms(400);
        }
        gi.GPIO_Mode = GPIO_Mode_Out_OD;
        GPIO_Init(nRESET_PORT, &gi);
        GPIO_SetBits(nRESET_PORT, nRESET_PIN);
        response[1] = KHPI_STAT_SUCCESS;
        num = 2;
        break;
    }

    default:
        response[0] = ID_DAP_Invalid;
        num = 1;
        break;
    }

    return num;
}
