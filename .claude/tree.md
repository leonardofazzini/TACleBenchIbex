# tree.md — map of the repository

One line per file or directory: what it contains. Update it **after every file added, renamed, moved or deleted** (see `rules.md`).

Not listed: build outputs (`*.o`, `*.d`, `*.elf`, `*.bin`, `*.vmem`, `*.dis`, `a.out`, `output.map`), the Secure-Ibex FuseSoC `build/` directory, `.venv/`, and the internals of the `Secure-Ibex/` submodule except the files this project reads. Other TACLeBench benchmarks are listed per directory, not per file.

## Root

- `CLAUDE.md`: agent router — objective, hard constraints, routing table, invariants.
- `README.md`: project README — PapaBench on Ibex quick start, prerequisites, make targets/variables, output format, results, what was done, known limitations, layout; then the upstream TACLeBench README (citation).
- `.gitmodules`: declares the `Secure-Ibex` submodule (`git@github.com:leonardofazzini/Secure-Ibex.git`).

## `.claude/`

- `.claude/rules.md`: protocol for updating `tree.md`, `status.md` and reference files.
- `.claude/tree.md`: this map.
- `.claude/status.md`: per-task log, to-do list, completed history.
- `.claude/reference/papabench.md`: PapaBench structure, tasks, macros, AVR register layer, float math.
- `.claude/reference/ibex-platform.md`: what we use from Secure-Ibex — memory map, `common/` lib, crt0, link.ld, Verilator flow, `mcycle`.
- `.claude/reference/port-harness.md`: design of the PapaBench-on-Ibex port — SFR remap, timer emulation, per-task measurement, open decisions.
- `.claude/reference/conventions.md`: build/run/debug commands, coding style, recipes.

## `papabench_ibex/` (our port and harness)

- `papabench_ibex/Makefile`: builds `build/<prog>/<prog>.elf` for `PROG=fbw|autopilot` (rv32im, `-lgcc`, our `link.ld` + Secure-Ibex `crt0.S`; objects of `harness/periph.c` + `harness/<prog>_periph.c`), with selective `-finstrument-functions` (plain build → `nm` → exclude list); `sim` target stages Secure-Ibex SoC files into `build/sim/hw/`, adds `hw/rtl/` + the generated stimulus, applies `hw/patches/`, builds the Verilator model (`papabench:soc:reference_system`) into `build/sim/`; `hwtest` target builds and runs `hw/test/hw_smoke.c` with a waveform and `check_pwm.sh`; `run` target runs it; vars `TICKS`, `TICK_CYCLES`, `SIM`, `FUSESOC`, `OPT`.
- `papabench_ibex/.gitignore`: ignores `build/`.
- `papabench_ibex/link.ld`: linker script — stock Secure-Ibex layout (vectors, entry +0x80, tohost) over the whole 128 KiB RAM: 112 KiB code+data, 16 KiB stack.
- `papabench_ibex/include/arch/sfr_defs.h`: replaces the upstream AVR `sfr_defs.h` (first on the include path); maps every AVR register address to `papabench_sfr[ 0x100 ]` except `SPDR` (`PAPABENCH_SPDR_ADDR`), whose accesses go through `papabench_spdr_access()`; `_SFR_MEM_ADDR` returns the array offset; `bit_is_set( TIFR, TOV2 )` consumes the timer tick flag (`papabench_tick_take`).
- `papabench_ibex/hw/papabench_soc_core.core`: FuseSoC core `papabench:soc:reference_system_core` — the Secure-Ibex SoC RTL file list, read from the patched copy in `build/sim/hw/`.
- `papabench_ibex/hw/papabench_soc.core`: FuseSoC core `papabench:soc:reference_system`, target `sim_ibex` (same options as upstream) — patched SoC + standard Ibex + Verilator testbench.
- `papabench_ibex/hw/patches/0001-soc-timer-c.patch`: adds TimerC (`0x80030000`, fast IRQ 20) to `rtl/system/reference_system_core.sv`.
- `papabench_ibex/hw/patches/0002-soc-pwm-ctr-size.patch`: `PwmCtrSize` becomes a parameter of `reference_system_core` (default 8); `rtl/top_simulation/reference_system.sv` sets 20.
- `papabench_ibex/hw/patches/0003-sim-top-papabench-env.patch`: the simulation top instantiates `papabench_env`, which drives `gp_i` and `uart_rx`; `uartdpi` only receives.
- `papabench_ibex/hw/rtl/papabench_env.sv`: deterministic environment — mode from `gp_o[2:1]`; FBW radio PPM / Autopilot modem clock as falling edges into the `gp_i[0]` latch (ack = toggle of `gp_o[0]`); Autopilot GPS UBX bursts on `uart_rx` (115200 8N1); replays `papabench_stim.svh`.
- `papabench_ibex/hw/stimulus/gen_stimulus.py`: writes `papabench_stim.svh` (PPM widths: 40 frames × 9 channels; GPS: 120 epochs of NAV-POSUTM/STATUS/VELNED with UBX checksums, circle flight); run by `make sim`.
- `papabench_ibex/hw/test/hw_smoke.c`: `make hwtest` program — TimerC periodic IRQ 20 checks, programs PWM channel 0 (100000-cycle period, 75000-cycle pulse); prints PASS/FAIL to SimCtrl.
- `papabench_ibex/hw/test/check_pwm.sh`: measures `pwm_o[0]` on the `hwtest` waveform (`fst2vcd` | awk, clock-edge count) against the programmed pulse/period.
- `papabench_ibex/patches/autopilot_main_modeupdate.patch`: removes the `;` after `ModeUpdate(...)` in `pprz_mode_update()` (upstream syntax error); applied by the Makefile to a copy of `autopilot/main.c`.
- `papabench_ibex/harness/papabench_harness.h`: typedef-free interface between the two sides — `struct papabench_task` (`name`, `first`, `last`), `PAPABENCH_TASK()`, `papabench_tasks[]`, `papabench_startup_ticks`, `papabench_check()`, `papabench_upstream_main()`, `papabench_calib()`, `papabench_tick_pending`, `papabench_tick_cycles`; model interface `papabench_isrs[]`, `papabench_isr_run()`, `papabench_periph_init/poll()`, `papabench_irq_*()`.
- `papabench_ibex/harness/ibex_io.h`: typedef-free SoC register map (timers incl. TimerC, UART, GPIO, PWM), IRQ numbers, `mcycle`/`mie`/`mstatus` helpers; used by both sides.
- `papabench_ibex/harness/periph.h`, `periph.c`: generic AVR peripheral-model layer (PapaBench side) — event channels on TimerA/B/C and their IRQ dispatch, AVR↔Ibex time conversion and counter snapshot, Timer1 compare arming (`pb_oc_sync`), `gp_o` shadow and IRQ 17 acknowledge, the `SPDR` receive/transmit slots.
- `papabench_ibex/harness/fbw_periph.c`: FBW model — servo compare (TimerA) → PWM ch 0–9 + 4017 pins on `gp_o`, ADC (TimerB), virtual Autopilot SPI master (TimerC, frame every 3 ticks), UART TX to the real UART, PPM input capture on IRQ 17; ISR table.
- `papabench_ibex/harness/autopilot_periph.c`: Autopilot model — `link_fbw` compare (TimerA), SPI master with a virtual FBW slave (TimerC, mode MANUAL→AUTO1→AUTO2), ADC with IR samples (TimerB), GPS on UART RX (IRQ 16), modem clock on IRQ 17 with the data bit on `gp_o[3]`; ISR table.
- `papabench_ibex/harness/harness.c`: Ibex-side `main()` — `papabench_periph_init()`, wrappers on IRQ 16–20 (dispatch + poll, body added to the ISR time), `harness_timer_isr` on the machine timer (IRQ 7, `PAPABENCH_TICK_CYCLES`), runs `papabench_upstream_main()`; `__cyg_profile_func_enter/exit` hooks record `mcycle` per task minus ISR time; `papabench_isr_run()` times each upstream ISR; report (`task,` and `isr,` lines) + `sim_halt()` after `PAPABENCH_TICKS` ticks.
- `papabench_ibex/harness/calib.c`: `papabench_calib()`, empty instrumented function for the hook-overhead measurement.
- `papabench_ibex/harness/fbw_glue.c`: FBW task table (5 tasks), `papabench_startup_ticks = 0`, TIFR/TOV2 check.
- `papabench_ibex/harness/autopilot_glue.c`: Autopilot task table (8 tasks; `navigation_task`/`reporting_task`/`receive_gps_data_task` as first/last call of their sequences), `papabench_startup_ticks = 30`, TIFR/TOV2 check.
- `papabench_ibex/harness/runtime.c`: `papabench_sfr[]`, `papabench_tick_pending` + `papabench_tick_take()` (also the idle sync point: `papabench_periph_poll()` with interrupts off), `memcpy`, `memset`.
- `papabench_ibex/build/`: build outputs (ignored) — per program `plain/` (for nm), `pb/` (instrumented), `glue/`, `ibex/`, `harness/`, `patched/` objects, `instrumented.txt`, `functions.txt`, `exclude.txt`, `.elf`, `.map`, and after `run`: `reference_system.log` (results), `sim.log`, `uart0.log`, `reference_system_pcount.csv`; `build/sim/`: FuseSoC build root of the standard-Ibex Verilator model (`sim_ibex-verilator/Vreference_system`), with `hw/` = staged and patched SoC sources + our `.core` files + `rtl/papabench/` (environment, generated `papabench_stim.svh`); `build/hwtest/`: smoke-test ELF, logs, `sim.fst` waveform.

## `bench/` (upstream TACLeBench — read-only)

- `bench/checkBenchmark.sh`: host-side check; compiles each `bench/<cat>/<name>/*.c` with `gcc -Wall -Wno-unknown-pragmas -Werror` and expects exit code 0.
- `bench/app/`: application benchmarks (`lift`, `powerwindow`).
- `bench/kernel/`: kernel benchmarks (`binarysearch`, `bitcount`, …, `st`), each self-contained `*.c`.
- `bench/sequential/`: sequential benchmarks (`adpcm_dec`, …, `susan`).
- `bench/test/`: test programs (`cover`, `duff`, `test3`).
- `bench/parallel/DEBIE/`: DEBIE parallel benchmark (not in scope).
- `bench/parallel/rosace/`: ROSACE parallel benchmark (not in scope).
- `bench/parallel/PapaBench/`: the benchmark this project ports; detail below.

### `bench/parallel/PapaBench/`

- `README`: task lists and frequencies for FBW and Autopilot, `PAPABENCH_SINGLE` mode, `__SFR_OFFSET` warning.
- `PapaBench_for_wcet.txt`: task identifiers → C entry points (FBW T1–T5, Autopilot T5–T12) and interrupt vectors.
- `Loops_Bounds.txt`: per-function loop bounds (source of the `_Pragma( "loopbound … " )` annotations).
- `AUTHORS`, `COPYING`: authorship and GPLv2.
- `aadl_sources/`: AADL/AAXL task models (four periodicity variants); documentation only.
- `conf/`: original cross-compile configs (`Makefile.std`, `*-elf*.conf`, `Makefile.local.in`); no working Makefile exists in this tree.
- `arch/include/avr/arch/io.h`: AVR device dispatch — includes `iom8.h`/`iom128.h` by `__AVR_ATmega*__` macro.
- `arch/include/avr/arch/iom8.h`, `iom128.h`: register addresses (`TIFR`, `SPSR`, `TCNT1`, …) for ATmega8 / ATmega128.
- `arch/include/avr/arch/sfr_defs.h`: `_MMIO_BYTE`/`_MMIO_WORD`, `_SFR_IO8` (hard-coded `+ 0x20` in C), `_SFR_MEM8`, `_BV`, `bit_is_set`.
- `arch/include/avr/arch/signal.h`: `SIGNAL()`/`INTERRUPT()` — plain function definitions when not C++.
- `arch/include/avr/arch/interrupt.h`: `sei()`/`cli()` as AVR inline asm (calls are commented out in the sources).
- `arch/include/avr/arch/portpins.h`: AVR port pin names.
- `sw/airborne/fly_by_wire/main.c`: FBW `main()`, `fbw_init()`, `fbw_schedule()`, tasks `test_ppm_task`, `check_failsafe_task`, `check_mega128_values_task`, `send_data_to_autopilot_task`.
- `sw/airborne/fly_by_wire/servo.c`, `servo.h`: `servo_init`, `servo_set`, task `servo_transmit`, `SIG_OUTPUT_COMPARE1A` ISR.
- `sw/airborne/fly_by_wire/ppm.c`, `ppm.h`: radio PPM decoding, `SIG_INPUT_CAPTURE1` ISR, `ppm_valid`.
- `sw/airborne/fly_by_wire/spi.c`, `spi.h`: FBW-side SPI slave, `SIG_SPI` ISR, `spi_was_interrupted`.
- `sw/airborne/fly_by_wire/adc_fbw.c`, `adc_fbw.h`: ADC buffers, `SIG_ADC` ISR.
- `sw/airborne/fly_by_wire/uart.c`, `uart.h`: `uart_init_tx`, `uart_print_string`, TX ISR.
- `sw/airborne/fly_by_wire/timer.h`: `timer_init`, `timer_periodic` (polls `TIFR.TOV2`).
- `sw/airborne/fly_by_wire/link_autopilot.h`: `inter_mcu_msg` shared message layout.
- `sw/airborne/autopilot/mainloop.c`: Autopilot `main()` — init, 31-iteration `timer_periodic` wait, main loop.
- `sw/airborne/autopilot/main.c`: `periodic_task()` scheduler and tasks `radio_control_task`, `navigation_task`, `altitude_control_task`, `climb_control_task`, `stabilisation_task`, `reporting_task`.
- `sw/airborne/autopilot/link_fbw.c`, `link_fbw.h`: task `link_fbw_send`, `link_fbw_receive_complete`.
- `sw/airborne/autopilot/nav.c`, `nav.h`: navigation, flight-plan execution (`NormCourse` loop bounds).
- `sw/airborne/autopilot/estimator.c`, `estimator.h`: attitude/position estimator (float).
- `sw/airborne/autopilot/pid.c`, `pid.h`: roll/pitch/climb PID loops (float).
- `sw/airborne/autopilot/infrared.c`, `infrared.h`: IR sensor processing.
- `sw/airborne/autopilot/gps_ubx.c`, `ubx.h`: UBX GPS parsing (`parse_gps_msg`, `utm_east0`); the one we build (`-DUBX`).
- `sw/airborne/autopilot/gps_sirf.c`, `sirf.h`: SiRF variant; does not compile (missing `math_papabench.h`), not built.
- `sw/airborne/autopilot/gps.h`: selects `ubx.h` or `sirf.h` by `UBX`.
- `sw/airborne/autopilot/adc.c`, `adc.h`: ADC driver.
- `sw/airborne/autopilot/ad7714.c`, `ad7714.h`: external ADC driver; unused and does not compile (`else` after macro block), not built.
- `sw/airborne/autopilot/spi.c`, `spi.h`, `modem.c`, `modem.h`, `uart.c`, `uart.h`: Autopilot-side peripherals and ISRs.
- `sw/airborne/autopilot/if_calib.c`, `if_calib.h`: in-flight calibration.
- `sw/airborne/autopilot/autopilot.h`, `downlink.h`, `timer.h`: shared state/macros; `timer_periodic`.
- `sw/include/math.h`: redirects `sin`/`cos`/`sqrt`/`atan2`/`fabs` to `pp_*` functions.
- `sw/include/inttypes.h`: local fixed-width typedefs (`uint32_t` is `unsigned long`) — shadows the toolchain header.
- `sw/include/std.h`: `TRUE`/`FALSE`, `bool_t`, `sbi`/`cbi` SFR bit macros.
- `sw/include/c/`: ground-station headers (`downlink.h` needs glib, `traces.h` needs stdio); not for the airborne build.
- `sw/lib/c/math.c`: `pp_sin`, `pp_atan2` (double); `pp_sqrt` body is under `#if 0` (returns nothing). Built for the Autopilot only.
- `sw/lib/c/*.c` (others): ground-station library code; not needed by either airborne program (both link without them).
- `sw/lib/crt0/powerpc-elf-crt0.c`: PowerPC startup; not used.
- `sw/var/include/`: generated config headers — `airframe.h`, `flight_plan.h`, `radio.h`, `messages.h`, `inflight_calib.h`, `ubx_protocol.h`.

## `doc/` (upstream)

- `doc/20160705-wcet-falk.pdf`: TACLeBench WCET 2016 paper.
- `doc/code_formatting.txt`: TACLeBench coding style (2-space indent, `( a )` spacing, 80 cols, no `static`, name-prefixed globals).
- `doc/TACLeBench-Flowfacts.pdf`, `.odt`: flow-fact (loopbound) annotation format.
- `doc/2017-TACLeBench-LITES_Journal/`, `techReport/`, `wcet-paper/`, `whitepaper.txt`, `code_review_1/`, `example/`: other upstream papers and examples.

## `Secure-Ibex/` (submodule — do not edit from here)

Only the files this project reads:

- `Secure-Ibex/CLAUDE.md`: the submodule's own router and rules.
- `Secure-Ibex/README.md`: prerequisites, FuseSoC/Verilator build and run commands.
- `Secure-Ibex/sw/Standard_Only_SW/Standard_Tests/common/common.mk`: bare-metal build rules (`ARCH ?= rv32imc`, `-nostdlib`, `LIBS`, `.elf/.bin/.vmem`).
- `Secure-Ibex/sw/Standard_Only_SW/Standard_Tests/common/link.ld`: RAM at `0x00100000` (56 KiB) + stack (8 KiB), `_vectors_start`, entry at `+0x80`.
- `Secure-Ibex/sw/Standard_Only_SW/Standard_Tests/common/reference_system_common.[ch]`: `putchar`, `puts`, `puthex`, `sim_halt`, `PCOUNT_READ`, `pcount_enable`.
- `Secure-Ibex/sw/Standard_Only_SW/Standard_Tests/common/reference_system_regs.h`: `SIM_CTRL_BASE 0x20000`, `TIMER_BASE 0x80000000`, `UART0_BASE 0x80001000`, `GPIO_BASE`, `PWM_BASE`.
- `Secure-Ibex/sw/Standard_Only_SW/Standard_Tests/common/{timer,uart,gpio,pwm}.[ch]`: peripheral drivers.
- `Secure-Ibex/sw/Standard_Only_SW/Standard_Tests/hello_test/`: minimal program (`crt0.S`, `Makefile`, `hello_test.c`) — template for a standard-Ibex build.
- `Secure-Ibex/sw/Standard_vs_Smtctx_SW/Embench_Tasks_Tests/`: precedent for running a benchmark suite as tasks (Smtctx-oriented; reference only).
- `Secure-Ibex/dv/verilator/`: Verilator testbench (`reference_system.cc`, `reference_system_main.cc`).
- `Secure-Ibex/reference_system.core` (+ the other `*.core`, `rtl/`, `vendor/`): FuseSoC cores and RTL read by `make sim` (target `sim_ibex`).
- `Secure-Ibex/python-requirements.txt`: FuseSoC/edalize venv requirements.
