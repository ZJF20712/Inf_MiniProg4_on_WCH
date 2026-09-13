# WCH-MiniProg4 - MiniProg4 (KitProg3) compatible firmware for CH32V203G6U6
TARGET   := wch-miniprog4

MRS      := /c/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH
GCCDIR   := $(MRS)/Toolchain/RISC-V Embedded GCC/bin
OCD      := $(MRS)/OpenOCD/OpenOCD/bin/openocd.exe
OCD_CFG  := $(MRS)/OpenOCD/OpenOCD/bin/wch-riscv.cfg
PREFIX   := $(GCCDIR)/riscv-none-embed-
CC      := "$(GCCDIR)/riscv-none-embed-gcc"
OBJCOPY := "$(GCCDIR)/riscv-none-embed-objcopy"
SIZE    := "$(GCCDIR)/riscv-none-embed-size"

# sources
SRCS := \
	src/main.c \
	src/dap_usb.c \
	src/khpi.c \
	src/bridge.c \
	src/led.c 	src/virtual_target.c \
	src/debug.c \
	src/vcom_serial.c \
	src/Startup/system_ch32v20x.c \
	src/Startup/core_riscv.c \
	src/Peripheral/ch32v20x_gpio.c \
	src/Peripheral/ch32v20x_rcc.c \
	src/Peripheral/ch32v20x_usart.c \
	src/Peripheral/ch32v20x_misc.c \
	src/Peripheral/ch32v20x_exti.c \
	src/Peripheral/ch32v20x_dma.c \
	src/USB/USBLib/usb_core.c \
	src/USB/USBLib/usb_init.c \
	src/USB/USBLib/usb_int.c \
	src/USB/USBLib/usb_mem.c \
	src/USB/USBLib/usb_regs.c \
	src/USB/USBLib/usb_sil.c \
	src/USB/USBUsr/usb_desc.c \
	src/USB/USBUsr/usb_prop.c \
	src/USB/USBUsr/usb_istr.c \
	src/USB/USBUsr/usb_pwr.c \
	DAP/DAP.c \
	DAP/SW_DP.c \
	DAP/JTAG_DP.c \
	src/Startup/startup_ch32v20x_D6.S

OBJS := $(patsubst %.S,%.o,$(patsubst %.c,%.o,$(notdir $(SRCS))))
vpath %.c $(sort $(dir $(SRCS)))
vpath %.S $(sort $(dir $(SRCS)))
vpath %.o obj

# flags: QingKe V4 = RV32IMAC, ilp32
ARCH     := -march=rv32imac -mabi=ilp32 -mcmodel=medany
CFLAGS   := $(ARCH) -msmall-data-limit=8 -msave-restore -Os \
            -g -Wall \
            -ffunction-sections -fdata-sections -fno-common \
            -std=gnu99 -DDAP_VIRTUAL_TARGET -DDAP_FW_V1 $(CFLAGS_EXTRA) \
            -Isrc -Isrc/Startup -Isrc/Peripheral -Isrc/USB/USBLib -Isrc/USB/USBUsr \
            -IDAP
LDFLAGS  := $(ARCH) -msave-restore -nostartfiles \
            -T link.ld -Wl,--gc-sections -lgcc

.PHONY: all size clean flash hid flash-hid bulk flash-bulk

all: obj/$(TARGET).bin obj/$(TARGET).hex
	$(SIZE) obj/$(TARGET).elf

%.o: %.c
	@mkdir -p obj
	$(CC) $(CFLAGS) -c $< -o obj/$@

%.o: %.S
	@mkdir -p obj
	$(CC) $(CFLAGS) -c $< -o obj/$@

obj/$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(addprefix obj/,$(OBJS)) -o $@

obj/$(TARGET).bin: obj/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

obj/$(TARGET).hex: obj/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

size: obj/$(TARGET).elf
	$(SIZE) $<

flash: obj/$(TARGET).bin
	"$(OCD)" -f "$(OCD_CFG)" -c "program obj/$(TARGET).bin verify exit"

# HID mode is now the DEFAULT (DAP_FW_V1 always defined in CFLAGS)
# Build HID mode firmware (CMSIS-DAP v1, PID 0xF152, for PSoC Programmer)
hid: clean
	/c/msys64/usr/bin/make -j4

flash-hid: hid
	/c/msys64/usr/bin/make flash

# Build bulk mode firmware (CMSIS-DAP v2, PID 0xF151, for pyOCD/OpenOCD)
bulk: clean
	/c/msys64/usr/bin/make CFLAGS_EXTRA=-UDAP_FW_V1 -j4

flash-bulk: bulk
	/c/msys64/usr/bin/make flash

# Build real-SWD firmware (virtual target removed; drives PA0/PA1/PA4 GPIO)
real: clean
	/c/msys64/usr/bin/make CFLAGS_EXTRA=-UDAP_VIRTUAL_TARGET -j4

flash-real: real
	/c/msys64/usr/bin/make flash

clean:
	rm -rf obj
