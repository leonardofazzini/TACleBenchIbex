# PapaBench (upstream, `bench/parallel/PapaBench/`)

Read-only. Everything here describes the upstream sources as they are; the port lives elsewhere (see `port-harness.md`).

## What it is

Code extracted from Paparazzi, a UAV autopilot that originally ran on two AVR microcontrollers linked by SPI: **Fly-By-Wire (FBW)** drives the servos and reads the radio; **Autopilot** runs navigation, estimation and control. PapaBench keeps the two programs separate, each with its own `main()` and infinite control loop. WCET is meant to be measured **per task** (entry-point function), not per `main()`; see `PapaBench_for_wcet.txt`.

## Structure

- FBW: `sw/airborne/fly_by_wire/*.c` — `main()` in `main.c`.
- Autopilot: `sw/airborne/autopilot/*.c` — `main()` in `mainloop.c`, scheduler `periodic_task()` and most tasks in `main.c`.
- Shared: `sw/include/` (`math.h`, `inttypes.h`, `std.h`), `sw/lib/c/math.c`, `sw/var/include/` (generated airframe/flight-plan/radio config), `arch/include/avr/arch/` (AVR register headers).
- No working Makefile exists in this tree; `conf/` holds the original configs (`Makefile.std` includes a missing `conf/Makefile.local`).
- Verified file lists (2026-09-23): FBW = `adc_fbw.c main.c ppm.c servo.c spi.c uart.c`; Autopilot = `adc.c estimator.c gps_ubx.c if_calib.c infrared.c link_fbw.c main.c mainloop.c modem.c nav.c pid.c spi.c uart.c` + `sw/lib/c/math.c`, with `-DUBX`. `ad7714.c` and `gps_sirf.c` are unused and do not compile. The Autopilot also needs `-I sw/airborne/fly_by_wire` for `link_autopilot.h`, placed after its own directory.

## Tasks (entry points, marked `_Pragma( "entrypoint" )` in most cases)

FBW (`fbw_schedule()` in `fly_by_wire/main.c` calls them every loop iteration):

| Task | Where | Activation in `fbw_schedule()` |
|---|---|---|
| `test_ppm_task` | `main.c` | every iteration |
| `check_mega128_values_task` | `main.c` | every iteration |
| `send_data_to_autopilot_task` | `main.c` | every iteration |
| `check_failsafe_task` | `main.c` | every iteration |
| `servo_transmit` | `servo.c` | only when `_20Hz >= 3` |

`_1Hz`/`_20Hz` are incremented in `main()` only when `timer_periodic()` returns TRUE.

**Upstream defect: `servo_transmit` is never called in the two-program build.** `main()` does `_20Hz++; if ( _20Hz >= 3 ) _20Hz = 0;` before the next `fbw_schedule()`, so the `_20Hz >= 3` test there is never true (verified in the disassembly and on Verilator: 0 activations in 61 ticks). Only `PAPABENCH_SINGLE` uses a different path.

Autopilot (`main()` in `mainloop.c` → `periodic_task()` in `main.c` when `timer_periodic()` is TRUE):

- `periodic_task()` (61 Hz tick): every 15 ticks (`_4Hz == 0`) runs the navigation block (`estimator_propagate_state`, `navigation_update`, `send_nav_values`, `course_run`) then `altitude_control_task`, `climb_control_task`; on `_20Hz == 1` every other time the reporting block (`send_boot` … `send_nav_ref`); on `_20Hz == 2` `stabilisation_task`, `link_fbw_send`.
- **`navigation_task`, `reporting_task` and `receive_gps_data_task` do not exist as functions** in this version, although `PapaBench_for_wcet.txt` lists them; their bodies are the blocks above (and the GPS block below).
- `radio_control_task` runs when `link_fbw_receive_complete` is set (by the SPI ISR).
- The GPS path (`parse_gps_msg`, `send_gps_pos`, …) runs when `gps_msg_received` is set (by the UART ISR); `receive_gps_data_task` itself is commented out.

Interrupt service routines are written as `SIGNAL( SIG_… )` and are **plain C functions** on non-AVR targets (`signal.h`); nothing calls them unless the harness does.

## Build macros

- `PAPABENCH_SINGLE` — merges FBW into the Autopilot executable. **Never define it here.**
- `NO_MAINLOOP` — removes the `while ( 1 )`: `main()` runs one iteration and returns 0.
- `WCET_ANALYSIS` — removes SPI/UART-dependent bodies (e.g. in `send_data_to_autopilot_task`, `check_mega128_values_task`, the UART boot message).
- `CTL_BRD_V1_1` — selects an older control board (drops the FBW ADC). Leave undefined unless there is a reason.
- `__AVR_ATmega8__` / `__AVR_ATmega128__` — required by `arch/io.h` to include `iom8.h` / `iom128.h`. Verified: FBW = ATmega8, Autopilot = ATmega128 (the other choice leaves registers such as `UBRRH` or `PORTF`/`UDR1` undefined).
- `UBX` — selects `ubx.h` in `gps.h`; without it `utm_east0`/`utm_north0` (used by `main.c`) are undeclared.

## AVR register layer

- `sfr_defs.h`, default C path (`_SFR_ASM_COMPAT` undefined): `_SFR_IO8(x)` → `_MMIO_BYTE( (x) + 0x20 )`, `_SFR_MEM8(x)` → `_MMIO_BYTE( x )`, `_MMIO_BYTE(a)` → `*(volatile uint8_t *)(a)`. The `0x20` is a literal, not `__SFR_OFFSET`; `__SFR_OFFSET` is only used (and `#error`-checked to be 0 or 0x20) when `_SFR_ASM_COMPAT` is set. So the upstream README's "set SFR_OFFSET" advice cannot move the registers away from low memory. `bit_is_set`, `_BV`, `_SFR_BYTE` also come from here.
- Register names used by the sources (most frequent: `SPDR`, `PORTB/D/E`, `DDRB/D/E`, `UCSRB`, `UCSR0B`, `UCSR1B`, `TIFR`, `SPCR`, `ADMUX`, `TIMSK`, `OCR1A`, `UDR0/1`, `TCNT1`, `ADCSR`, `TCCR2`) therefore resolve to fixed addresses in 0x20–0xFF.
- Redirecting `_MMIO_BYTE`/`_MMIO_WORD` (both defined unconditionally in `sfr_defs.h`, not `#ifndef`-guarded) is enough to move every register; it requires shadowing the header, since redefining the macro after inclusion triggers a redefinition warning and ordering issues.
- `interrupt.h`: `sei()`/`cli()` are AVR inline asm; all calls are commented out, so they never reach the RISC-V assembler. Any new call would break the build.
- Include style differs: FBW uses `<io.h>`, `<signal.h>`, `<interrupt.h>`; Autopilot uses `<arch/io.h>`, `<arch/signal.h>`. Both `-I arch/include/avr` and `-I arch/include/avr/arch` are needed, placed before system includes (a bare `<signal.h>` must not resolve to newlib's).

## Timing layer

`timer_periodic()` (identical in `fly_by_wire/timer.h` and `autopilot/timer.h`, `static inline`) returns TRUE only if `bit_is_set( TIFR, TOV2 )`, then writes `TIFR = 1 << TOV2`. On real AVR, writing 1 clears the flag; on plain memory it would **set** it, so the port emulates the flag outside the register array (see `port-harness.md`). It is the only reader of `TIFR`; `servo.c` and `link_fbw.c` write other `TIFR` bits. `TOV2 = 6` on both devices; `TIFR` is at data address `0x58` (ATmega8) / `0x56` (ATmega128). Autopilot `main()` first waits for 30 `timer_periodic()` ticks (`_Pragma( "loopbound min 31 max 31" )`).

## Types and math

- `sw/include/inttypes.h` defines `int32_t`/`uint32_t` as `signed long`/`unsigned long` — 32 bits on RV32, but a **different type** from newlib's `stdint.h` (`unsigned int`). Do not include both in one translation unit.
- `sw/include/math.h` maps `sin`, `cos`, `sqrt`, `atan2`, `fabs` onto `pp_*` in `sw/lib/c/math.c` (all `double`). No libm needed; libgcc soft-float is.

## Flow facts

`_Pragma( "loopbound min N max M" )` and `_Pragma( "entrypoint" )` annotate loops and tasks for WCET tools (format: `doc/TACLeBench-Flowfacts.pdf`; values mirrored in `Loops_Bounds.txt`). GCC ignores them with a warning (`-Wno-unknown-pragmas`). Note `nav.c:190,192` uses the misspelled `loopbounds`. Never edit them.

## Upstream defects (verified with GCC 10.2)

- `autopilot/main.c:147-150`: `ModeUpdate(...)` expands to `{ ... }`, the caller adds `;`, then `else` → syntax error. Same pattern in `ad7714.c:86`.
- `autopilot/main.c`: non-static `inline` functions (`pprz_mode_update`, `ground_calibrate`, …) have no external definition under C99 semantics → undefined references unless `-fgnu89-inline`.
- `modem.c` and `gps_ubx.c` both define `ck_a`/`ck_b` without initializer → multiple definition under GCC ≥ 10 unless `-fcommon`.
- `fly_by_wire/main.c`: `servo_transmit()` unreachable (see Tasks above).
- `sw/lib/c/math.c`: `pp_sqrt()` body is inside `#if 0` (and contains an unterminated `_Pragma` string) → returns an indeterminate value; called by `nav.c:159` (`sqrt( leg2 )`).
- `gps_sirf.c` includes a missing `math_papabench.h`.

## Traps

- `_Pragma( "entrypoint" )` placed between return type and name (`void _Pragma( "entrypoint" ) f( void )`) is valid C99 but some tools choke on it; GCC accepts it.
- Ground-station code in `sw/include/c/` and `sw/lib/c/` (except `math.c`) needs glib/stdio — keep it out of the build.
- `uart_print_string` in FBW has a 100-iteration loop bound and writes to AVR UART registers, not to the Ibex UART.
