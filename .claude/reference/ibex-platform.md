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
| TimerC | `0x80030000` | **our patch** (`hw/patches/0001-soc-timer-c.patch`); fast IRQ 20 (`mip` bit 20); not in `reference_system_regs.h`; SPI link (both) |
| TimerD / TimerE | `0x80040000` / `0x80050000` | **our patch** (`hw/patches/0004-soc-timers-d-e-gpio-irq.patch`); fast IRQ 22/23; Autopilot `link_fbw` compare / FBW virtual UART |
| UART0 | `0x80001000` | IRQ 16 |
| GPIO | `0x80002000` | |
| PWM | `0x80003000` | 12 channels, no IRQ, no read-back; channel `i`: pulse width at `+8i`, period (counter max) at `+8i+4`; high for `width` cycles out of `max+1`. Counter is **20 bits** in our model (`hw/patches/0002-soc-pwm-ctr-size.patch`; 8 upstream) |
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

The model built from the submodule (commit `b04c064`) contains `u_timer`, TimerA and TimerB. (The older prebuilt model in the sibling clone `~/01_Progetti_PC/Secure-Ibex/build/`, used before 2026-09-23, had only `u_timer`.) Results with the submodule model are identical to the prebuilt one for both programs.

## Interrupt lines (verified in `rtl/system/reference_system_core.sv`, 2026-09-23)

`irq_fast_i` = `{7'b0, timer_e_irq, timer_d_irq, gp_i[1], timer_c_irq, timer_b_irq, timer_a_irq, gp_i[0], uart_irq}` in our patched model, i.e. IRQ 16 UART, 17 `gp_i[0]`, 18 TimerA, 19 TimerB, 20 TimerC (patch 0001), 21 `gp_i[1]`, 22 TimerD, 23 TimerE (patch 0004); 24–30 tied to 0; timer on IRQ 7; software/external/NMI tied to 0.

- UART IRQ = RX FIFO not empty (no TX interrupt); reading RX drains it. Baud fixed at 115200 (50 MHz).
- `gp_i[0]` and `gp_i[1]` reach the core **directly and level-sensitive**; the GPIO has no interrupt or acknowledge register.
- In the upstream Verilator top (`rtl/top_simulation/reference_system.sv`) `gp_i` is undriven and `uart_rx` comes from `uartdpi` (a host pty, not deterministic). Our patch 0003 drives both from `papabench_env` instead (see SoC patches); `uartdpi` still receives the UART TX (`uart0.log`).
- No SPI or ADC is instantiated in the SoC (`spi_top.sv` is compiled but unused).

## SoC patches (`papabench_ibex/hw/`)

The submodule is never edited. `make sim`:

1. copies `SOC_FILES` (Makefile: `rtl/system/{reference_system_core,debounce,gpio,pwm,pwm_wrapper,uart,spi_host,spi_top}.sv`, `rtl/top_simulation/reference_system.sv`, `dv/verilator/reference_system{.cc,.h,_main.cc,_verilator_lint.vlt}`) from `Secure-Ibex/` into `build/sim/hw/` (with `cp --parents`, so the tree is kept);
2. copies `hw/*.core` there, adds `hw/rtl/*.sv` and the generated stimulus under `rtl/papabench/`, and applies `hw/patches/*.patch` in name order (`patch -p1 -N`; paths `a/rtl/...` relative to the submodule root). A failing patch stops the build;
3. runs FuseSoC on `papabench:soc:reference_system` (`hw/papabench_soc.core`, same `sim_ibex` options as upstream), which depends on `papabench:soc:reference_system_core` (`hw/papabench_soc_core.core`, the staged RTL) and on the submodule's `lowrisc:ibex:ibex_top`, `sim_shared`, DPI cores.

The top module keeps the name `reference_system` and the instance `u_reference_system`: the testbench C++ looks up `TOP.reference_system` and the RAM path by name.

| Patch | Change |
|---|---|
| `0001-soc-timer-c.patch` | TimerC `u_timer_c` at `0x80030000`, `irq_fast_i[4]` (IRQ 20); CV32E40P branch `core_irq[20]` too; `NrDevices` +1 |
| `0002-soc-pwm-ctr-size.patch` | `PwmCtrSize` becomes a `reference_system_core` parameter (default 8); the simulation top passes 20 |
| `0003-sim-top-papabench-env.patch` | simulation top instantiates `papabench_env` (`hw/rtl/papabench_env.sv`): `gp_i` and `uart_rx` come from it, `gp_o` feeds it; `uartdpi`'s TX output (host → RX) is left unconnected |
| `0004-soc-timers-d-e-gpio-irq.patch` | TimerD `u_timer_d` at `0x80040000` (`irq_fast_i[6]`, IRQ 22), TimerE `u_timer_e` at `0x80050000` (`irq_fast_i[7]`, IRQ 23), `gp_i[1]` on `irq_fast_i[5]` (IRQ 21); CV32E40P `core_irq[21..23]` too; `NrDevices` +2 |
| `0005-sim-top-pwm-monitor.patch` | simulation top instantiates `papabench_pwm_monitor` (`hw/rtl/papabench_pwm_monitor.sv`) on `pwm_o`: one `pwm.log` line per pulse (`<rise> <channel> <width>`, cycles since reset) |

`make sim` also copies `hw/rtl/*.sv` to `build/sim/hw/rtl/papabench/` and generates `papabench_stim.svh` there with `hw/stimulus/gen_stimulus.py`; both are listed in `hw/papabench_soc.core` (`files_simulation`). What the environment plays: `.claude/reference/port-harness.md` → Simulation environment.

Verified 2026-09-23 (patches 0001–0002, before the peripheral models existed): FBW and Autopilot `reference_system.log` and `Executed cycles` identical on the unpatched and patched models; with 0003 and the models, two consecutive runs of each program are cycle-identical; `make hwtest` passes (TimerC: 4/4 periodic IRQs, never early, max latency 18 cycles; TimerA/B quiet; `pwm_o[0]` 75000 high / 100001 period). 2026-09-24 (patches 0004–0005): `make hwtest` passes — TimerC/D/E 4/4 periodic IRQs each on its own line, never early, no other line (17–23) raised, max latency 48 cycles (heavier test ISR); modem clock → IRQ 21 once per edge with the `gp_o[6]` ack, IRQ 17 quiet; `pwm.log` has the 75000-cycle pulses. Note the PWM start-up: the counter starts when the period register is written, so the first pulse after programming is 2 cycles short.

To add a patch: copy the file(s) from the submodule into two trees `a/` and `b/`, edit `b/`, `diff -u`, rewrite the headers to `--- a/<path>` / `+++ b/<path>`, save as `hw/patches/NNNN-<name>.patch`; add any new file to `SOC_FILES` and to the `.core` file.

## Cycle counting

`mcycle`/`minstret` are readable via `PCOUNT_READ( mcycle, x )`; `pcount_enable( 1 )` clears `mcountinhibit`, `pcount_reset()` zeroes the counters. RV32 `mcycle` is 32-bit in `PCOUNT_READ`; `mcycleh` is needed for runs longer than ~4·10⁹ cycles (not expected per task).

## Traps

- The submodule's `CLAUDE.md` says "no compressed instructions" for Smtctx work, but the standard kit defaults to `rv32imc`. We use `rv32im` by decision, not by inheritance.
- The installed toolchain (`/tools/riscv/riscv32`, GCC 10.2) has only the `rv32imc` multilib: `-lgcc` pulls compressed code into an otherwise rv32im binary.
- `-nostdlib` drops libgcc too; soft-float and 64-bit division helpers are then undefined.
- Stock `link.ld` gives 56 KiB for code+data; the Autopilot with soft-float may need a harness-owned linker script up to the 128 KiB of HW RAM.
