/********************************** (C) COPYRIGHT *******************************
 * File Name          : virtual_target.c
 * Description        : Simulated ADIv5 target (SW-DP v2 + AHB-AP + Cortex-M4
 *                      ROM table) answering inside the DAP firmware, so the
 *                      complete host<->DAP protocol path can be verified
 *                      without real target hardware.
*******************************************************************************/
#include <string.h>
#include "virtual_target.h"
#include "debug.h"
#include "DAP.h"

/* ---------------- state ---------------- */
static uint32_t dpSelect;        /* DP SELECT (APSEL[31:24], APBANKSEL[7:4]) */
static uint32_t dpCtrlStat;      /* DP CTRL/STAT written value               */
static uint32_t dpRdBuff;        /* last read data                           */

static uint32_t apCSW = 0x40u;      /* DEVICEEN set so the AP reports enabled */
static uint32_t apTAR;
static uint8_t  targetHalted;
static uint8_t  debugEnabled;

/* AP register file: 16 banks x 4 regs (bank = APBANKSEL, reg = A[3:2]) */
static uint32_t apRegs[16][4];

/* ---------------- virtual memory ---------------- */
static uint32_t rom_table[16] = {   /* 0xE00FF000 .. 0xE00FF03C (entries) */
    0xFFF0F003,     /* entry0: SCS  @ 0xE000E000 */
    0xFFF02003,     /* entry1: DWT  @ 0xE0001000 */
    0xFFF03003,     /* entry2: FPB  @ 0xE0002000 */
    0xFFF04003,     /* entry3: ITM  @ 0xE0003000 */
};
static uint32_t rom_pid_cid[16] = { /* 0xE00FFFD0 .. 0xE00FFFFC */
    0x00000004,     /* PID4 */
    0x00000000,     /* PID5 */
    0x00000000,     /* PID6 */
    0x00000000,     /* PID7 */
    0x00000047,     /* PID0 (Cortex-M4) */
    0x000000B7,     /* PID1 */
    0x0000000B,     /* PID2 */
    0x00000000,     /* PID3 */
    0x0000000D,     /* CID0 */
    0x00000010,     /* CID1 */
    0x00000005,     /* CID2 */
    0x000000B1,     /* CID3 */
};

/* SCS (System Control Space, 0xE000E000) CoreSight ID registers.
 * pyOCD discovers the CPU core by walking the ROM table to the SCS entry and
 * validating its ID registers (PIDR=0x000BB747 -> Cortex-M4 SCS, CIDR class 0xE). */
static const struct { uint32_t addr; uint32_t val; } scs_ids[] = {
    /* PIDR values chosen so pyocd decodes:
     *   part = 0x000 (v7-M SCS -> CortexM.factory)
     *   designer = 0x43B (pyocd ARM_ID) via PID1[7:4]=B, PID2[2:0]=3, PID4[3:0]=4 */
    {0xE000EFD0u, 0x00000004u},     /* PID4: [3:0]=4 -> pyocd designer continuation */
    {0xE000EFE0u, 0x00000000u},     /* PID0: part[7:0] = 0x00 */
    {0xE000EFE4u, 0x000000B0u},     /* PID1: [3:0]=part[11:8]=0, [7:4]=designer[3:0]=0xB */
    {0xE000EFE8u, 0x0000000Bu},     /* PID2: [2:0]=designer[6:4]=3 */
    {0xE000EFEcu, 0x00000000u},     /* PID3 */
    {0xE000EFF0u, 0x0000000Du},     /* CID0 */
    {0xE000EFF4u, 0x000000E0u},     /* CID1: class 0xE = processor */
    {0xE000EFF8u, 0x00000005u},     /* CID2 */
    {0xE000EFFCu, 0x000000B1u},     /* CID3 */
};

/* Debug components referenced by the ROM table (DWT/FPB/ITM).
 * All are class 0x9 (CoreSight) with CIDR = 0xB105900D. */
static const struct { uint32_t addr; uint32_t val; } debug_ids[] = {
    /* DWT @ 0xE0001000 */
    {0xE0001FD0u, 0x00000004u},     /* PID4 */
    {0xE0001FE0u, 0x00000003u},     /* PID0 */
    {0xE0001FE4u, 0x000000B7u},     /* PID1 */
    {0xE0001FE8u, 0x0000000Bu},     /* PID2 */
    {0xE0001FECu, 0x00000000u},     /* PID3 */
    {0xE0001FF0u, 0x0000000Du},     /* CID0 */
    {0xE0001FF4u, 0x00000090u},     /* CID1: class 9 = CoreSight */
    {0xE0001FF8u, 0x00000005u},     /* CID2 */
    {0xE0001FFCu, 0x000000B1u},     /* CID3 */
    {0xE0001FCCu, 0x00000011u},     /* DEVTYPE: DWT */
    /* FPB @ 0xE0002000 */
    {0xE0002FD0u, 0x00000004u},     /* PID4 */
    {0xE0002FE0u, 0x00000002u},     /* PID0 */
    {0xE0002FE4u, 0x000000B7u},     /* PID1 */
    {0xE0002FE8u, 0x0000000Bu},     /* PID2 */
    {0xE0002FECu, 0x00000000u},     /* PID3 */
    {0xE0002FF0u, 0x0000000Du},     /* CID0 */
    {0xE0002FF4u, 0x00000090u},     /* CID1: class 9 */
    {0xE0002FF8u, 0x00000005u},     /* CID2 */
    {0xE0002FFCu, 0x000000B1u},     /* CID3 */
    {0xE0002FCCu, 0x00000002u},     /* DEVTYPE: FPB */
    /* ITM @ 0xE0003000 */
    {0xE0003FD0u, 0x00000004u},     /* PID4 */
    {0xE0003FE0u, 0x00000001u},     /* PID0 */
    {0xE0003FE4u, 0x000000B7u},     /* PID1 */
    {0xE0003FE8u, 0x0000000Bu},     /* PID2 */
    {0xE0003FECu, 0x00000000u},     /* PID3 */
    {0xE0003FF0u, 0x0000000Du},     /* CID0 */
    {0xE0003FF4u, 0x00000090u},     /* CID1: class 9 */
    {0xE0003FF8u, 0x00000005u},     /* CID2 */
    {0xE0003FFCu, 0x000000B1u},     /* CID3 */
    {0xE0003FCCu, 0x00000001u},     /* DEVTYPE: ITM */
};

/* Virtual core register file (for DCRSR/DCRDR access) */
static uint32_t dcrsr;           /* last DCRSR write: [16]=RnW, [4:0]=regsel */
static uint32_t dcrdr;           /* DCRDR data register (moved by DCRSR trigger) */
static uint32_t core_regs[32];   /* r0-r15, xpsr=16, msp=17, psp=18, ... */

/* Initialize core registers with plausible values (done on first access) */
static uint8_t regs_initialized = 0;
static void init_core_regs(void)
{
    if (regs_initialized) return;
    core_regs[13] = 0x20001000u;   /* MSP */
    core_regs[14] = 0xFFFFFFFFu;   /* LR */
    core_regs[15] = 0x00000110u;   /* PC: plausible reset vector */
    core_regs[16] = 0x01000000u;   /* xPSR: Thumb bit set */
    regs_initialized = 1;
}

static uint32_t virt_mem_read(uint32_t addr)
{
    init_core_regs();
    addr &= ~3u;

    if (addr >= 0xE00FF000u && addr < 0xE00FF040u)
        return rom_table[(addr - 0xE00FF000u) >> 2];

    if (addr >= 0xE00FFFD0u && addr <= 0xE00FFFFCu)
        return rom_pid_cid[(addr - 0xE00FFFD0u) >> 2];

    /* SCS ID register block */
    if (addr >= 0xE000EFD0u && addr <= 0xE000EFFCu)
    {
        for (unsigned i = 0; i < sizeof(scs_ids)/sizeof(scs_ids[0]); i++)
            if (scs_ids[i].addr == addr) return scs_ids[i].val;
        return 0u;
    }

    /* Debug component (DWT/FPB/ITM) ID register blocks */
    if ((addr >= 0xE0001FC0u && addr <= 0xE0001FFFu) ||
        (addr >= 0xE0002FC0u && addr <= 0xE0002FFFu) ||
        (addr >= 0xE0003FC0u && addr <= 0xE0003FFFu))
    {
        for (unsigned i = 0; i < sizeof(debug_ids)/sizeof(debug_ids[0]); i++)
            if (debug_ids[i].addr == addr) return debug_ids[i].val;
        return 0u;
    }

    switch (addr)
    {
    case 0xE000ED00u:   return 0x410FC241u;                 /* CPUID: CM4 r1p1 */
    case 0xE000EDF0u:   /* DHCSR */
    {
        uint32_t v = 0;
        if (debugEnabled) v |= 1u << 0;                     /* C_DEBUGEN */
        if (targetHalted) v |= 1u << 17;                    /* S_HALT    */
        v |= 1u << 16;                                      /* S_REGRDY: register transfer ready */
        v |= 1u << 24;                                      /* S_RESET_ST (sticky) */
        return v;
    }
    case 0xE000EDF4u:   return dcrsr;                       /* DCRSR readback */
    case 0xE000EDF8u:   return dcrdr;                       /* DCRDR (loaded by DCRSR read trigger) */
    case 0xE000EDFCu:   return 1u << 24;                    /* DEMCR: TRCENA readback */
    case 0xE000EF40u:   return 0;                           /* FPCR */
    default:            return 0x00000000u;
    }
}

static void virt_mem_write(uint32_t addr, uint32_t v)
{
    addr &= ~3u;

    switch (addr)
    {
    case 0xE000EDF0u:   /* DHCSR: C_DEBUGEN / C_HALT / C_DEBUGEN key */
        if ((v & 0xFFFF0000u) == 0xA05F0000u)  /* key required for writes */
        {
            if (v & (1u << 0)) debugEnabled = 1;
            if (v & (1u << 1)) targetHalted = 1;
            else               targetHalted = 0;  /* C_HALT cleared = resume */
            if (v & (1u << 2)) targetHalted = 0;  /* C_STEP */
        }
        break;
    case 0xE000EDF4u:   /* DCRSR: select register [4:0], RnW [16] */
        dcrsr = v;
        /* DCRSR write triggers the transfer (ARM SCS semantics):
         * host writes DCRDR first, then DCRSR to move the data. */
        {
            uint32_t regsel = dcrsr & 0x1Fu;
            if (regsel < 32)
            {
                if (dcrsr & (1u << 16)) core_regs[regsel] = dcrdr;  /* REGWNR=1: reg <- DCRDR */
                else                   dcrdr = core_regs[regsel];  /* REGWNR=0: DCRDR <- reg */
            }
        }
        break;
    case 0xE000EDF8u:   /* DCRDR: holds data only, no direct reg-file effect */
        dcrdr = v;
        break;
    default:
        break;          /* ROM table / everything else is read-only */
    }
}

/* ---------------- AP / DP register engine ---------------- */

static uint32_t ap_current_idr(void)
{
    /* only AP 0 exists (AHB-AP of a Cortex-M4) */
    if (((dpSelect >> 24) & 0xFFu) != 0u) return 0u;
    return 0x24770011u;
}

uint8_t Virtual_SWD_Transfer(uint32_t request, uint32_t *data)
{
    uint32_t apndp   =  request       & 1u;
    uint32_t rnw     = (request >> 1) & 1u;
    uint32_t a32     = (request >> 2) & 3u;         /* A[3:2] */
    uint32_t apsel   =  dpSelect >> 24;
    uint32_t bank    = (dpSelect >> 4) & 0xFu;
    uint32_t *reg    = 0;
    uint32_t val;

    if (apndp == 0u)
    {
        /* --- Debug Port ---
         * request bits[3:2] map to DP address bits[3:2]:
         *   0b00 = 0x0 (IDCODE R / ABORT W), 0b01 = 0x4 (CTRL/STAT R/W),
         *   0b10 = 0x8 (SELECT W),           0b11 = 0xC (RDBUFF R) */
        switch (a32)
        {
        case 0x0:   /* IDCODE (R) / ABORT (W) */
            if (rnw) val = 0x2BA01477u;             /* SW-DP v2 */
            else     { dpCtrlStat &= ~0x000000F0u; val = 0; }   /* clear sticky */
            break;
        case 0x1:   /* CTRL/STAT */
            if (rnw)
            {
                val = dpCtrlStat & ~((1u << 29) | (1u << 31));
                if (dpCtrlStat & (1u << 28)) val |= (1u << 29); /* CDBGPWRUPACK */
                if (dpCtrlStat & (1u << 30)) val |= (1u << 31); /* CSYSPWRUPACK */
            }
            else
            {
                dpCtrlStat = *data;
                val = 0;
            }
            break;
        case 0x2:   /* SELECT (W) */
            if (!rnw) { dpSelect = *data; val = 0; }
            else      val = dpSelect;
            break;
        case 0x3:   /* RDBUFF (R) */
            val = dpRdBuff;
            break;
        }

        /* Posted reads (data == NULL) still latch into RDBUFF */
        if (rnw) { dpRdBuff = val; if (data) *data = val; }
        return DAP_TRANSFER_OK;
    }

    /* --- Access Port (APSEL != 0 does not exist) --- */
    if (apsel != 0u)
    {
        if (rnw)
        {
            dpRdBuff = 0u;              /* reads from a missing AP latch zero */
            if (data) *data = 0u;
        }
        return DAP_TRANSFER_OK;
    }

    reg = &apRegs[bank][a32];   /* a32 = 0..3 register index within the bank */

    if (rnw)
    {
        uint32_t regAddr = (bank << 4) | (a32 << 2);
        uint32_t wireVal;

        /* AHB-AP read pipelining (matches real silicon, DAP.c depends on it):
         *  - A posted read (data == NULL) ARMS an access: consumes the TAR,
         *    latches the value into RDBUFF, and returns nothing on the wire.
         *  - A data-phase AP read (data != NULL) returns what the PREVIOUS
         *    armed access latched, then arms the next one.
         *  - DP_RDBUFF reads just return RDBUFF without arming anything.
         * DAP.c block sequence: post + (N-1) DRW reads + final RDBUFF read
         * then yields exactly words [0..N-1] on the wire. */
        if (regAddr == 0x0C)
        {
            /* DRW read - AHB-AP pipelined access:
             * The wire value of this read is the PREVIOUS armed access.
             * This read arms a new access at the current TAR. */
            uint32_t wireVal = dpRdBuff;          /* previous armed value */
            dpRdBuff = virt_mem_read(apTAR);       /* arm new access */
            /* CSW.AddrInc bits[5:4]: 0x10 = increment by SIZE (=4) */
            if ((apCSW & 0x30u) == 0x10u) apTAR += 4u;
            if (data) *data = wireVal;             /* posted arm: no wire value */
            Debug_Print("[VT] AP rd DRW sel=%08X wire=%08X latch=%08X%s\r\n",
                        dpSelect, wireVal, dpRdBuff, data ? "" : " (post)");
            return DAP_TRANSFER_OK;
        }

        if (regAddr == 0xFC)            val = ap_current_idr(); /* IDR  */
        else if (regAddr == 0xF8)       val = 0xE00FF003u;      /* Base */
        else if (regAddr == 0x04)       val = apTAR;            /* TAR  */
        else if (regAddr == 0x00)       val = apCSW | 0x40u;    /* CSW: DeviceEn driven by the AP's DEVICEIN input - always report enabled */
        else                            val = *reg;

        wireVal = val;
        dpRdBuff = val;
        if (data) *data = wireVal;
        Debug_Print("[VT] AP rd addr=%02X sel=%08X val=%08X%s\r\n",
                    regAddr, dpSelect, val, data ? "" : " (post)");
    }
    else
    {
        val = *data;
        uint32_t regAddr = (bank << 4) | (a32 << 2);

        if (regAddr == 0x04)        apTAR = val;                /* TAR  */
        if (regAddr == 0x00)        apCSW = val;                /* CSW  */
        if (regAddr == 0x0C)
        {
            virt_mem_write(apTAR, val);                          /* DRW  */
            if ((apCSW & 0x30u) == 0x10u) apTAR += 4u;
        }
        *reg = val;
        Debug_Print("[VT] AP wr addr=%02X sel=%08X val=%08X\r\n",
                    regAddr, dpSelect, val);
    }

    return DAP_TRANSFER_OK;
}

void Virtual_SWJ_Sequence(uint32_t count, const uint8_t *data)
{
    /* Line reset / JTAG-to-SWD sequences: nothing to do for the simulated
     * target (state machine is always live). */
    (void)count;
    (void)data;
}

void Virtual_SWD_Sequence(uint32_t info, const uint8_t *swdo, uint8_t *swdi)
{
    uint32_t val;
    uint32_t n, k;

    n = info & SWD_SEQUENCE_CLK;
    if (n == 0U) n = 64U;

    if (info & SWD_SEQUENCE_DIN)
    {
        /* capture zeros so the response frame length stays correct */
        while (n)
        {
            val = 0U;
            for (k = 8U; k && n; k--, n--)
            {
                val >>= 1;
            }
            val >>= k;
            *swdi++ = (uint8_t)val;
        }
    }
    else
    {
        while (n)
        {
            val = *swdo++;
            for (k = 8U; k && n; k--, n--) { }
        }
    }
}
