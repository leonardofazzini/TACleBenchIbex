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
| SPI master / SPI slave | `0x80004000` / `0x80005000` | reference SoC (`spi_master.sv`, `spi_slave.sv`, stock since submodule `d5e2fb5`); fast IRQ 20/21; Autopilot / FBW end of the inter-MCU link |
| TimerD / TimerE | `0x80040000` / `0x80050000` | **`system/papabench` branch** (`Secure-Ibex/rtl/system/reference_system_core.sv`); fast IRQ 22/23; Autopilot `link_fbw` compare / FBW virtual UART |
| UART0 | `0x80001000` | IRQ 16 |
| GPIO bank 0 / bank 1 | `0x80002000` / `0x80060000` | **`system/papabench` branch**: both are `gpio_irq.sv` (stock `gpio.sv` map OUT `+0x0`, IN `+0x4`, IN_DBNC `+0x8`, plus IRQ_EN `+0xC`, IRQ_STATUS `+0x10` write-1-to-clear, IRQ_FALL `+0x14`, IRQ_RISE `+0x18`); fast IRQ 17 / 24; FBW / Autopilot. Bank 1 has its own ports `gp1_i`/`gp1_o` |
| PWM | `0x80003000` | 12 channels, no IRQ, no read-back; channel `i`: pulse width at `+8i`, period (counter max) at `+8i+4`; high for `width` cycles out of `max+1`. Counter is **20 bits** on `system/papabench` (8 on the reference SoC) |
| Debug module | `0x1A110000` | |

The reference SoC (`master`) has the first block only (down to SPI master/slave). Everything below the line comes from the Secure-Ibex `system/papabench` branch (`Secure-Ibex/README.md`, `doc/Branching.md` there): our own SoC, not local patches — `papabench_ibex/` never edits or patches `Secure-Ibex/`.

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

## SoC patches (now a submodule branch, `system/papabench`)

Until 2026-09-25 the extra hardware below was a series of patches in `papabench_ibex/hw/patches/`, applied by `make sim` to a copy of the submodule sources under `build/sim/hw/`. Secure-Ibex `3fbd2ea` ("Branch-friendly version of the system") introduced a branching model where each non-reference SoC lives on its own long-lived `system/<name>` branch, merging `master` for core updates (`Secure-Ibex/doc/Branching.md`); the patch series was rebuilt as commits on a new `system/papabench` branch of the submodule (`Secure-Ibex/README.md` describes that branch's system) and `papabench_ibex/hw/` was removed. **The submodule is still never edited from this repo** — hardware changes are commits on `system/papabench`, made and reviewed inside `Secure-Ibex/`, following that repo's own `CLAUDE.md`.

`make sim`/`make sim-dual` (`papabench_ibex/Makefile`) refuse to build (`check-branch`) unless the submodule is checked out to `system/papabench`; `make checkout-branch` does `git -C Secure-Ibex switch system/papabench`. Both then run FuseSoC directly on the submodule, no local staging or patching:

- `make sim`: `fusesoc --cores-root=Secure-Ibex run --target=sim_ibex --build-root=papabench_ibex/build/sim riscv:soc:reference_system` (that branch's own core, replacing `papabench:soc:reference_system`).
- `make sim-dual`: same, `riscv:soc:papabench_dual` (`papabench_dual.core`, root of the branch), building `Vpapabench_dual` into `build/sim-dual/`.

What `system/papabench` adds to the reference SoC, all in `rtl/system/reference_system_core.sv` there (`NrDevices` 12→15) unless noted:

| Addition | Change |
|---|---|
| 20-bit PWM | `PwmCtrSize` parameter (was a fixed-8 `localparam`); the branch's own `rtl/top_simulation/reference_system.sv` and `papabench_dual.sv` pass 20 |
| TimerD / TimerE | `u_timer_d` at `0x80040000` (`irq_fast_i[6]`, IRQ 22), `u_timer_e` at `0x80050000` (`irq_fast_i[7]`, IRQ 23) |
| GPIO bank 0 / bank 1 | `gpio.sv` → `gpio_irq.sv` (new file) on both instances: bank 0 (`u_gpio`) now drives `irq_fast_i[1]` = IRQ 17 (was the raw `gp_i[0]`); `u_gpio1` is new, bank 1 at `0x80060000`, ports `gp1_i`/`gp1_o`, `irq_fast_i[8]` = IRQ 24 |
| `SimCtrlLogName` | parameter of `reference_system_core` (default `"reference_system.log"`), passed to `simulator_ctrl`'s `LogName` — lets `papabench_dual.sv`'s two SoC instances each write their own log |
| Deterministic environment | the branch's `rtl/top_simulation/reference_system.sv` instantiates `papabench_env.sv` (drives both GPIO banks and `uart_rx`; `uartdpi`'s TX output, host → RX, is left unconnected) and `papabench_pwm_monitor.sv` (`pwm_o` → `pwm.log`, one line per pulse); `papabench_dual.sv` does the same for its two SoCs |

`papabench_dual.sv` (module `papabench_dual`, on the branch) instantiates `reference_system_core` twice: `u_fbw` (`SimCtrlLogName("fbw.log")`, SPI slave, GPIO bank 0) and `u_autopilot` (`"autopilot.log"`, SPI master, GPIO bank 1), same clock/reset, linked by `u_autopilot`'s SPI master → `u_fbw`'s SPI slave (MISO pulled up when not driven; the other two SPI ends tied off). One `papabench_env` drives the FBW's bank 0 and the Autopilot's bank 1 (the other bank of each SoC unconnected); GPS on the Autopilot's `uart_rx`, the FBW's idles high; the PWM monitor watches the FBW's `pwm_o`. Ends when `gp_o[7]` of both banks is set (each harness sets it after printing its results) — a SimCtrl halt still ends it at once. Testbench `dv/papabench/papabench_dual.cc` (on the branch): RAMs registered as `fbw` (base 0) / `autopilot` (base `0x10000000`, disjoint areas only) → `--meminit=fbw,<elf> --meminit=autopilot,<elf>`.

The single-MCU top keeps the module name `reference_system` and the instance `u_reference_system`: the testbench C++ looks up `TOP.reference_system` and the RAM path by name — unchanged by the migration.

Stimulus tables (`papabench_stim.svh`, PPM/GPS) are generated on the branch by `rtl/top_simulation/gen_papabench_stim.py` and **committed** there (not regenerated by `make sim`/`make sim-dual` any more): changing the scenario means running that script and committing both files on `system/papabench`, then `make sim`/`make sim-dual` here. What the environment plays: `.claude/reference/port-harness.md` → Simulation environment.

**Verified 2026-09-25 (migration to `system/papabench`, submodule `d5e2fb5` → `3fbd2ea`):** `fbw`, `autopilot` (both also `MEASURE=0`), `run-joint` and `run-joint MEASURE=0` produce bit-for-bit identical output on the branch-built simulators vs. the last patch-based build at submodule `d5e2fb5` — same `Executed cycles`, same per-task/per-ISR `,` lines, same `pwm.log`, same ELF `md5sum`. `hwtest` PASS on both (same TimerD/E/GPIO/SPI/PWM checks); its `pwm.log` differs by a constant 10-cycle offset from renaming the test's own banner string (`hw_smoke.c`, one byte shorter), not from the SoC — `check_pwm.sh`'s pulse/period assertions are unaffected. Comparison done by pointing `papabench_ibex/Makefile`'s `SIM`/`SIM_DUAL` at each build in turn, same `TICKS`/`TICK_CYCLES` defaults.

## Cycle counting

`mcycle`/`minstret` are readable via `PCOUNT_READ( mcycle, x )`; `pcount_enable( 1 )` clears `mcountinhibit`, `pcount_reset()` zeroes the counters. RV32 `mcycle` is 32-bit in `PCOUNT_READ`; `mcycleh` is needed for runs longer than ~4·10⁹ cycles (not expected per task).

## Traps

- The submodule's `CLAUDE.md` says "no compressed instructions" for Smtctx work, but the standard kit defaults to `rv32imc`. We use `rv32im` by decision, not by inheritance.
- The installed toolchain (`/tools/riscv/riscv32`, GCC 10.2) has only the `rv32imc` multilib: `-lgcc` pulls compressed code into an otherwise rv32im binary.
- `-nostdlib` drops libgcc too; soft-float and 64-bit division helpers are then undefined.
- Stock `link.ld` gives 56 KiB for code+data; the Autopilot with soft-float may need a harness-owned linker script up to the 128 KiB of HW RAM.
