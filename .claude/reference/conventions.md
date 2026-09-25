# Conventions: build, run, debug, style, recipes

## Language

- Chat with the user: Italian.
- Code, comments, identifiers, docs, commit messages: English.

## Toolchain and flags

- Compiler: `/tools/riscv/riscv32/bin/riscv32-unknown-elf-gcc` (crosstool-NG, GCC 10.2). Single multilib, `rv32imc`/`ilp32`: `libgcc.a` contains compressed instructions.
- All flags live in `papabench_ibex/Makefile` (`BASE_CFLAGS`, `PB_CFLAGS`, `IBEX_CFLAGS`, `LDFLAGS`); see `port-harness.md` for why each PapaBench flag is there.
- Never `-DPAPABENCH_SINGLE`.

## Build and run (standard Ibex, Verilator)

From `papabench_ibex/`:

- `make PROG=fbw` / `make PROG=autopilot` / `make all-progs` → `build/<prog>/<prog>.elf` (+ `.map`, `instrumented.txt`, `exclude.txt`).
- `make PROG=<prog> run` → single-MCU run (other MCU virtual): runs the simulator in `build/<prog>/` and prints `reference_system.log` (the results). Simulator stdout (statistics, performance counters) is in `sim.log`.
- `make run-joint` → joint run: builds both programs with `JOINT=1` (FBW with `TICKS` + 30), runs `Vpapabench_dual` in `build/joint/` and prints `fbw.log` + `autopilot.log`; `sim.log`, `pwm.log` (FBW) there too. A `JOINT=1` ELF never ends on the single-MCU simulator: `make run` refuses `JOINT=1`.
- Variables: `TICKS=<n>` (scheduler ticks, default 61; the Autopilot adds 30 startup ticks), `TICK_CYCLES=<n>` (timer period, default 819200), `OPT=-O2` etc., `SIM=<path>`, `SIM_DUAL=<path>`, `MEASURE=0` (no instrumentation, no calibration, no timing in the IRQ wrappers, output only header `,measure=0` + `link,` lines + `END`; objects in `build/<prog>[-joint]-nomeasure/`, so the builds never mix), `JOINT=1` (build for the two-MCU model, `build/<prog>-joint/`).
- Simulator: default `SIM` is `papabench_ibex/build/sim/sim_ibex-verilator/Vreference_system`, built from the `Secure-Ibex/` submodule sources plus `papabench_ibex/hw/patches/*.patch` (applied to a copy in `build/sim/hw/`, see `ibex-platform.md` → SoC patches) by `make sim` (which also stages `hw/rtl/` and regenerates `papabench_stim.svh` with `hw/stimulus/gen_stimulus.py`: any change there, or to `hw/patches/`, needs `make sim` and `make sim-dual`; `run`/`run-joint` only build their simulator when it is missing; `make sim-dual` builds the two-MCU model into `build/sim-dual/` from the same staging) with FuseSoC `--build-root`, so nothing is written inside the submodule. `make sim` needs the FuseSoC venv **activated** (the Secure-Ibex pre-build check runs `pip3 show edalize` from `PATH`) and Verilator 5.014/5.017 (5.014 installed). A venv exists at `~/01_Progetti_PC/Secure-Ibex/.venv` (sibling clone): `source ~/01_Progetti_PC/Secure-Ibex/.venv/bin/activate`. `FUSESOC=<path>` overrides the executable. `make distclean` also deletes the simulator.
- Wall time with defaults: FBW ~1.5 min (50 M cycles), Autopilot ~2 min (75 M cycles), joint ~4.5 min (75 M cycles on each of two cores, ~0.28 M cycles/s); they can run in parallel, except two runs of the same build directory (e.g. `PROG=autopilot run` with different `TICKS`: both write `build/autopilot/`). For a quick check use e.g. `TICKS=10 TICK_CYCLES=20000`.
- Debug: `make PROG=<prog> disassemble` → `build/<prog>/<prog>.dis`; run the simulator by hand with `-t` for a waveform. An exception prints `EXCEPTION!!!` with `MEPC`/`MCAUSE`/`MTVAL` in `reference_system.log` and halts.
- `make PROG=<prog> vmem` produces a `.vmem` image (not needed by Verilator).

Output format (`reference_system.log`):

```
PAPABENCH,<prog>,ticks=<n>,startup_ticks=<s>,tick_cycles=<c>[,measure=0][,joint=1]
overhead,<cycles>
task,<name>,<count>,<min>,<max>,<avg>
trap_overhead,<cycles>
isr_overhead,<cycles>
isr,<name>,<count>,<min>,<max>,<avg>
link,<counter>,<value>
END
```

`count` = activations by the upstream scheduler; cycles include `overhead`. A task never activated prints `0,0,0,0`.

Host sanity check of the upstream sources (not of the port): `bench/checkBenchmark.sh` (native `gcc`, `-Werror`).

## Coding style

- New C in the harness follows TACLeBench style (`doc/code_formatting.txt`): 2-space indent, no tabs, spaces inside `( )` and `[ ]`, 80 columns, ANSI prototypes, two blank lines between functions, global symbols prefixed with the module name.
- No libc, no `malloc`, no `printf`: print with Secure-Ibex `puts`/`puthex`.
- `uint32_t` from `<stdint.h>` only in files that do not include PapaBench headers (see `port-harness.md`, type clash).

## Recipes

### Measure one more task

1. Confirm the entry point in `bench/parallel/PapaBench/PapaBench_for_wcet.txt` and its call site (`reference/papabench.md`); check with `nm` that it exists as a function (some tasks were inlined upstream).
2. Declare the function(s) `void f( void );` in `harness/fbw_glue.c` or `harness/autopilot_glue.c` and add one line `PAPABENCH_TASK( "name", first, last ),` to `papabench_tasks[]` (single line, this exact form: the Makefile parses it). For a single function `first == last`; for a sequence inlined in the scheduler use its first and last call, after checking with `grep` that `first` is not called elsewhere.
3. `make PROG=<prog>` must pass the instrumentation checks; `cat build/<prog>/instrumented.txt` shows the hooked functions. `make PROG=<prog> run`, check the new `task,` line (count > 0 if the scheduler activates it).
4. Record the step in `.claude/status.md`; update the task list in `reference/port-harness.md` if the table changed meaning.

### Add a new harness file

1. Create it under the harness directory.
2. Add its line to `.claude/tree.md` immediately.
3. If it changes how the port works, update `.claude/reference/port-harness.md` in the same step.

### Port another TACLeBench benchmark later

1. Check whether it is self-contained (`bench/<cat>/<name>/*.c`, `main()` returns 0 on success) — most kernel/sequential ones are; PapaBench is the exception.
2. Reuse the harness build flags; the benchmark's `main()` return value is its correctness check.
