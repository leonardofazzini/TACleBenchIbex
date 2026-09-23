# Conventions: build, run, debug, style, recipes

## Language

- Chat with the user: Italian.
- Code, comments, identifiers, docs, commit messages: English.

## Toolchain and flags

- Compiler: `riscv32-unknown-elf-gcc` (lowRISC toolchain; install notes in `Secure-Ibex/README.md`).
- Base flags (from Secure-Ibex `common.mk`, with our overrides): `-march=rv32im -mabi=ilp32 -static -mcmodel=medany -Wall -Wno-unknown-pragmas -g -Os -nostdlib -nostartfiles -ffreestanding`, link `-lgcc`.
- PapaBench include paths (before any system include): `bench/parallel/PapaBench/sw/include`, `sw/var/include`, `sw/airborne/<prog>`, `arch/include/avr`, `arch/include/avr/arch`, plus the harness directory if it shadows `sfr_defs.h`.
- Never `-DPAPABENCH_SINGLE`.

## Build and run (standard Ibex, Verilator)

1. Once: build the simulator from `Secure-Ibex/` — `fusesoc --cores-root=. run --target=sim_ibex --setup --build riscv:soc:reference_system --verilator_options=-Wno-fatal`.
2. Build a program: harness Makefile — `TODO: describe` once it exists.
3. Run: `Secure-Ibex/build/riscv_soc_reference_system_0/sim_ibex-verilator/Vreference_system --meminit=ram,<prog>.elf`.
4. Debug: add `-t` for a waveform; `riscv32-unknown-elf-objdump -d <prog>.elf` (or `make disassemble` via `common.mk`) to check that no compressed instructions and no AVR asm slipped in.

Host sanity check of the upstream sources (not of the port): `bench/checkBenchmark.sh` (native `gcc`, `-Werror`).

## Coding style

- New C in the harness follows TACLeBench style (`doc/code_formatting.txt`): 2-space indent, no tabs, spaces inside `( )` and `[ ]`, 80 columns, ANSI prototypes, two blank lines between functions, global symbols prefixed with the module name.
- No libc, no `malloc`, no `printf`: print with Secure-Ibex `puts`/`puthex`.
- `uint32_t` from `<stdint.h>` only in files that do not include PapaBench headers (see `port-harness.md`, type clash).

## Recipes

### Measure one more task

1. Confirm the entry point in `bench/parallel/PapaBench/PapaBench_for_wcet.txt` and its call site (`reference/papabench.md`).
2. Add it to the harness measurement list (mechanism `TODO: describe` once chosen in `port-harness.md`).
3. Rebuild, run on Verilator, check the new line in the output.
4. Record the step in `.claude/status.md`.

### Add a new harness file

1. Create it under the harness directory.
2. Add its line to `.claude/tree.md` immediately.
3. If it changes how the port works, update `.claude/reference/port-harness.md` in the same step.

### Port another TACLeBench benchmark later

1. Check whether it is self-contained (`bench/<cat>/<name>/*.c`, `main()` returns 0 on success) — most kernel/sequential ones are; PapaBench is the exception.
2. Reuse the harness build flags; the benchmark's `main()` return value is its correctness check.
