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

FBW (`fbw_schedule()` in `fly_by_wire/main.c`; upstream `main()` calls it every loop iteration, our patch once per tick):

| Task | Where | Activation in `fbw_schedule()` |
|---|---|---|
| `test_ppm_task` | `main.c` | every iteration upstream; every tick with our patch |
| `check_mega128_values_task` | `main.c` | every iteration upstream; every tick with our patch |
| `send_data_to_autopilot_task` | `main.c` | every iteration upstream; every tick with our patch |
| `check_failsafe_task` | `main.c` | every iteration upstream; every tick with our patch |
| `servo_transmit` | `servo.c` | only when `_20Hz >= 3` (every 3rd tick with our patch) |

`_1Hz`/`_20Hz` are incremented in `main()` only when `timer_periodic()` returns TRUE.

**Upstream defect: `servo_transmit` is never called in the two-program build.** `main()` does `_20Hz++; if ( _20Hz >= 3 ) _20Hz = 0;` before the next `fbw_schedule()`, so the `_20Hz >= 3` test there is never true (verified in the disassembly and on Verilator: 0 activations in 61 ticks). Only `PAPABENCH_SINGLE` uses a different path. Fixed by `papabench_ibex/patches/fbw_main_schedule.patch` (2026-09-24): `fbw_schedule()` does `if ( _20Hz >= 3 ) { _20Hz = 0; servo_transmit(); }` and `main()` no longer wraps `_20Hz`, so the call happens exactly once, on the first `fbw_schedule()` after every 3rd tick (a test on the tick value alone would fire on every loop iteration of that tick, ~370 times). `servo_transmit` writes 23 bytes to the UART (0, 0, 10 × big-endian width, `\n`).

Autopilot (`main()` in `mainloop.c` → `periodic_task()` in `main.c` when `timer_periodic()` is TRUE):

- `periodic_task()` (61 Hz tick): every 15 ticks (`_4Hz == 0`) runs the navigation block (`estimator_propagate_state`, `navigation_update`, `send_nav_values`, `course_run`) then `altitude_control_task`, `climb_control_task`; on `_20Hz == 1` every other time the reporting block (`send_boot` … `send_nav_ref`); on `_20Hz == 2` `stabilisation_task`, `link_fbw_send`.
- **`navigation_task`, `reporting_task` and `receive_gps_data_task` do not exist as functions** in this version, although `PapaBench_for_wcet.txt` lists them; their bodies are the blocks above (and the GPS block below).
- `radio_control_task` runs when `link_fbw_receive_complete` is set (by the SPI ISR).
- The GPS path (`parse_gps_msg`, `send_gps_pos`, …) runs when `gps_msg_received` is set (by the UART ISR); `receive_gps_data_task` itself is commented out.

Interrupt service routines are written as `SIGNAL( SIG_… )` and are **plain C functions** on non-AVR targets (`signal.h`); nothing calls them unless the harness does.

## Interrupts and peripherals (verified 2026-09-23)

| Program | Vector | Source | What it does |
|---|---|---|---|
| FBW | `__vector_5` | Timer1 input capture (PPM, falling edge) | decodes the radio: `ICR1` widths, sync gap measured with `TCNT2` (> 7 ms); after 9 channels sets `ppm_valid` |
| FBW | `__vector_6` | Timer1 compare A | 4017 servo driver: `OCR1A += servo_widths[ servo++ ]` (10 channels, `servo_widths` and `servo` static) |
| FBW | `__vector_10` | SPI (slave) | one byte of the 23-byte frame with the Autopilot (`FRAME_LENGTH` = 22-byte `inter_mcu_msg` + XOR checksum); writes the next TX byte to `SPDR`, then reads the received one |
| FBW | `__vector_13` | UART TX complete | sends the next buffered byte (boot string, then the 23-byte `servo_transmit` frames) |
| FBW | `__vector_14` | ADC | stores `ADCW` of `ADMUX & 7`, next channel, restarts (`ADSC`) |
| Autopilot | `__vector_5` | INT4 (`CTL_BRD_V1_2_1`), modem clock | bit-bangs the downlink on PORTD.6 (start, 8 data, stop); disables INT4 when the buffer is empty |
| Autopilot | `__vector_12` | Timer1 compare A | `link_fbw`: next SPI byte (write `SPDR`, read the received one); at the end unselects the slave, `SPI_STOP()`, sets `link_fbw_receive_complete` |
| Autopilot | `__vector_17` | SPI (master) | `link_fbw_on_spi_it()`: `OCR1A = TCNT1 + 200`, enables OCIE1A |
| Autopilot | `__vector_21` | ADC | as FBW; IR sensors on channels 1 and 2 |
| Autopilot | `__vector_30` | UART1 RX | `parse_ubx( UDR1 )` |

`PapaBench_for_wcet.txt` lists only I1–I7 (FBW 5/6/10, Autopilot 5/12/17/30), not the ADC and UART TX handlers.

- **`SPDR` is two registers** (write = transmit, read = received byte); both SPI protocols write the next byte and then read the received one in the same ISR, so a single memory cell breaks them (checksums never match).
- Counters: `TCNT1` is read only by `link_fbw_on_spi_it()`, `TCNT2` and `ICR1` only by the PPM ISR; `timer_now()` is never called.
- Downlink goes through the modem (`downlink.h` includes `modem.h`, `MODEM_PUT_*` into `tx_buf`), not through a UART. `MODEM_CHECK_RUNNING()` enables INT4 and clears `EIFR.INTF0` (not INTF4).
- **`ck_a`/`ck_b` are shared by the modem (downlink checksum) and the UBX parser** (both define them; `-fcommon` merges them): a downlink message built while a UBX message is being received corrupts its checksum and the message is dropped. Kept as is.
- `parse_ubx()` drops a message if `gps_msg_received` is still set (`gps_nb_ovrn++`); `send_gps_pos()` clears it. During the Autopilot's 30-tick start-up wait the main loop does not consume GPS messages.
- Flight plan (`flight_plan.h`, block 0 "init"): waits for `estimator_flight_time > 8` (seconds of flight after `send_takeOff()`), then climbs to `SECURITY_ALT`; only block 1 sets `VERTICAL_MODE_AUTO_ALT`, so `altitude_control_task` takes its long path (`altitude_pid_run()`) only after ~8 s of flight in AUTO2.
- Radio: PPM order = channel index (`radio.h`: 0 throttle, 1 roll, 2 pitch, 3 yaw, 4 mode, 5 gain1, 6 gain2, 7 LLS, 8 calib); FBW mode = AUTO when the mode channel ≥ `MIN_PPRZ / 2`; Autopilot `PPRZ_MODE_OF_PULSE`: > 3200 AUTO2, > −4800 AUTO1, else MANUAL; takeoff needs throttle > 0.9 `MAX_PPRZ`.
- Upstream FBW `fbw_schedule()` counts `time_since_last_ppm`/`time_since_last_mega128` per main-loop iteration (~370 per tick), not per tick: `radio_ok`/`mega128_ok` drop within a fraction of a tick and `check_failsafe_task` applies `failsafe` (all servos at neutral) almost always (verified 2026-09-24 on the servo frames). Fixed by `patches/fbw_main_schedule.patch` (`fbw_schedule()` only when `timer_periodic()` is TRUE, as `PAPABENCH_SINGLE`).

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
- `fly_by_wire/main.c`: `servo_transmit()` unreachable and `fbw_schedule()` called every loop iteration (see Tasks above and scenario notes) → `patches/fbw_main_schedule.patch`.
- `sw/lib/c/math.c`: `pp_sqrt()` body is inside `#if 0` (and contains an unterminated `_Pragma` string) → returns an indeterminate value; called by `nav.c:159` (`sqrt( leg2 )`).
- `gps_sirf.c` includes a missing `math_papabench.h`.

## Traps

- `_Pragma( "entrypoint" )` placed between return type and name (`void _Pragma( "entrypoint" ) f( void )`) is valid C99 but some tools choke on it; GCC accepts it.
- Ground-station code in `sw/include/c/` and `sw/lib/c/` (except `math.c`) needs glib/stdio — keep it out of the build.
- `uart_print_string` in FBW has a 100-iteration loop bound and writes to AVR UART registers, not to the Ibex UART.
