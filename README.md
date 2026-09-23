TACLeBench on Ibex — PapaBench
==============================

This repository runs **PapaBench**, the parallel benchmark from
[TACLeBench](#upstream-tacle-benchmarks), on our own Ibex SoC. The SoC is the
reference system in the `Secure-Ibex/` submodule, built with the **standard
Ibex core** (no Smtctx isolation) and simulated with **Verilator**.

PapaBench models the software of a small UAV as two programs that run on two
separate microcontrollers: **Fly-By-Wire (FBW)** and **Autopilot**. Here they
are built and run **separately**. Each run executes one program on bare-metal
Ibex, under that program's original scheduler, and reports `mcycle` for every
task activation.

- `bench/` holds the upstream TACLeBench sources. It is read-only: PapaBench
  is compiled from it unmodified.
- `papabench_ibex/` holds everything this project adds: the Makefile, the
  harness, the AVR shim and one upstream bug-fix patch.
- `Secure-Ibex/` is a git submodule. It provides the RTL, the Verilator
  testbench, the bare-metal support library (`common/`, `crt0.S`, `link.ld`)
  and the FuseSoC cores. Nothing is written inside it, not even build outputs.


Quick start
-----------

```sh
git clone --recurse-submodules <this repo>
cd TACleBenchIbex/papabench_ibex

export PATH=/tools/riscv/riscv32/bin:$PATH   # riscv32-unknown-elf-gcc
source <path-to-venv>/bin/activate           # FuseSoC, see Prerequisites

make PROG=fbw run          # Fly-By-Wire
make PROG=autopilot run    # Autopilot
```

The first `run` builds the Verilator simulator from the submodule into
`papabench_ibex/build/sim/`. Every run then builds the chosen program,
simulates it until the configured number of scheduler ticks has elapsed, and
prints the results. With default settings FBW takes about 1.5 minutes of wall
time and the Autopilot about 2 minutes. The two runs can be launched in
parallel.


Prerequisites
-------------

These are the same tools the Secure-Ibex submodule needs; see
`Secure-Ibex/README.md` for full installation steps.

- **RISC-V GCC**: `riscv32-unknown-elf-gcc` on `PATH`. Tested with GCC 10.2
  from `/tools/riscv/riscv32`.
- **Verilator 5.014 or 5.017.** Other versions do not build the reference
  system.
- **FuseSoC** (lowRISC fork) in a Python venv. The simulator build checks the
  tools with `pip3 show edalize`, so the venv must be *activated*, not just
  have its `fusesoc` called by path:

  ```sh
  python3 -m venv .venv
  source .venv/bin/activate
  pip3 install -r Secure-Ibex/python-requirements.txt
  ```

- `srecord`, only for the optional `make vmem`.


Make targets and options
------------------------

Run from `papabench_ibex/`:

| Command | Effect |
|---|---|
| `make PROG=fbw` / `make PROG=autopilot` | Build `build/<prog>/<prog>.elf` |
| `make all-progs` | Build both programs |
| `make PROG=<prog> run` | Build, simulate and print `build/<prog>/reference_system.log` |
| `make sim` | (Re)build the Verilator model from `Secure-Ibex/` (FuseSoC target `sim_ibex`) into `build/sim/` |
| `make PROG=<prog> disassemble` | Write `build/<prog>/<prog>.dis` |
| `make PROG=<prog> vmem` | Write a `.vmem` image (not needed by Verilator) |
| `make PROG=<prog> clean` | Remove `build/<prog>/` |
| `make distclean` | Remove all of `build/`, simulator included |

| Variable | Default | Meaning |
|---|---|---|
| `PROG` | `fbw` | `fbw` or `autopilot` |
| `TICKS` | `61` | Scheduler ticks to simulate before reporting (about 1 s of flight). The Autopilot adds its 30 start-up ticks. |
| `TICK_CYCLES` | `819200` | Timer period in cycles: the AVR Timer2 overflow period, 16.384 ms, at 50 MHz |
| `OPT` | `-Os` | Optimisation level |
| `SIM` | `build/sim/sim_ibex-verilator/Vreference_system` | Simulator to use |
| `FUSESOC` | `fusesoc` | FuseSoC executable |

For a quick smoke test, use `make PROG=autopilot run TICKS=10 TICK_CYCLES=20000`.

All files a run produces are in `build/<prog>/`:

- `reference_system.log` holds the results, written through SimCtrl.
- `sim.log` holds the simulator's stdout.
- `uart0.log` and `reference_system_pcount.csv` are also written by the
  simulator.
- The build leaves the `.elf`, the `.map` and the instrumentation lists
  described below.


Output
------

```
PAPABENCH,<prog>,ticks=<n>,startup_ticks=<s>,tick_cycles=<c>
overhead,<cycles>
task,<name>,<count>,<min>,<max>,<avg>
...
END
```

- `count` is the number of times the upstream scheduler activated the task.
  A task that was never activated prints `0,0,0,0`.
- `min`, `max` and `avg` are `mcycle` cycles per activation. They include
  the measurement overhead printed on the `overhead` line, and exclude the
  cycles spent in the timer ISR body.

Results with default settings (`-Os`, 61 ticks):

| Program | Task | Activations | Cycles min / max / avg |
|---|---|---|---|
| FBW | `check_failsafe_task` | 22645 | 1652 / 1746 / 1658 |
| FBW | `check_mega128_values_task` | 22645 | 38 / 126 / 39 |
| FBW | `send_data_to_autopilot_task` | 22645 | 33 / 128 / 33 |
| FBW | `servo_transmit` | 0 | — (see Known limitations) |
| FBW | `test_ppm_task` | 22646 | 50 / 140 / 53 |
| Autopilot | `altitude_control_task` | 4 | 33 |
| Autopilot | `climb_control_task` | 4 | 35 |
| Autopilot | `link_fbw_send` | 20 | 34 / 75 / 36 |
| Autopilot | `navigation_task` | 4 | 1405 |
| Autopilot | `radio_control_task` | 0 | — (no radio input) |
| Autopilot | `receive_gps_data_task` | 0 | — (no GPS input) |
| Autopilot | `reporting_task` | 10 | 716 / 1365 / 935 |
| Autopilot | `stabilisation_task` | 20 | 1819 |

The measurement overhead is 23 cycles. FBW runs 50 M cycles and the
Autopilot 75 M cycles (30 start-up ticks + 61).


What was done
-------------

### 1. Building PapaBench for bare-metal RV32

- **ISA and libraries.** Both programs are built for `rv32im`/`ilp32` with no
  libc and no heap.
  - `libgcc` supplies soft-float and 64-bit division, since both programs use
    `float`/`double` and the core has no FPU.
  - `papabench_ibex/harness/runtime.c` provides `memcpy` and `memset`.
- **Start-up code.** The linker script, `crt0.S` and the UART/timer/SimCtrl
  drivers come from `Secure-Ibex/sw/Standard_Only_SW/Standard_Tests/`.
- **AVR device selection.**
  - FBW is compiled as an ATmega8 (`__AVR_ATmega8__`).
  - The Autopilot is compiled as an ATmega128 (`__AVR_ATmega128__`, plus
    `-DUBX` for the UBX GPS parser).
  - `PAPABENCH_SINGLE`, which would merge FBW into the Autopilot, is never
    defined.
- **Compiling 2000s-era C with GCC 10.** The upstream code needs
  `-fcommon` and `-fgnu89-inline`.
  - `autopilot/main.c` has a syntax error (`ModeUpdate(...); else`). It is
    fixed by `papabench_ibex/patches/autopilot_main_modeupdate.patch`, applied
    to a copy in the build directory.
  - `ad7714.c` and `gps_sirf.c` are unused and do not compile, so they are
    not built.
- **Flow facts.** The loop-bound and entry-point pragmas (WCET flow facts)
  are untouched.

### 2. AVR peripheral registers

The AVR code accesses its peripheral registers (`TIFR`, `SPDR`, `TCNT1`, …)
as memory at addresses 0x20–0xFF, which are unmapped on the Ibex SoC. The
header `papabench_ibex/include/arch/sfr_defs.h` is placed first on the
include path and shadows the upstream one. It redirects every register
access into a RAM array, `papabench_sfr[0x100]`, so the upstream sources
compile unchanged.

### 3. Each program's own scheduler, driven by the Ibex hardware timer

- **Upstream loops run unchanged.** The upstream `main()` of FBW
  (`fly_by_wire/main.c`) and of the Autopilot (`autopilot/mainloop.c`) are
  renamed at compile time and called by the harness.
- **How the loops are paced on AVR.** Both loops pace themselves through
  `timer_periodic()`, which polls the AVR Timer2 overflow flag (`TIFR.TOV2`).
- **Replacement time base.** The harness drives that flag from the Ibex
  machine timer at `0x80000000` (IRQ 7).
  - Every `TICK_CYCLES` cycles, the ISR sets a tick flag.
  - The shadow `sfr_defs.h` turns `bit_is_set( TIFR, TOV2 )` into a
    test-and-clear of that flag, emulating the AVR write-1-to-clear semantics.
- **Resulting behaviour.** The tasks run at their original rates.
  - FBW polls its tasks continuously.
  - The Autopilot runs `periodic_task()` once per tick: stabilisation and the
    FBW link every 3 ticks, reporting every 6, and navigation, altitude and
    climb every 15.

### 4. Per-task `mcycle` measurement

- **Instrumented functions.** Tasks are timed with
  `-finstrument-functions`, applied only to the first and last function of
  each task.
- **Excluded functions.** The Makefile excludes every other PapaBench
  function from instrumentation.
  - It compiles a plain build, lists its functions with `nm`, and removes the
    task boundaries declared in `harness/{fbw,autopilot}_glue.c`.
  - The result is `build/<prog>/exclude.txt`.
  - This keeps hooks out of the timed windows.
- **What the hooks record.** The hooks in `harness/harness.c` read `mcycle`
  and subtract the cycles the timer ISR spent inside the window.
- **Overhead.** `harness/calib.c` measures the hook overhead, which is
  printed on the `overhead` line.
- **Composite tasks.** Three Autopilot tasks do not exist as functions
  upstream: `navigation_task`, `reporting_task` and `receive_gps_data_task`.
  Their code is inlined in `periodic_task()` or `mainloop.c`, so each is
  timed from the first to the last call of its sequence.

### 5. Simulator from the submodule

`make sim` builds the standard-Ibex Verilator model (FuseSoC target
`sim_ibex`) from the `Secure-Ibex/` sources. It uses FuseSoC's
`--build-root`, so the output goes to `papabench_ibex/build/sim/` and the
submodule stays clean.


Known limitations
-----------------

- **`servo_transmit` is never activated.** This is an upstream defect: FBW's
  `main()` resets `_20Hz` before `fbw_schedule()` can see it reach 3. The
  port keeps the upstream behaviour.
- **No input stimulus.**
  - No radio, SPI or GPS data arrives, so `radio_control_task` and
    `receive_gps_data_task` are never activated.
  - Several other tasks take their short path. For example,
    `altitude_control_task` is short because the flight mode is never AUTO2.
- **Samples that include a tick.** A sample during which the tick fired also
  includes the ISR entry/exit cost (register save/restore, `mret`). That is
  why short FBW tasks show a `max` about 90 cycles above `min`.
- **Compressed instructions in libgcc.** The installed toolchain only ships
  an `rv32imc` libgcc, so its soft-float helpers contain compressed
  instructions.
- **`pp_sqrt()`.** Its upstream body is under `#if 0` and it returns an
  undefined value; it is kept as is.
- **Other AVR timer registers.** Registers other than the tick flag (for
  example `TCNT1`) are plain RAM and do not advance.
- **Memory.** The Autopilot uses about 54.7 KB of the 56 KiB RAM region of
  the stock `link.ld`.
- **Timer choice.** The Secure-Ibex SoC also has TimerA (`0x80010000`) and
  TimerB (`0x80020000`, fast IRQ 18/19). They are unused; the harness uses
  the machine timer.


Repository layout
-----------------

| Path | Contents |
|---|---|
| `papabench_ibex/Makefile` | Build, instrumentation lists, simulator build, run |
| `papabench_ibex/include/arch/sfr_defs.h` | AVR register shim and tick test-and-clear |
| `papabench_ibex/harness/harness.c` | `main()`, timer ISR, instrumentation hooks, report |
| `papabench_ibex/harness/{fbw,autopilot}_glue.c` | Task tables (`PAPABENCH_TASK( name, first, last )`) |
| `papabench_ibex/harness/runtime.c` | Register array, tick flag, `memcpy`/`memset` |
| `papabench_ibex/harness/calib.c` | Empty instrumented function for the overhead |
| `papabench_ibex/patches/` | Upstream bug fix applied to a build copy |
| `bench/parallel/PapaBench/` | Upstream PapaBench (read-only) |
| `Secure-Ibex/` | Ibex SoC submodule |
| `CLAUDE.md`, `.claude/` | Project notes: design details, decisions and status |


Upstream: TACLe Benchmarks
==========================

<a id="upstream-tacle-benchmarks"></a>

The rest of this file is the upstream TACLeBench README.

About TACLe Benchmarks
----------------------

This is the starting point for the repository of the TACLe benchmark
collection.

Documentation
-------------

The TACLe benchmark collection has been described in a paper at the
WCET 2016 workshop. The paper is included as PDF in the doc folder.
If you use the TACLe benchmarks please cite following paper:

    @INPROCEEDINGS{TACLeBench,
      author = {Heiko Falk and Sebastian Altmeyer and Peter Hellinckx and Bj{\"o}rn
    	Lisper and Wolfgang Puffitsch and Christine Rochange and Martin Schoeberl
    	and Rasmus Bo S{\o}rensen and Peter W{\"a}gemann and Simon Wegener},
      title = {{TACLeBench}: A Benchmark Collection to Support Worst-Case Execution
    	Time Research},
      booktitle = {16th International Workshop on Worst-Case Execution Time Analysis
    	(WCET 2016)},
      year = {2016},
      editor = {Martin Schoeberl},
      volume = {55},
      series = {OpenAccess Series in Informatics (OASIcs)},
      pages = {2:1--2:10},
      address = {Dagstuhl, Germany},
      publisher = {Schloss Dagstuhl--Leibniz-Zentrum f\"ur Informatik}
    }


Getting Started
---------------

All benchmarks are self-contained and can be compiled just with

    cc/gcc/clang *.c

Current Status
--------------

the TACLeBench group is currently in the process of cleaning up and unifying
the benchmarks. A version 2.0 will be available in the relative near future.

The WCET paper is based on the version V1.9. To switch to this version use:
```
git checkout V1.9
```

Regression Tests
----------------

https://www4.cs.fau.de/Research/TACLeBench

Getting Involved
----------------

You are welcome to contribute and help. To get involved contact:

Heiko for the mailing list (http://www.tuhh.de/es/esd/people/hfalk) and
Martin for GitHub access (http://www2.imm.dtu.dk/~masca/).
