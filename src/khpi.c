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

extern void Delay_Ms(uint32_t n);

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

/* ------------------------------------------------------------------ *
 * Per-family acquire INIT sequences (ported from official KitProg3
 * swd.c swdWriteBlockDict + per-DUT SwdAcquire* routines).
 * req = DAP_TRANSFER flags: bit0 APnDP, bit1 RnW, bit2 A2, bit3 A3
 * ------------------------------------------------------------------ */
typedef struct { uint8_t req; uint32_t val; } acq_w_t;
typedef struct {
    uint8_t hs;             /* 0 = line reset (+J2S), 1 = dormant wake */
    acq_w_t w[5];
    uint8_t n;              /* number of valid writes (2..5) */
} acq_family_t;

/* dormant-to-SWD selection alert (official swd.c SwitchDormantToSwd) */
static const uint8_t dormantWake[22] = {
    0xFF,                                   /* >=8 SWCLK with SWDIO high   */
    0x92, 0xF3, 0x09, 0x62,                 /* 128-bit selection alert     */
    0x95, 0x2D, 0x85, 0x86,
    0xE9, 0xAF, 0xDD, 0xE3,
    0xA2, 0x0E, 0xBC, 0x19,
    0xA0, 0xF1,                             /* 4 low + activation 0x1A + hi */
    0xFF, 0xFF, 0xFF, 0xFF                  /* >=50 more high (tail)       */
};

static const acq_family_t acqFamilies[6] = {
    /* [0] PSoC4 / CCGx: CTRL 0x54000000, TMR @ 0x40030014 */
    { 0, { {0x04,0x54000000u}, {0x08,0x00000000u}, {0x01,0x00000002u},
           {0x05,0x40030014u}, {0x0D,0x80000000u} }, 5 },
    /* [1] PSoC5: TMR @ 0x40050210, unlock 0xEA7E30A9 */
    { 0, { {0x05,0x40050210u}, {0x0D,0xEA7E30A9u} }, 2 },
    /* [2] PSoC6-BLE: CTRL 0x50000000, TMR @ 0x40260100 */
    { 1, { {0x04,0x50000000u}, {0x08,0x00000000u}, {0x01,0x00000002u},
           {0x05,0x40260100u}, {0x0D,0x80000000u} }, 5 },
    /* [3] TVII: TMR @ 0x40261100 */
    { 1, { {0x04,0x50000000u}, {0x08,0x00000000u}, {0x01,0x00000002u},
           {0x05,0x40261100u}, {0x0D,0x80000000u} }, 5 },
    /* [4] CYW20829: CSW_PROTECT 0x23000052, TMR @ 0x40200400 */
    { 1, { {0x04,0x50000000u}, {0x08,0x00000000u}, {0x01,0x23000052u},
           {0x05,0x40200400u}, {0x0D,0x80000000u} }, 5 },
    /* [5] PSoC3: TMR @ 0x00050210, unlock 0xEA7E30A9 */
    { 1, { {0x05,0x00050210u}, {0x0D,0xEA7E30A9u} }, 2 },
};

/* One acquire attempt on the CURRENT pin mapping (g_swdSwap): XRES pulse ->
 * handshake per family -> IDCODE -> family INIT incl. TMR unlock. dut:
 * 0=PSoC4/CCGx, 1=PSoC5, 2=PSoC6-BLE, 3=TVII, 4=CYW20829, 5=PSoC3 */
static uint8_t SWD_AcquireOnce(uint8_t dut)
{
    extern volatile uint32_t SysTick_ms;
    uint32_t idr = 0;
    uint32_t t0 = 0;
    uint8_t ack;
    static const uint8_t lineResetBits[7] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x03};

    uint8_t famIdx = (dut < 6u) ? dut : 0u;     /* unknown DUT -> PSoC4 path */
    const acq_family_t *fam = &acqFamilies[famIdx];

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
     * family INIT NOW, with NO prints in between (each print costs ~ms) */
    uint8_t a[5] = {0}; uint32_t rbVal;
    for (uint8_t k = 0; k < fam->n; k++)
        a[k] = SWD_Transfer(fam->w[k].req, (uint32_t *)&fam->w[k].val);

    SWJ_Sequence(8, (const uint8_t *)"\x00");           /* idle */

    rbVal = 0u; ack = SWD_Transfer(0x0Eu, &rbVal);      /* DP_R_RDBUFF    */

    /* verify: one more IDCODE read */
    uint8_t ack2;
    idr = 0u;
    ack2 = SWD_Transfer(DAP_TRANSFER_RnW, &idr);
    SWJ_Sequence(2, (const uint8_t *)"\x00");

    Debug_Print("[SWD] DUT d%u idr=%08X init:%u,%u,%u,%u,%u rb:%u=%08X re-idr ack=%u %08X\r\n",
                dut, idr, a[0], a[1], a[2], a[3], a[4], ack, rbVal, ack2, idr);

    return ((ack2 == DAP_TRANSFER_OK) && (idr != 0u) && (idr != 0xFFFFFFFFu)) ? 1u : 0u;
}

/* Acquire driver: try normal pin mapping, then swapped; keep the working one */
static uint8_t SWD_AcquireTarget(uint8_t dut)
{
    PORT_SWD_SETUP();
    DAP_Data.debug_port = DAP_PORT_SWD;

    Debug_Print("[SWD] idle: swdio=%u nreset=%u clock_delay=%u\r\n",
                PIN_SWDIO_IN(), PIN_nRESET_IN(), DAP_Data.clock_delay);

    for (uint8_t phase = 0; phase < 2; phase++)
    {
        g_swdSwap = phase;
        Debug_Print("[SWD] --- phase swap=%u ---\r\n", phase);
        if (SWD_AcquireOnce(dut))
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

/* Virtual bootloader mode: re-enumerate as PID 0xF146 so fw-loader / the
 * host sees a device sitting in bootloader mode. Firmware images delivered
 * to that interface are received and discarded (no real upgrade path). */

#define BOOT_FLAG_ADDR      0x20001800u     /* SRAM gap: above .bss, below stack */
#define BOOT_FLAG_MARKER    0xB0070000u

/* persist the target PID across the system reset (SRAM survives a warm
 * reset); startup applies it to the device descriptor before USB init */
static void PatchPid(uint16_t pid)
{
    /* flag word: 0xB0070000 marker | target PID in the low half */
    *(volatile uint32_t *)BOOT_FLAG_ADDR = 0xB0070000u | pid;
    HandleReset();                              /* warm reset, SRAM kept */
}

static void HandleModeSwitch(const uint8_t *request, uint8_t *response)
{
    switch (request[1])
    {
    case MODE_BOOTLOADER:
        /* virtual bootloader: persist flag, reset, boot as PID 0xF146.
         * firmware delivered to that interface is received and discarded. */
        response[1] = KHPI_STAT_SUCCESS;
        PatchPid(0xF146u);                      /* sets flag + resets */
        break;

    case MODE_CMSIS_DAP2X:
    case MODE_CMSIS_DAP1X:
    case MODE_CMSIS_DAP2X_2UART:
        /* Single-firmware implementation: normal PID, clean re-enum */
        response[1] = KHPI_STAT_SUCCESS;
        PatchPid(DAP_FW_V1 ? 0xF152u : 0xF151u);/* sets flag + resets */
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
        acquired = SWD_AcquireTarget(dut);
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
