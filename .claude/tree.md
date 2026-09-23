# tree.md — map of the repository

One line per file or directory: what it contains. Update it **after every file added, renamed, moved or deleted** (see `rules.md`).

Not listed: build outputs (`*.o`, `*.d`, `*.elf`, `*.bin`, `*.vmem`, `*.dis`, `a.out`, `output.map`), the Secure-Ibex FuseSoC `build/` directory, `.venv/`, and the internals of the `Secure-Ibex/` submodule except the files this project reads. Other TACLeBench benchmarks are listed per directory, not per file.

## Root

- `CLAUDE.md`: agent router — objective, hard constraints, routing table, invariants.
- `README.md`: upstream TACLeBench README (citation, `cc *.c` build note); nothing Ibex-specific.
- `.gitmodules`: declares the `Secure-Ibex` submodule (`git@github.com:leonardofazzini/Secure-Ibex.git`).

## `.claude/`

- `.claude/rules.md`: protocol for updating `tree.md`, `status.md` and reference files.
- `.claude/tree.md`: this map.
- `.claude/status.md`: per-task log, to-do list, completed history.
- `.claude/reference/papabench.md`: PapaBench structure, tasks, macros, AVR register layer, float math.
- `.claude/reference/ibex-platform.md`: what we use from Secure-Ibex — memory map, `common/` lib, crt0, link.ld, Verilator flow, `mcycle`.
- `.claude/reference/port-harness.md`: design of the PapaBench-on-Ibex port — SFR remap, timer emulation, per-task measurement, open decisions.
- `.claude/reference/conventions.md`: build/run/debug commands, coding style, recipes.

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
- `sw/airborne/autopilot/gps_ubx.c`, `gps_sirf.c`, `gps.h`, `ubx.h`, `sirf.h`: GPS parsing (`parse_gps_msg`); which one is built is `TODO: verify`.
- `sw/airborne/autopilot/adc.c`, `adc.h`, `ad7714.c`, `ad7714.h`: ADC drivers.
- `sw/airborne/autopilot/spi.c`, `spi.h`, `modem.c`, `modem.h`, `uart.c`, `uart.h`: Autopilot-side peripherals and ISRs.
- `sw/airborne/autopilot/if_calib.c`, `if_calib.h`: in-flight calibration.
- `sw/airborne/autopilot/autopilot.h`, `downlink.h`, `timer.h`: shared state/macros; `timer_periodic`.
- `sw/include/math.h`: redirects `sin`/`cos`/`sqrt`/`atan2`/`fabs` to `pp_*` functions.
- `sw/include/inttypes.h`: local fixed-width typedefs (`uint32_t` is `unsigned long`) — shadows the toolchain header.
- `sw/include/std.h`: `TRUE`/`FALSE`, `bool_t`, `sbi`/`cbi` SFR bit macros.
- `sw/include/c/`: ground-station headers (`downlink.h` needs glib, `traces.h` needs stdio); not for the airborne build.
- `sw/lib/c/math.c`: `pp_sin`, `pp_sqrt`, `pp_atan2` (double).
- `sw/lib/c/*.c` (others): ground-station library code; `TODO: verify` none is needed by airborne.
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
