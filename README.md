# WCH-MiniProg4

基于 **CH32V203G6U6** 的 MiniProg4（KitProg3 固件架构）兼容编程/调试器。
**双模式固件**：`DAP_FW_V1` 宏切换 HID（PSoC Programmer）和 Bulk（pyOCD/OpenOCD）模式。

## 当前验证状态（2026-09-12）

### HID 模式（`make CFLAGS_EXTRA=-DDAP_FW_V1`）
- PID **0xF152**，IF0 = CMSIS-DAP v1 (HID class 0x03, EP1 IN + EP2 OUT interrupt)
- **PSoC Programmer 完全识别并打开** ✅
  - `GetPorts()` → `MiniProg4 (CMSIS-DAP/HID/932B8FFB3D9F)`
  - `OpenPort()` → 成功 (hr=0)
  - 版本汇报：`MiniProg4. CMSIS-DAP Version 1.3.0. Firmware Version 2.40.1241. Hardware Id 05.`
- KHPI 0x80-0x94 厂商命令 ✅
- 桥协议框架 (0x86-0x89) ✅
- CDC-UART ✅
- **桥接口 IF1 在 HID 模式下也加载 WinUSB** ✅（与真 KitProg3 行为一致）
  - bcdUSB = 2.10 + BOS + MS OS 2.0（仅桥 function subset，HID 接口仍用系统 HidUsb）
  - Windows 注册表：`MI_01` service = `WINUSB`，`Device Parameters.DeviceInterfaceGUID = {CDB3B5AD-...}`
  - libusb 1.0.30 可 claim IF1 并完成桥协议读写（0x86 速度查询/设置）✅
  - PSoC Programmer COM 服务的 libusb 桥通道因此可用
- **pyOCD 也能调试 HID 探针** ✅（CMSIS-DAP v1 over HID）
  - 连接 / halt / 写读 r0-r15/xPSR/MSP/PSP / continue 全链路通过
- **PSoC Programmer COM 自动化**（PSoCProgrammerCOM.PSoCProgrammerCOM_Object，64 位 PowerShell 可直接驱动）
  - `GetPorts`/`OpenPort`/`SetProtocol(SWD=8)`/`SetAcquireMode("Reset")` 全部 rc=0 ✅
  - 注意：所有方法的尾部 `string` 参数是 **OUT strError**（需 [ref] 传引用），照 `Documents\PSoC Programmer COM User Guide.pdf`
  - 残留的 PSoCProgrammerCOM.exe 进程会携带旧状态导致 OpenPort 假失败，重试前先 taskkill
  - **未通**：`Acquire()` 仍 E_FAIL——串口 trace 显示 OpenPort 阶段序列为
    `DAP_Info×7 → DAP_Connect(0x02) → DAP_SWJ_Clock(0x11) → KHPI 0x80 → KHPI 0x90 → DAP_Disconnect`，
    即服务器读完 0x80/0x90 后内部放弃探针识别（对外仍返回 hr=0）。0x80/0x90 响应已与官方
    KitProg3 固件源码逐字节核对一致，待进一步定位服务器侧判定条件
- KHPI 0x90 Capabilities 已对齐真 MiniProg4 (HWID 5)：`90 00 3F 13 0F 6E 01 00 00 80 8D 5B 00 07 0F`
  （接口位图 0x3F 含 I2C/SPI/双 DAP/电源/电压测量；SPI 366Hz-6MHz；SS=0x07；电压 1.8/2.5/3.3/5V）
- KHPI 0x84 电源：无调压硬件时按虚拟电源应答（开关成功、VTARG 恒报 3300mV 在位）

## 真实目标接入（2026-09-13，进行中）

目标：**CYPD5235-96（EZ-PD CCG5，双 Cortex-M0）**，整机设备引出的 SWD 测试点。

- 真实 SWD 构建：`make real`（`-UDAP_VIRTUAL_TARGET`），GPIO 翻转位沿 PA1=SWCLK / PA0=SWDIO / PA4=nRESET
- **SWD 波形经 DSLogic 逐位验证**（sigrok-cli / DSView 导出 VCD 解析）：
  - dormant 唤醒序列（8 低 + 0x6209F392×4）、线复位 52 周期、IDCODE 读请求位流全部逐位正确
- **修复的关键 bug**：
  - acquire 请求字节 `0x81` 错误（按 DAP_TRANSFER 标志解释是 AP 写！）→ 改 `DAP_TRANSFER_RnW`
  - **`PIN_DELAY_SLOW` 的 C 空延时循环被 GCC -Os 整体优化 → SWD 时钟跑到 6.7MHz**；改为 RISC-V 内联汇编减计数循环后实测 323kHz
  - `make flash` 因 ELF 前置条件用裸文件名（无 `vpath %.o`）导致每次全量重编并覆盖为默认 CFLAGS 构建 → 加 `vpath %.o obj`
- 新增 KHPI 命令：
  - `0x95` 复位线可见性测试（8×400ms 慢脉冲）——**已确认 XRES 连通**（目标 TypeC 输出随复位中断）
- **当前卡点**：CCG5 对 SWD 完全无应答（DP/AP、复位中/运行态、dormant 唤醒前后全试过，ACK 恒为上拉电平）。
  用户实测 SWDIO 阻值偏低，怀疑目标侧 SWDIO 存在低阻负载（短路/异常上拉），硬件排查中。

## CCG5 真实调试突破（2026-09-13 晚）

**CYPD5235-96 已完整 acquire 并可调试 + 可编程**：

- **DP IDCODE = 0x0BB11477**（Cypress DAP），AP IDR = 0x04770021
- pyOCD 完整链路：connect → halt → 读寄存器 → 内存/块读写
- **CPUID = 0x410CC200 = Cortex-M0 r0p0**（CYPD5235 为单核 M0），halt 后 pc 为目标自身固件
- **PSoC Programmer 编程全流程（COM 自动化驱动）**：
  `GetPorts→OpenPort→SetProtocol(SWD)→SetChipType(CYPD5xxx)→SetAcquireMode("Reset")→DAP_Acquire→PSoC4_GetSiliconID(2102/B1)→HEX_ReadFile(131072B)→PSoC4_EraseAll→PSoC4_ProgramRowFromHex×512（0 失败，~30s）→PSoC4_VerifyRowFromHex 全部一致`
- Flash 正确映射：用户 flash 走 **0x00000000** 别名空间（128KB 与官方读出逐字节一致）；0x10000000 为受限区（SFlash，仅头部可读）

**关键机制（经 KitProg3-master 源码对照 + DSLogic 波形逐位验证得来）**：
1. CCG5 的 DAP 在 **XRES 释放后约 3ms** 才上线，且**只应答一次事务即关闭**（产品固件接管）
2. acquire 结构 = 复位后**连续轰炸握手+IDCODE** 直到窗口命中（官方 SwdAcquirePSoC4 同款）
3. 首次应答后**必须立即执行 INIT**（CTRL/STAT→SELECT→CSW→TAR(0x40030014)→DRW(0x80000000) 的 TMR 解锁）——**中间不能有任何 UART 打印**（一次 ~3.5ms 的 Debug_Print 就足以把 INIT 推出窗口）
4. TMR 解锁后 DAP 保持在线，可正常调试
5. **DAP_PACKET_COUNT=8**：PSoC Programmer 库按官方 MiniProg4 容量批量突发命令，4 深环会静默丢包导致 "Failed to send packet (batch)"

本轮还修复：0x81 错误请求字节、PIN_DELAY 被 -Os 优化（6.7MHz→323kHz→1MHz 分场景）、复位极性、
`make flash` 重编译覆盖（`vpath %.o obj`）、SWDIO 内部上拉掩蔽目标驱动（改纯高阻输入）、
引脚角色运行时反转探测（g_swdSwap）、dormant 唤醒字节序、JTAG→SWD 0xE79E 切换、
20 idle 周期、SWJ_Clock 钳制 ~1MHz、以及请求/响应环深度 4→8。

### Bulk 模式（默认 `make`）
- PID **0xF151**，IF0 = CMSIS-DAP v2 (vendor 0xFF, EP1 OUT + EP2 IN bulk)
- **pyOCD 完全识别并调试** ✅
  - `pyocd list` → `Cypress Semiconductor Cypress MiniProg4 (CMSIS-DAP)`
  - halt / reg pc / reg sp / reg xpsr 全部工作
- 虚拟 Cortex-M4 目标完整（ROM 表/组件发现/DCRSR/DCRDR）
- CDC-UART ✅

### 本轮修复的关键 Bug

| # | Bug | 修复 | 影响 |
|---|-----|------|------|
| 1 | `Usb_wOffset` 未清零（HID report desc） | 在 `USBD_Data_Setup` 返回前设置 `= 0` | HID 枚举失败 → PSoC Programmer 立即识别 |
| 2 | `DAP_EndpointInDone` 从 ISR 调用 `SetEPTxValid` | ISR 只设 `RespIdle = 1`，主循环发送 | 设备死锁/冻结 |
| 3 | HID 模式 EP 路由错误（EP1 而非 EP2） | `#ifdef DAP_FW_V1` 条件编译 | DAP 命令无法接收 |
| 4 | MS OS 2.0 `wTotalLength` 溢出（330 > 255） | 拆分为字节对 `0x4A, 0x01` | WinUSB 驱动加载失败 |
| 5 | Config subset `wTotalLength` 312 ≠ 320 | 改为 `0x40, 0x01` | Bulk 模式 Code 10 |
| 6 | FW_BUILD_NUMBER = 0 ≠ 1241 | 改为 1241 | PSoC Programmer 版本检查失败 |
| 7 | **BOS 处理被 `#ifndef DAP_FW_V1` 屏蔽** | 移除条件编译，两种模式都响应 GET_DESCRIPTOR(BOS) | HID 模式 bcdUSB=2.10 但 STALL BOS → Windows 视为不合规 2.1 设备 → **Code 10 (CM_PROB_FAILED_START)**，EP0 日志显示 Windows 读 BOS 后直接放弃 |
| 8 | **虚拟目标 DCRSR/DCRDR 语义错误** | DCRSR 写入触发传输（先写 DCRDR 再写 DCRSR），DCRDR 写入仅存值；DCRDR 读返回 `dcrdr` | pyOCD 写 r0/r1 寄存器**交叉**（值落到上一次选择的寄存器上）；ARM SCS 正确语义是 DCRSR 写入触发 |
| 9 | **虚拟目标不支持 resume** | DHCSR 写 C_HALT=0 时清除 halted | pyOCD `continue` 报 "debug event occurred" |
| 10 | **devnode 旧于 MS OS 2.0 → `DeviceInterfaceGUID` 未写入** | 序列号派生从 `+` 改为 `^`（UID XOR），Windows 全新安装 devnode 并处理 MS OS 2.0 注册属性 | libusb 无法 attach WinUSB 子设备（`LIBUSB_ERROR_NOT_SUPPORTED`）；libusb 1.0.24+ 要求 WinUSB 子设备有已注册的 DeviceInterfaceGUID 才能打开 |


## 构建命令

```bash
# Bulk 模式 (CMSIS-DAP v2, PID 0xF151, pyOCD/OpenOCD)
make

# HID 模式 (CMSIS-DAP v1, PID 0xF152, PSoC Programmer)
make CFLAGS_EXTRA=-DDAP_FW_V1

# 烧录（两种模式都相同）
make flash

# 或者直接用 openocd
openocd -f wch-riscv.cfg -c "init; reset halt; program obj/wch-miniprog4.bin verify; reset run; shutdown"
```

**烧录后必须等待 10-15 秒**让 Windows 完成重枚举。
**切换模式后如果出现 Code 10，拔插 USB 线。**

## 引脚（沿用 DAPLink-CH32V203）

| 信号 | 引脚 | 说明 |
|------|------|------|
| SWDIO / TMS | PA0 | |
| SWCLK / TCK | PA1 | |
| nRESET / XRES | PA4 | 开漏 |
| LED | PA5 | 状态灯 |
| TDO | PA6 | 输入 |
| TDI | PA7 | 输出 |
| CDC UART TX/RX | PA2 / PA3 | USART2 |
| 调试日志 TX | PA9 | USART1 → WCH-Link (COM26), 115200 |

## 虚拟目标说明

`DAP_VIRTUAL_TARGET` 编译开关（Makefile 默认开启）使 SWD 事务不驱动真实引脚，
改为仿真完整的 ARM ADIv5 调试目标（DP + AHB-AP + ROM 表 + Cortex-M4 SCS + DWT/FPB/ITM），
支持 DCRSR/DCRDR 核心寄存器访问。

关闭虚拟目标（连接真实目标时）：从 Makefile 移除 `-DDAP_VIRTUAL_TARGET`。

## 硬件相关功能表（待分配）

| # | 功能 | 协议入口 | 需要的硬件 |
|---|------|---------|-----------|
| 1 | 真实 SWD/JWD 驱动 | — | 移除 DAP_VIRTUAL_TARGET + 逻辑分析仪验证 |
| 2 | 目标电压输出 VTARG | KHPI 0x84 | DC-DC/数字电位器 + GPIO |
| 3 | VTARG 电压测量 | KHPI 0x84 GET | ADC 通道 |
| 4 | USB-I2C 桥 | 0x88 | I2C 引脚 + 上拉 |
| 5 | USB-SPI 桥 | 0x89 | SPI 引脚 + SS |
| 6 | GPIO 桥 | 0x8A-0x8D | 2 路 GPIO + EXTI |
| 7 | RTS/CTS 硬件流控 | KHPI 0x93 | 2 路 GPIO |
| 8 | XRES 独立复位线 | KHPI 0x85 | 1 路 GPIO |
| 9 | PSoC 专属 acquire | KHPI 0x85 DUT | 无新硬件（软件） |
| 10 | SWO 跟踪 | DAP_SWO | TDO 复用 + 定时器 |

## 目录结构

```
WCH-MiniProg4/
├── Makefile / link.ld          # 32K flash / 10K RAM, 栈 2K
├── DAP/                        # ARM CMSIS-DAP 2.1.0 核心 + 虚拟目标补丁
└── src/
    ├── main.c                  # 初始化 + 主循环 + EP0 日志
    ├── DAP_config.h            # CH32V203 引脚/时钟/版本字符串
    ├── dap_usb.c/h             # DAP 传输层 (HID: EP1 IN/EP2 OUT, Bulk: EP1 OUT/EP2 IN)
    ├── khpi.c/h                # KHPI 厂商命令 0x80-0x94 (fw 2.40.1241, HWID 0x05)
    ├── bridge.c/h              # 桥协议 0x86-0x89 框架
    ├── virtual_target.c/h      # ADIv5 虚拟目标 (DP+AHB-AP+ROM表+核心)
    ├── led.c/h / debug.c/h     # 状态灯 / PA9 日志
    ├── vcom_serial.c/h         # CDC<->USART2 DMA 桥
    └── USB/                    # WCH USBFS 设备栈 + MiniProg4 描述符
        ├── USBUsr/usb_desc.c   # 双模式描述符 (HID/Bulk 条件编译)
        ├── USBUsr/usb_prop.c   # 端点配置 + HID report descriptor 处理
        ├── USBUsr/usb_istr.c   # EP 回调 + ISR 计数器
        └── USBLib/usb_core.c   # EP0 日志缓冲 + STALL 诊断
```
