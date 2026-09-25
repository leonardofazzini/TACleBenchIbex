# Ibex platform (what we use from the `Secure-Ibex/` submodule)

The submodule is not edited from this repo. This file describes only the parts the PapaBench port consumes: the **standard Ibex** reference system (no Smtctx), its bare-metal software kit, and the Verilator flow. Paths below are relative to `Secure-Ibex/`.

## SoC memory map (standard target)

From `sw/Standard_Only_SW/Standard_Tests/common/reference_system_regs.h` and `README.md`:

| Region | Base | Notes |
|---|---|---|
| RAM | `0x00100000` | 128 KiB in HW; the stock `link.ld` uses 56 KiB code/data + 8 KiB stack |
| SimCtrl | `0x00020000` | Verilator only; `SIM_CTRL_OUT` (+0x0) prints a char, `SIM_CTRL_CTRL` (+0x8) halts |
| Timer | `0x80000000` | `mtime`/`mtimecmp`, IRQ 7; drives the PapaBench schedulers |
| TimerA / TimerB | `0x80010000` / `0x80020000` | fast IRQ, `mip` bits 18/19; FBW servo compare / ADC (both programs) |
| SPI master / SPI slave | `0x80004000` / `0x80005000` | stock since submodule `d5e2fb5` (`spi_master.sv`, `spi_slave.sv`); fast IRQ 20/21; Autopilot / FBW end of the inter-MCU link |
| TimerD / TimerE | `0x80040000` / `0x80050000` | **our patch** (`hw/patches/0002-soc-timers-d-e.patch`); fast IRQ 22/23; Autopilot `link_fbw` compare / FBW virtual UART |
| UART0 | `0x80001000` | IRQ 16 |
| GPIO bank 0 / bank 1 | `0x80002000` / `0x80060000` | **our patch** (`hw/patches/0003-soc-gpio-banks.patch`): both are `hw/rtl/papabench_gpio.sv` (stock `gpio.sv` map OUT `+0x0`, IN `+0x4`, IN_DBNC `+0x8`, plus IRQ_EN `+0xC`, IRQ_STATUS `+0x10` write-1-to-clear, IRQ_FALL `+0x14`, IRQ_RISE `+0x18`); fast IRQ 17 / 24; FBW / Autopilot. Bank 1 has its own ports `gp1_i`/`gp1_o` |
| PWM | `0x80003000` | 12 channels, no IRQ, no read-back; channel `i`: pulse width at `+8i`, period (counter max) at `+8i+4`; high for `width` cycles out of `max+1`. Counter is **20 bits** in our model (`hw/patches/0001-soc-pwm-ctr-size.patch`; 8 upstream) |
| Debug module | `0x1A110000` | |

Nothing is mapped at 0x0–0xFF. Bus behaviour on an unmapped access: `TODO: verify` (see `rtl/system/` in the submodule).

## Bare-metal software kit (`sw/Standard_Only_SW/Standard_Tests/`)

- `common/common.mk`: compiles `common/*.c` + `$(PROGRAM).c` + `$(EXTRA_SRCS)` + `$(CRT)` with `riscv32-unknown-elf-gcc`, `CFLAGS ?= -march=$(ARCH) -mabi=ilp32 -static -mcmodel=medany -Wall -g -Os -fvisibility=hidden -nostdlib -nostartfiles -ffreestanding $(PROGRAM_CFLAGS)`, links with `-T $(LINKER_SCRIPT) … $(LIBS)`; produces `.elf`, `.bin`, `.vmem` (the latter needs `srec_cat`). `ARCH ?= rv32imc` — override to `rv32im`. `LIBS` is empty by default → add `-lgcc`.
- `common/link.ld`: `ram` at `0x00100000` length `0xE000`, `stack` at `0x0010E000` length `0x2000`; `ENTRY(_vectors_start + 0x80)`; `tohost = 0x20008`.
- `common/reference_system_common.[ch]`: `putchar`, `puts`, `puthex`, `sim_halt()` (writes 1 to `SIM_CTRL_CTRL`), `pcount_reset()`, `pcount_enable()`, `PCOUNT_READ(mcycle, dst)`, `install_exception_handler()`. Uses `<stdint.h>` types.
- `common/timer.[ch]`, `uart.[ch]`, `gpio.[ch]`, `pwm.[ch]`: peripheral drivers.
- `hello_test/`: the template — `Makefile` sets `PROGRAM`, `CRT ?= crt0.S`, includes `../common/common.mk`; `crt0.S` holds the vector table (`simple_exc_handler`, `simple_timer_handler`), sets `sp = _stack_start`, clears `.bss`, calls `main`.

`papabench_ibex/` links with its own `papabench_ibex/link.ld` (same layout as `common/link.ld`, but 112 KiB code+data + 16 KiB stack = the whole 128 KiB RAM of `reference_system_core.sv`); the stock 56 KiB is too small for the Autopilot with the peripheral models.

`common.mk` is **not** reused by `papabench_ibex/`: it writes each `.o` next to its source, which would drop build artifacts into the submodule's `common/`. Our Makefile compiles `reference_system_common.c`, `timer.c`, `uart.c` and `hello_test/crt0.S` into `papabench_ibex/build/<prog>/ibex/` and links with the stock `common/link.ld`.

## Verilator flow (from `Secure-Ibex/README.md`)

Upstream flow: build the standard-Ibex model once, from `Secure-Ibex/` (this project does **not** do this, see below):

```
fusesoc --cores-root=. run --target=sim_ibex --setup --build riscv:soc:reference_system --verilator_options=-Wno-fatal
```

Run an ELF:

```
./build/riscv_soc_reference_system_0/sim_ibex-verilator/Vreference_system --meminit=ram,<path/to/prog.elf> [-t]
```

`-t` dumps a waveform. Requires Verilator 5.014 or 5.017 and the `.venv` with FuseSoC (see the README).

The simulator writes into its **current directory** (verified 2026-09-23): `reference_system.log` = everything written to SimCtrl (`putchar`/`puts` of `common/`, so the harness results); `uart0.log` = UART0 output (also exposed on a `/dev/pts/N`; empty for PapaBench, no program transmits); `pwm.log` = PWM pulses (our monitor, patch 0005); `reference_system_pcount.csv` = Ibex performance counters; stdout = banner, `Executed cycles`, performance counter summary. A write of 1 to `SIM_CTRL_CTRL` (`sim_halt()`, or `crt0.S` after `main` returns) ends the run.

This project builds the model from the submodule sources **plus our SoC patches** (see "SoC patches" below) with `fusesoc --cores-root=<repo>/Secure-Ibex --cores-root=papabench_ibex/build/sim/hw run --target=sim_ibex --setup --build --build-root=papabench_ibex/build/sim papabench:soc:reference_system --verilator_options=-Wno-fatal` (`make sim`), so the output is `papabench_ibex/build/sim/sim_ibex-verilator/Vreference_system` and the submodule stays clean (verified with `git -C Secure-Ibex status`, 2026-09-23). The FuseSoC venv must be activated: the pre-build `util/check_tool_requirements.py` runs `pip3 show edalize` from `PATH` and fails otherwise.

## Timer (`vendor/lowrisc_ibex_smtctx/shared/rtl/timer.sv`)

`mtime` increments by 1 every clock cycle (no prescaler). `timer_intr_o` is **sticky**: it is set once `mtime >= mtimecmp` and stays set until `mtimecmp` (low or high half) is written, so a compare that passes while interrupts are disabled is never lost. `mtimecmp` resets to all ones (never fires). Registers: `mtime` +0x0/+0x4, `mtimecmp` +0x8/+0xC; the submodule version adds an interrupt-enable mask at +0x100 (reset = enabled). Driver (`common/timer.c`): `timer_read()` (64-bit, overflow-safe), `timecmp_update()` (writes low = −1, high, low to avoid a spurious match), `timer_enable()` + `simple_timer_handler` (not used by `papabench_ibex/`, which installs its own ISR on vector 7 with `install_exception_handler`).

The model built from the submodule (commit `b04c064`, and `d5e2fb5` since 2026-09-24) contains `u_timer`, TimerA and TimerB. (The older prebuilt model in the sibling clone `~/01_Progetti_PC/Secure-Ibex/build/`, used before 2026-09-23, had only `u_timer`.) Results with the submodule model are identical to the prebuilt one for both programs.

## Interrupt lines (verified in `rtl/system/reference_system_core.sv`, 2026-09-24, submodule `d5e2fb5`)

`irq_fast_i` = `{6'b0, gpio1_irq, timer_e_irq, timer_d_irq, spi_s_irq, spi_m_irq, timer_b_irq, timer_a_irq, gpio_irq, uart_irq}` in our patched model, i.e. IRQ 16 UART, 17 GPIO bank 0, 18 TimerA, 19 TimerB, 20 SPI master, 21 SPI slave (stock), 22 TimerD, 23 TimerE (patch 0002), 24 GPIO bank 1 (patch 0003); 25–30 tied to 0; timer on IRQ 7; software/external/NMI tied to 0. Priority among fast interrupts: lower index first (Ibex).

- UART IRQ = RX FIFO not empty (no TX interrupt); reading RX drains it. Baud fixed at 115200 (50 MHz).
- Stock: `gp_i[0]` reaches the core directly and level-sensitive (IRQ 17) and `gpio.sv` has no interrupt register. Patched (0003): the GPIO line of each bank is `|(IRQ_STATUS & IRQ_EN)`; IRQ_STATUS latches the edges selected by IRQ_FALL/IRQ_RISE (2-flop synchroniser, edge between the last two stages) whether or not IRQ_EN is set, and an edge in the same cycle as its write-1-to-clear wins.
- In the upstream Verilator top (`rtl/top_simulation/reference_system.sv`) `gp_i` is undriven and `uart_rx` comes from `uartdpi` (a host pty, not deterministic). Our patch 0005 drives both GPIO banks' inputs and `uart_rx` from `papabench_env` instead (see SoC patches); `uartdpi` still receives the UART TX (`uart0.log`).
- No ADC is instantiated in the SoC.
- SPI master (`spi_master.sv`): CTRL `+0x00` (EN, CPOL, CPHA), DIV `+0x04` (SCK = 50 MHz / (2·(DIV+1)); a byte takes 17·(DIV+1) cycles, the extra half period is hold time), TXDATA `+0x08` (push; starts a transfer when EN), RXDATA `+0x0C` (pop, 0 when empty), STATUS `+0x10`, CS `+0x14` (software chip select), IE `+0x18` (RX not empty, idle); 16-byte FIFOs; IRQ level-sensitive. MISO goes through a 2-flop synchronizer: DIV ≥ 5 with the on-chip slave.
- SPI slave (`spi_slave.sv`): same CTRL/TXDATA/RXDATA/IE layout, STATUS adds BUSY (= CS asserted), TX_UNDERRUN, FRAME_DONE (CS released; sticky, write 1 to clear); sends 0x00 when its TX FIFO is empty; **with CPHA = 0 a byte must be queued before the master starts it** (its MSB is on MISO before the first edge); a partial byte is dropped on CS release. There is no TX FIFO flush.
- The upstream simulation top wires the SoC's master to its own slave (loopback, MISO pulled up when not driven); our patch 0005 keeps it.

## SoC patches (`papabench_ibex/hw/`)

The submodule is never edited. `make sim`:

1. copies `SOC_FILES` (Makefile: `rtl/system/{reference_system_core,debounce,pwm,pwm_wrapper,uart,spi_master,spi_slave}.sv`, `rtl/top_simulation/reference_system.sv`, `dv/verilator/reference_system{.cc,.h,_main.cc,_verilator_lint.vlt}`) from `Secure-Ibex/` into `build/sim/hw/` (with `cp --parents`, so the tree is kept);
2. copies `hw/*.core` there, adds `hw/rtl/*.sv` and the generated stimulus under `rtl/papabench/` and `hw/dv/*.cc` under `dv/`, and applies `hw/patches/*.patch` in name order (`patch -p1 -N`; paths `a/rtl/...` relative to the submodule root). A failing patch stops the build;
3. runs FuseSoC on `papabench:soc:reference_system` (`hw/papabench_soc.core`, same `sim_ibex` options as upstream), which depends on `papabench:soc:reference_system_core` (`hw/papabench_soc_core.core`, the staged RTL) and on the submodule's `lowrisc:ibex:ibex_top`, `sim_shared`, DPI cores.

`make sim-dual` does the same staging, then builds `papabench:soc:dual` (`hw/papabench_dual.core`) into `build/sim-dual/` (`Vpapabench_dual`).

The single-MCU top keeps the name `reference_system` and the instance `u_reference_system`: the testbench C++ looks up `TOP.reference_system` and the RAM path by name.

### Two-MCU model (`make sim-dual`, 2026-09-24)

`hw/rtl/papabench_dual.sv` (module `papabench_dual`) instantiates the patched `reference_system_core` twice: `u_fbw` (`SimCtrlLogName("fbw.log")`) and `u_autopilot` (`"autopilot.log"`), same clock and reset. Link: `u_autopilot`'s SPI master → `u_fbw`'s SPI slave (MISO pulled up when not driven); the other two SPI ends are tied off. One `papabench_env` on the FBW's GPIO bank 0 and the Autopilot's bank 1 (the other bank of each SoC is unconnected: inputs 0, outputs open); GPS on the Autopilot's `uart_rx`, the FBW's idles high; PWM monitor on the FBW's `pwm_o` (`pwm.log`); no UART or debugger DPI, no performance-counter dump. The simulation ends when `gp_o[7]` of both banks is set (each harness sets it after printing its results, `papabench_periph_done()`) — a SimCtrl halt still ends it at once. Testbench `hw/dv/papabench_dual.cc`: RAMs registered as memory areas `fbw` (base 0) and `autopilot` (base `0x10000000`, only to keep the areas disjoint: a named ELF load writes the flattened image at the start of the RAM) → `--meminit=fbw,<elf> --meminit=autopilot,<elf>`. Toplevel `papabench_dual` (`-DTOPLEVEL_NAME=papabench_dual`).

| Patch | Change |
|---|---|
| `0001-soc-pwm-ctr-size.patch` | `PwmCtrSize` becomes a `reference_system_core` parameter (default 8); the simulation top passes 20 |
| `0002-soc-timers-d-e.patch` | TimerD `u_timer_d` at `0x80040000` (`irq_fast_i[6]`, IRQ 22), TimerE `u_timer_e` at `0x80050000` (`irq_fast_i[7]`, IRQ 23); CV32E40P `core_irq[22..23]` too; `NrDevices` +2 |
| `0003-soc-gpio-banks.patch` | `gpio` → `papabench_gpio` (`u_gpio`, bank 0, `irq_fast_i[1]` = IRQ 17 instead of the raw `gp_i[0]`) plus `u_gpio1` (bank 1 at `0x80060000`, new ports `gp1_i`/`gp1_o`, `irq_fast_i[8]` = IRQ 24); CV32E40P `core_irq[17]`, `core_irq[24]` too; `NrDevices` +1 |
| `0004-soc-simctrl-log-name.patch` | `SimCtrlLogName` parameter of `reference_system_core` (default `"reference_system.log"`), passed to `simulator_ctrl`'s `LogName` |
| `0005-sim-top-papabench-env.patch` | simulation top instantiates `papabench_env` (`hw/rtl/papabench_env.sv`) on both GPIO banks (bank 0 = FBW ports, bank 1 = Autopilot ports) and `uart_rx`; `uartdpi`'s TX output (host → RX) is left unconnected; the SPI loopback stays |
| `0006-sim-top-pwm-monitor.patch` | simulation top instantiates `papabench_pwm_monitor` (`hw/rtl/papabench_pwm_monitor.sv`) on `pwm_o`: one `pwm.log` line per pulse (`<rise> <channel> <width>`, cycles since reset) |

History: until 2026-09-24 (submodule `b04c064`) a patch added TimerC (`0x80030000`, IRQ 20) for the SPI link timing, and `gp_i[1]` was IRQ 21; the submodule's SPI master/slave took IRQ 20/21, the series was rebased and TimerC dropped (the link is the real SPI now). Until the GPIO banks (2026-09-24, user request: the two programs are to be virtualised later, so they must not share one GPIO register) both programs used the one stock GPIO: `gp_i[0]`/`gp_i[1]` were edge latches in `papabench_env`, raw on IRQ 17/24, acknowledged by toggling `gp_o[0]`/`gp_o[6]`, and the two programs' `gp_o` bits were OR'ed in the dual top.

`make sim` also copies `hw/rtl/*.sv` to `build/sim/hw/rtl/papabench/` and generates `papabench_stim.svh` there with `hw/stimulus/gen_stimulus.py`; the environment and monitor are listed in `hw/papabench_soc.core` (`files_simulation`), `papabench_gpio.sv` (part of the SoC) in `hw/papabench_soc_core.core`, which no longer lists the stock `gpio.sv`. What the environment plays: `.claude/reference/port-harness.md` → Simulation environment.

Verified 2026-09-23 (old numbering, before the 2026-09-24 rebase; patches 0001–0002, before the peripheral models existed): FBW and Autopilot `reference_system.log` and `Executed cycles` identical on the unpatched and patched models; with 0003 and the models, two consecutive runs of each program are cycle-identical; `make hwtest` passes (TimerC: 4/4 periodic IRQs, never early, max latency 18 cycles; TimerA/B quiet; `pwm_o[0]` 75000 high / 100001 period). 2026-09-24 (patches 0004–0005): `make hwtest` passes — TimerC/D/E 4/4 periodic IRQs each on its own line, never early, no other line (17–23) raised, max latency 48 cycles (heavier test ISR); modem clock → IRQ 21 once per edge with the `gp_o[6]` ack, IRQ 17 quiet; `pwm.log` has the 75000-cycle pulses. Note the PWM start-up: the counter starts when the period register is written, so the first pulse after programming is 2 cycles short.

To add a patch: copy the file(s) from the submodule into two trees `a/` and `b/`, edit `b/`, `diff -u`, rewrite the headers to `--- a/<path>` / `+++ b/<path>`, save as `hw/patches/NNNN-<name>.patch`; add any new file to `SOC_FILES` and to the `.core` file. After a submodule update that breaks the series: apply the patches one by one to a copy of the new files (stage per patch), fix the failing ones by hand, and regenerate every patch as the diff between consecutive stages; check that the whole series applies to a fresh copy.

2026-09-24 (series rebased on `d5e2fb5`; GPIO banks the same day): `make hwtest` passes — TimerD/E 4/4 periodic IRQs on their own lines, no other line (17–24) raised, max latency 48 cycles; bank 0 and bank 1 OUT independent; modem clock (bank 1) → IRQ 24 once per falling edge, acknowledged by the IRQ_STATUS write-1-to-clear, bank 0 (IRQ 17) quiet; a PPM edge on bank 0 with IRQ_EN clear sets IRQ_STATUS without IRQ 17, the write-1-to-clear clears it, and with IRQ_EN set the next edge raises IRQ 17; SPI loopback at the models' settings (DIV 24, mode 0) exchanges 3 bytes each way through IRQ 20/21, FRAME_DONE set on CS release; PWM checks unchanged.

## Cycle counting

`mcycle`/`minstret` are readable via `PCOUNT_READ( mcycle, x )`; `pcount_enable( 1 )` clears `mcountinhibit`, `pcount_reset()` zeroes the counters. RV32 `mcycle` is 32-bit in `PCOUNT_READ`; `mcycleh` is needed for runs longer than ~4·10⁹ cycles (not expected per task).

## Traps

- The submodule's `CLAUDE.md` says "no compressed instructions" for Smtctx work, but the standard kit defaults to `rv32imc`. We use `rv32im` by decision, not by inheritance.
- The installed toolchain (`/tools/riscv/riscv32`, GCC 10.2) has only the `rv32imc` multilib: `-lgcc` pulls compressed code into an otherwise rv32im binary.
- `-nostdlib` drops libgcc too; soft-float and 64-bit division helpers are then undefined.
- Stock `link.ld` gives 56 KiB for code+data; the Autopilot with soft-float may need a harness-owned linker script up to the 128 KiB of HW RAM.
