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
task activation and every interrupt handler call. The AVR peripherals the
programs were written for are modelled on the SoC's real peripherals: the
upstream interrupt handlers run from real Ibex interrupts (timers, UART,
GPIO), the servos drive the PWM, and radio, GPS and modem signals come from a
deterministic simulation environment.

- `bench/` holds the upstream TACLeBench sources. It is read-only: PapaBench
  is compiled from it unmodified.
- `papabench_ibex/` holds everything this project adds: the Makefile, the
  harness, the AVR shim and peripheral models, one upstream bug-fix patch,
  the SoC hardware patches and the simulation environment.
- `Secure-Ibex/` is a git submodule. It provides the RTL, the Verilator
  testbench, the bare-metal support library (`common/`, `crt0.S`) and the
  FuseSoC cores. Nothing is written inside it, not even build outputs; our
  hardware changes are patches applied to a copy.


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

The first `run` builds the Verilator simulator from the submodule, plus our
SoC patches, into `papabench_ibex/build/sim/`. Every run then builds the chosen program,
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
| `make sim` | (Re)build the Verilator model from `Secure-Ibex/` + `hw/patches/` (FuseSoC target `sim_ibex`) into `build/sim/` |
| `make hwtest` | Smoke test of the SoC patches (TimerC IRQ 20, 20-bit PWM) on the simulator |
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
isr_overhead,<cycles>
isr,<name>,<count>,<min>,<max>,<avg>
...
END
```

- `count` is the number of times the upstream scheduler activated the task
  (for `isr`, the number of times the handler ran). A task that was never
  activated prints `0,0,0,0`.
- `min`, `max` and `avg` are `mcycle` cycles per activation. Task samples
  include the measurement overhead printed on the `overhead` line, and
  exclude the cycles spent in every interrupt handler body (scheduler tick
  and peripheral models). They do include the trap entry/exit of those
  interrupts.
- `isr` samples time the upstream handler (`__vector_N`) alone, plus the
  `isr_overhead`.

Results with default settings (`-Os`, 61 ticks). Two consecutive runs give
identical results, cycle for cycle.

| Program | Task | Activations | Cycles min / max / avg |
|---|---|---|---|
| FBW | `check_failsafe_task` | 18917 | 34 / 2696 / 1632 |
| FBW | `check_mega128_values_task` | 18918 | 39 / 2969 / 45 |
| FBW | `send_data_to_autopilot_task` | 18918 | 33 / 1599 / 39 |
| FBW | `servo_transmit` | 0 | — (see Known limitations) |
| FBW | `test_ppm_task` | 18918 | 46 / 5684 / 57 |
| Autopilot | `altitude_control_task` | 4 | 38 (take-off block, see Known limitations) |
| Autopilot | `climb_control_task` | 4 | 60 |
| Autopilot | `link_fbw_send` | 20 | 108 / 195 / 113 |
| Autopilot | `navigation_task` | 4 | 1910 / 2319 / 2143 |
| Autopilot | `radio_control_task` | 20 | 291 / 1492 / 424 |
| Autopilot | `receive_gps_data_task` | 12 | 282 / 7997 / 1798 |
| Autopilot | `reporting_task` | 10 | 716 / 1365 / 959 |
| Autopilot | `stabilisation_task` | 20 | 1807 / 1983 / 1906 |

| Program | Interrupt handler | Calls | Cycles min / max / avg |
|---|---|---|---|
| FBW | `radio_ppm(__vector_5)` | 400 | 41 / 64 / 54 |
| FBW | `servo(__vector_6)` | 701 | 53 / 65 / 54 |
| FBW | `spi(__vector_10)` | 461 | 92 / 157 / 153 |
| FBW | `uart_tx(__vector_13)` | 63 | 21 / 34 / 33 |
| FBW | `adc(__vector_14)` | 8821 | 49 / 71 / 56 |
| Autopilot | `modem(__vector_5)` | 4129 | 27 / 73 / 42 |
| Autopilot | `link_fbw_oc1a(__vector_12)` | 460 | 113 / 136 / 134 |
| Autopilot | `spi(__vector_17)` | 460 | 47 |
| Autopilot | `gps_uart1_rx(__vector_30)` | 470 | 34 / 76 / 62 |
| Autopilot | `adc(__vector_21)` | 12728 | 47 / 68 / 52 |

The measurement overhead is 23 cycles for tasks and 5 for handlers. FBW runs
50 M cycles and the Autopilot 75 M cycles (30 start-up ticks + 61).

Long Autopilot run (`make PROG=autopilot run TICKS=600`, about 10 s of
flight, 516 M cycles, about 14 minutes of wall time). After 8 s of flight the
upstream flight plan leaves its take-off block for altitude hold, so the
control tasks take their long paths:

| Task | Activations | Cycles min / max / avg |
|---|---|---|
| `altitude_control_task` | 39 | 38 / 416 / 57 |
| `climb_control_task` | 39 | 60 / 1668 / 140 |
| `link_fbw_send` | 200 | 108 / 453 / 113 |
| `navigation_task` | 39 | 1910 / 6300 / 2408 |
| `radio_control_task` | 200 | 291 / 1492 / 304 |
| `receive_gps_data_task` | 118 | 282 / 12881 / 2932 |
| `reporting_task` | 100 | 716 / 1365 / 915 |
| `stabilisation_task` | 200 | 1807 / 2053 / 1890 |

| Interrupt handler | Calls | Cycles min / max / avg |
|---|---|---|
| `modem(__vector_5)` | 41354 | 27 / 73 / 42 |
| `link_fbw_oc1a(__vector_12)` | 4600 | 113 / 136 / 134 |
| `spi(__vector_17)` | 4600 | 47 |
| `gps_uart1_rx(__vector_30)` | 3854 | 34 / 76 / 66 |
| `adc(__vector_21)` | 88091 | 47 / 68 / 52 |


What was done
-------------

### 1. Building PapaBench for bare-metal RV32

- **ISA and libraries.** Both programs are built for `rv32im`/`ilp32` with no
  libc and no heap.
  - `libgcc` supplies soft-float and 64-bit division, since both programs use
    `float`/`double` and the core has no FPU.
  - `papabench_ibex/harness/runtime.c` provides `memcpy` and `memset`.
- **Start-up code.** `crt0.S` and the UART/timer/SimCtrl drivers come from
  `Secure-Ibex/sw/Standard_Only_SW/Standard_Tests/`. The linker script,
  `papabench_ibex/link.ld`, keeps the stock layout but uses the whole 128 KiB
  of RAM: the Autopilot with its peripheral models no longer fits in 56 KiB.
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
compile unchanged. One register needs more: on the AVR, `SPDR` is a transmit
register for writes and a receive buffer for reads, and both SPI handlers
write the next byte and then read the received one. Every `SPDR` access
therefore returns a fresh slot that holds the last received byte.

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

### 5. Simulator from the submodule, with our SoC patches

`make sim` builds the standard-Ibex Verilator model (FuseSoC target
`sim_ibex`) from the `Secure-Ibex/` sources. It uses FuseSoC's
`--build-root`, so the output goes to `papabench_ibex/build/sim/` and the
submodule stays clean.

The SoC hardware is extended without touching the submodule:

1. `make sim` copies the SoC wrapper, its peripherals, the simulation top and
   the Verilator testbench from `Secure-Ibex/` into `build/sim/hw/`.
2. It applies `papabench_ibex/hw/patches/*.patch` there, in name order. A
   patch that no longer applies (for example after a submodule update)
   stops the build.
3. It builds our cores `papabench:soc:reference_system[_core]`
   (`papabench_ibex/hw/*.core`) from that copy. Everything else (Ibex, bus,
   RAM, timers, DPI) comes unmodified from the submodule's cores.

Current patches:

| Patch | Change |
|---|---|
| `0001-soc-timer-c.patch` | TimerC at `0x80030000`, fast IRQ 20 (`mip` bit 20), same `timer.sv` as the other timers |
| `0002-soc-pwm-ctr-size.patch` | PWM counter width becomes a parameter (default 8, as before); the simulation top sets 20 bits, enough for a 20 ms servo period at 50 MHz |
| `0003-sim-top-papabench-env.patch` | The simulation top instantiates our environment, `papabench_ibex/hw/rtl/papabench_env.sv`, which drives the GPIO inputs and the UART RX line (section 6) |

`make hwtest` checks them: TimerC raises exactly its periodic interrupts on
IRQ 20 and never before the compare time, TimerA/TimerB stay quiet, and
`pwm_o[0]` on the waveform has the programmed pulse (75000 cycles) and
period (100001 cycles). With patches 0001 and 0002 only, both PapaBench
programs gave cycle-identical results on the patched and on the unpatched
model.

### 6. Real interrupts and peripherals

The upstream code only reads and writes its AVR registers. A per-program
model (`harness/fbw_periph.c`, `harness/autopilot_periph.c`, on top of
`harness/periph.c`) reads what the code wrote there and drives the real SoC.
The upstream interrupt handlers (`__vector_N`, plain C functions on RISC-V)
run from real Ibex interrupts:

| AVR source | FBW handler | Autopilot handler | On the Ibex SoC |
|---|---|---|---|
| Timer1 compare | servo (`__vector_6`) | FBW link byte pacing (`__vector_12`) | TimerA, IRQ 18 |
| ADC | `__vector_14` | `__vector_21` (IR sensors) | TimerB, IRQ 19 |
| SPI | slave (`__vector_10`) | master (`__vector_17`) | TimerC, IRQ 20 |
| UART TX | boot string (`__vector_13`) | — | TimerC + real UART TX (`uart0.log`) |
| Radio PPM input capture | `__vector_5` | — | environment → GPIO, IRQ 17 |
| Modem clock (INT4) | — | downlink bits (`__vector_5`) | environment → GPIO, IRQ 17 |
| GPS (UART1 RX) | — | UBX parser (`__vector_30`) | environment → real UART RX, IRQ 16 |
| Servo outputs | 10 channels | — | PWM channels 0–9, real 1–2 ms pulses |

- **When a model acts.** Models look at the registers at deterministic
  synchronisation points: at the end of every interrupt handler, and on
  every main-loop iteration (inside `timer_periodic()`). There they see, for
  example, that the code enabled the compare interrupt, restarted an ADC
  conversion or wrote a byte to transmit.
- **Timer1 counter.** The counters the handlers read (`TCNT1`, `TCNT2`,
  `ICR1`) are refreshed just before each handler runs. A compare the handler
  sets (`OCR1A = TCNT1 + 200`) is measured from the value the handler read,
  so the model's own cycles do not count as AVR time.
- **The other microcontroller is virtual.** FBW's model plays the Autopilot:
  it sends a command frame every 3 ticks over the SPI model, with the
  upstream checksum. The Autopilot's model plays FBW: it answers each frame
  with the radio status and switches the mode stick MANUAL → AUTO1 → AUTO2,
  full throttle.
- **Simulation environment.** `papabench_env.sv` sits in the Verilator top
  and plays, from the clock only, the tables generated by
  `hw/stimulus/gen_stimulus.py`:
  - FBW: the radio PPM train, 9 channels every 25 ms, mode MANUAL then AUTO,
    roll stick sweeping;
  - Autopilot: the modem clock (4800 Hz), and GPS UBX bursts (NAV-POSUTM,
    NAV-STATUS, NAV-VELNED) at 4 Hz on the UART, for a circle flight at
    200 m and 15 m/s.
  The GPIO line into the core is level-sensitive and the GPIO has no
  interrupt register, so the environment latches each edge; the handler
  acknowledges it by toggling a GPIO output bit. The program selects what
  the environment plays through two other GPIO output bits.

### 7. The simulated scenario

Every input the programs see is fixed in advance and replayed from the
clock, so every run is the same flight. Times below are simulated time
(50 MHz); one scheduler tick is 16.384 ms.

**Fly-By-Wire**

| Input | Source | What it contains |
|---|---|---|
| Radio (PPM) | `gen_stimulus.py` → environment → GPIO | One frame every 25 ms (first edge at 1 ms; sync gap at least 8 ms), 9 channels. Throttle 1600 µs; roll stick sweeping 1300 → 1900 → 1300 µs in steps of 75 µs, one step per frame (a 400 ms triangle); pitch, yaw and the other channels at 1500 µs; mode stick 1100 µs (MANUAL) for the first 20 frames (0.5 s), then 1900 µs (AUTO). The 40-frame table (1 s) repeats. |
| Autopilot commands (SPI) | `fbw_periph.c`, `fbw_spi_frame()` | One 23-byte frame every 3 ticks (49 ms, the Autopilot's `link_fbw_send()` rate), the first after 1 tick. Throttle `MAX_PPRZ`; roll sweeping −`MAX_PPRZ`/4 … +`MAX_PPRZ`/4 over 16 frames; pitch `MAX_PPRZ`/10; status "autopilot OK"; upstream XOR checksum. |
| ADC | `fbw_periph.c`, `fbw_adc_sample()` | Channel 3 (supply) 629 = 11.1 V; channel 6 (servo supply) 281 = 5.0 V; other channels 0. One conversion every 13 × 128 AVR clocks (104 µs). |

FBW therefore starts in MANUAL mode, driving the servos from the radio. The
mode channel is averaged over 10 frames (upstream `AVERAGING_PERIOD`), so
FBW switches to AUTO at the first average taken over AUTO frames only,
about 0.75 s in; from then on it drives the servos from the Autopilot's
commands.

**Autopilot**

| Input | Source | What it contains |
|---|---|---|
| FBW status (SPI) | `autopilot_periph.c`, `ap_spi_frame()` | The answer to each `link_fbw_send()` frame (every 3 ticks): radio OK with averaged channels; mode stick MANUAL for frames 0–1, AUTO1 for frames 2–3, AUTO2 from frame 4 (after about 0.7 s); throttle `MAX_PPRZ` (above the take-off threshold); `ppm_cpt` 40; supply 11.1 V; upstream XOR checksum. |
| GPS (UBX on the UART) | `gen_stimulus.py` → environment → UART RX | One burst every 250 ms from 250 ms: NAV-POSUTM, NAV-STATUS, NAV-VELNED (94 bytes at 115200 baud), with valid UBX checksums. 3D fix; a counter-clockwise circle of radius 80 m centred 60 m east and 40 m south of the flight-plan origin (`NAV_UTM_EAST0/NORTH0`), at 15 m/s ground speed and 200 m altitude, no climb. The 120-epoch table (30 s) repeats, so after 30 s the position jumps back to the start of the circle. |
| Modem clock | environment → GPIO | A 4800 Hz square wave; the handler sends one downlink bit per falling edge while it has data. |
| Infrared sensors (ADC) | `autopilot_periph.c`, `ap_adc_sample()` | Channels 1 and 2: the level-attitude values 402 and 512 (from the airframe's IR neutrals), both plus a roll oscillation of ±20 counts, a triangle with a 2 s period. Other channels 0. One conversion every 104 µs. |

The Autopilot therefore spends its 30-tick start-up wait (0.5 s), goes from
MANUAL through AUTO1 to AUTO2, launches (AUTO2 with full throttle), and
starts its flight time as soon as the GPS speed exceeds the take-off speed.
The upstream flight plan then holds its take-off block for 8 s of flight
time before switching to altitude hold, which is why the default run
(1.5 s) does not reach the long path of `altitude_control_task` and the
long run (`TICKS=600`, about 10 s) does.

To change the scenario:

- radio and GPS: edit `papabench_ibex/hw/stimulus/gen_stimulus.py`, then
  run `make sim` (the tables are compiled into the simulator);
- the other microcontroller's frames and the ADC values: edit
  `ap_spi_frame()`, `fbw_spi_frame()`, `ap_adc_sample()` or
  `fbw_adc_sample()` in `papabench_ibex/harness/`, then rebuild the program.


Known limitations
-----------------

- **`servo_transmit` is never activated.** This is an upstream defect: FBW's
  `main()` resets `_20Hz` before `fbw_schedule()` can see it reach 3. The
  port keeps the upstream behaviour.
- **Fixed scenario.** The radio, GPS, ADC values and the other
  microcontroller's frames are one deterministic scenario (see section 7).
  Other flights need a new `gen_stimulus.py` table (then `make sim`) or new
  frames in the models.
- **`altitude_control_task` stays short in the default run.** The upstream
  flight plan holds the take-off block until 8 s of flight time; only then
  does it switch to altitude hold. A default run simulates 1.5 s; the long
  run above (`TICKS=600`) covers it.
- **Task samples include the entry and exit cost of the interrupts that hit
  them.** This is deliberate: the samples are kept raw.
  - What is subtracted: the body of every interrupt handler that runs while
    a task is being timed (the scheduler tick, the peripheral model and the
    upstream `__vector_N` it calls).
  - What is not subtracted: the trap itself. That is the core taking the
    interrupt and jumping to the vector, the handler prologue and epilogue
    that save and restore the registers, and `mret`. The handler reads
    `mcycle` only once its prologue has run, so it cannot see this part.
    Before the peripheral models existed, when only the scheduler tick
    interrupted tasks, a task hit by the tick showed a `max` about 90 cycles
    above its `min`.
  - How often it happens: every interrupt that lands inside a task adds its
    trap cost to that one sample. The ADC interrupts every 104 µs (5200
    cycles), so a task longer than that is almost always hit at least once.
    The modem (Autopilot, 4800 Hz while it transmits), the PPM edges (FBW),
    the SPI bytes and the GPS bytes add more.
  - Effect on the results: `min` is the cost of the task with no interrupt
    inside it. `max` and `avg` include these trap costs, as they would on a
    real microcontroller with its interrupts enabled.
- **GPS messages dropped upstream.** The UBX parser and the modem share the
  globals `ck_a`/`ck_b` (merged by `-fcommon`), and the parser drops a
  message while the previous one is unread (for example during the
  Autopilot's start-up wait). In the default run 3 of the 15 GPS messages
  are lost; the port keeps this behaviour.
- **`SPDR` accesses cost a call.** Every access to `SPDR` calls a small
  function (see section 2), and a task that touches `SPDR`
  (`link_fbw_send`) pays for that call in its own sample.
- **Compressed instructions in libgcc.** The installed toolchain only ships
  an `rv32imc` libgcc, so its soft-float helpers contain compressed
  instructions.
- **`pp_sqrt()`.** Its upstream body is under `#if 0` and it returns an
  undefined value; it is kept as is.
- **AVR timer counters.** `TCNT1`, `TCNT2` and `ICR1` are refreshed only
  when a handler is about to run, because only handlers read them upstream.


Repository layout
-----------------

| Path | Contents |
|---|---|
| `papabench_ibex/Makefile` | Build, instrumentation lists, simulator build, run |
| `papabench_ibex/include/arch/sfr_defs.h` | AVR register shim and tick test-and-clear |
| `papabench_ibex/harness/harness.c` | `main()`, timer ISR, instrumentation hooks, report |
| `papabench_ibex/harness/{fbw,autopilot}_glue.c` | Task tables (`PAPABENCH_TASK( name, first, last )`) |
| `papabench_ibex/harness/runtime.c` | Register array, tick flag and idle synchronisation point, `memcpy`/`memset` |
| `papabench_ibex/harness/periph.{h,c}` | Peripheral-model layer: timer event channels, AVR time, Timer1 compare, GPIO, `SPDR` |
| `papabench_ibex/harness/{fbw,autopilot}_periph.c` | Per-program peripheral models and handler tables |
| `papabench_ibex/harness/ibex_io.h` | SoC register map and CSR helpers |
| `papabench_ibex/link.ld` | Linker script (whole 128 KiB RAM) |
| `papabench_ibex/harness/calib.c` | Empty instrumented function for the overhead |
| `papabench_ibex/patches/` | Upstream bug fix applied to a build copy |
| `papabench_ibex/hw/` | SoC hardware patches, our FuseSoC cores, simulation environment (`rtl/`), stimulus generator (`stimulus/`), `hwtest` smoke test |
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
