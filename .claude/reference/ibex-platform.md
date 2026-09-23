# Ibex platform (what we use from the `Secure-Ibex/` submodule)

The submodule is not edited from this repo. This file describes only the parts the PapaBench port consumes: the **standard Ibex** reference system (no Smtctx), its bare-metal software kit, and the Verilator flow. Paths below are relative to `Secure-Ibex/`.

## SoC memory map (standard target)

From `sw/Standard_Only_SW/Standard_Tests/common/reference_system_regs.h` and `README.md`:

| Region | Base | Notes |
|---|---|---|
| RAM | `0x00100000` | 128 KiB in HW; the stock `link.ld` uses 56 KiB code/data + 8 KiB stack |
| SimCtrl | `0x00020000` | Verilator only; `SIM_CTRL_OUT` (+0x0) prints a char, `SIM_CTRL_CTRL` (+0x8) halts |
| Timer | `0x80000000` | `mtime`/`mtimecmp`, IRQ 7 |
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

`common.mk` locates `common/` relative to itself, so a Makefile outside the submodule can `include` it by path and set `PROGRAM`, `EXTRA_SRCS`, `PROGRAM_CFLAGS`, `ARCH`, `LIBS`, `LINKER_SCRIPT`, `CRT`. Whether its `$(PROGRAM).c` convention and relative-path object placement work cleanly from outside is `TODO: verify` when the harness Makefile is written.

## Verilator flow (from `Secure-Ibex/README.md`)

Build the standard-Ibex model once, from `Secure-Ibex/`:

```
fusesoc --cores-root=. run --target=sim_ibex --setup --build riscv:soc:reference_system --verilator_options=-Wno-fatal
```

Run an ELF:

```
./build/riscv_soc_reference_system_0/sim_ibex-verilator/Vreference_system --meminit=ram,<path/to/prog.elf> [-t]
```

`-t` dumps a waveform. Requires Verilator 5.014 or 5.017 and the `.venv` with FuseSoC (see the README). UART output appears on the simulator's console/log — exact location `TODO: verify`.

## Cycle counting

`mcycle`/`minstret` are readable via `PCOUNT_READ( mcycle, x )`; `pcount_enable( 1 )` clears `mcountinhibit`, `pcount_reset()` zeroes the counters. RV32 `mcycle` is 32-bit in `PCOUNT_READ`; `mcycleh` is needed for runs longer than ~4·10⁹ cycles (not expected per task).

## Traps

- The submodule's `CLAUDE.md` says "no compressed instructions" for Smtctx work, but the standard kit defaults to `rv32imc`. We use `rv32im` by decision, not by inheritance.
- `-nostdlib` drops libgcc too; soft-float and 64-bit division helpers are then undefined.
- Stock `link.ld` gives 56 KiB for code+data; the Autopilot with soft-float may need a harness-owned linker script up to the 128 KiB of HW RAM.
