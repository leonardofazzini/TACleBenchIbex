# Ibex platform (what we use from the `Secure-Ibex/` submodule)

The submodule is not edited from this repo. This file describes only the parts the PapaBench port consumes: the **standard Ibex** reference system (no Smtctx), its bare-metal software kit, and the Verilator flow. Paths below are relative to `Secure-Ibex/`.

## SoC memory map (standard target)

From `sw/Standard_Only_SW/Standard_Tests/common/reference_system_regs.h` and `README.md`:

| Region | Base | Notes |
|---|---|---|
| RAM | `0x00100000` | 128 KiB in HW; the stock `link.ld` uses 56 KiB code/data + 8 KiB stack |
| SimCtrl | `0x00020000` | Verilator only; `SIM_CTRL_OUT` (+0x0) prints a char, `SIM_CTRL_CTRL` (+0x8) halts |
| Timer | `0x80000000` | `mtime`/`mtimecmp`, IRQ 7; drives the PapaBench schedulers |
| TimerA / TimerB | `0x80010000` / `0x80020000` | fast IRQ, `mip` bits 18/19; present in the simulator built from the submodule (`make sim`), unused by the harness |
| UART0 | `0x80001000` | IRQ 16 |
| GPIO | `0x80002000` | |
| PWM | `0x80003000` | 12 channels |
| Debug module | `0x1A110000` | |

Nothing is mapped at 0x0–0xFF. Bus behaviour on an unmapped access: `TODO: verify` (see `rtl/system/` in the submodule).

## Bare-metal software kit (`sw/Standard_Only_SW/Standard_Tests/`)

- `common/common.mk`: compiles `common/*.c` + `$(PROGRAM).c` + `$(EXTRA_SRCS)` + `$(CRT)` with `riscv32-unknown-elf-gcc`, `CFLAGS ?= -march=$(ARCH) -mabi=ilp32 -static -mcmodel=medany -Wall -g -Os -fvisibility=hidden -nostdlib -nostartfiles -ffreestanding $(PROGRAM_CFLAGS)`, links with `-T $(LINKER_SCRIPT) … $(LIBS)`; produces `.elf`, `.bin`, `.vmem` (the latter needs `srec_cat`). `ARCH ?= rv32imc` — override to `rv32im`. `LIBS` is empty by default → add `-lgcc`.
- `common/link.ld`: `ram` at `0x00100000` length `0xE000`, `stack` at `0x0010E000` length `0x2000`; `ENTRY(_vectors_start + 0x80)`; `tohost = 0x20008`.
- `common/reference_system_common.[ch]`: `putchar`, `puts`, `puthex`, `sim_halt()` (writes 1 to `SIM_CTRL_CTRL`), `pcount_reset()`, `pcount_enable()`, `PCOUNT_READ(mcycle, dst)`, `install_exception_handler()`. Uses `<stdint.h>` types.
- `common/timer.[ch]`, `uart.[ch]`, `gpio.[ch]`, `pwm.[ch]`: peripheral drivers.
- `hello_test/`: the template — `Makefile` sets `PROGRAM`, `CRT ?= crt0.S`, includes `../common/common.mk`; `crt0.S` holds the vector table (`simple_exc_handler`, `simple_timer_handler`), sets `sp = _stack_start`, clears `.bss`, calls `main`.

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

The simulator writes into its **current directory** (verified 2026-09-23): `reference_system.log` = everything written to SimCtrl (`putchar`/`puts` of `common/`, so the harness results); `uart0.log` = UART0 output (also exposed on a `/dev/pts/N`); `reference_system_pcount.csv` = Ibex performance counters; stdout = banner, `Executed cycles`, performance counter summary. A write of 1 to `SIM_CTRL_CTRL` (`sim_halt()`, or `crt0.S` after `main` returns) ends the run.

This project builds the model from the submodule sources with `fusesoc --cores-root=<repo>/Secure-Ibex run --target=sim_ibex --setup --build --build-root=papabench_ibex/build/sim riscv:soc:reference_system --verilator_options=-Wno-fatal` (`make sim`), so the output is `papabench_ibex/build/sim/sim_ibex-verilator/Vreference_system` and the submodule stays clean (verified with `git -C Secure-Ibex status`, 2026-09-23). The FuseSoC venv must be activated: the pre-build `util/check_tool_requirements.py` runs `pip3 show edalize` from `PATH` and fails otherwise.

## Timer (`vendor/lowrisc_ibex_smtctx/shared/rtl/timer.sv`)

`mtime` increments by 1 every clock cycle (no prescaler); `timer_intr_o` is asserted while `mtime >= mtimecmp`. Registers: `mtime` +0x0/+0x4, `mtimecmp` +0x8/+0xC; the submodule version adds an interrupt-enable mask at +0x100 (reset = enabled). Driver (`common/timer.c`): `timer_read()` (64-bit, overflow-safe), `timecmp_update()` (writes low = −1, high, low to avoid a spurious match), `timer_enable()` + `simple_timer_handler` (not used by `papabench_ibex/`, which installs its own ISR on vector 7 with `install_exception_handler`).

The model built from the submodule (commit `b04c064`) contains `u_timer`, TimerA and TimerB. (The older prebuilt model in the sibling clone `~/01_Progetti_PC/Secure-Ibex/build/`, used before 2026-09-23, had only `u_timer`.) Results with the submodule model are identical to the prebuilt one for both programs.

## Cycle counting

`mcycle`/`minstret` are readable via `PCOUNT_READ( mcycle, x )`; `pcount_enable( 1 )` clears `mcountinhibit`, `pcount_reset()` zeroes the counters. RV32 `mcycle` is 32-bit in `PCOUNT_READ`; `mcycleh` is needed for runs longer than ~4·10⁹ cycles (not expected per task).

## Traps

- The submodule's `CLAUDE.md` says "no compressed instructions" for Smtctx work, but the standard kit defaults to `rv32imc`. We use `rv32im` by decision, not by inheritance.
- The installed toolchain (`/tools/riscv/riscv32`, GCC 10.2) has only the `rv32imc` multilib: `-lgcc` pulls compressed code into an otherwise rv32im binary.
- `-nostdlib` drops libgcc too; soft-float and 64-bit division helpers are then undefined.
- Stock `link.ld` gives 56 KiB for code+data; the Autopilot with soft-float may need a harness-owned linker script up to the 128 KiB of HW RAM.
