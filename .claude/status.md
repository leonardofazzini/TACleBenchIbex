# status.md — task log

Git records what changed in the code. This file records what was asked, what was decided, and what is still open.

Format: one `## Task: <short name> — YYYY-MM-DD` section per distinct request, each with a table `| Date | Asked | Done | User revision |`. One row per completed step. A user correction goes in the **User revision** column of the row it corrects; rows are never rewritten or deleted. See `rules.md`.

---

## Task: Bootstrap `.claude/` project memory — 2026-09-23

| Date | Asked | Done | User revision |
|---|---|---|---|
| 2026-09-23 | Set up `CLAUDE.md` + `.claude/{rules,tree,status}.md` + `.claude/reference/` after an interview | Interviewed (objective, first task, constraints, invariants, target, measurement, ISA, language); explored PapaBench and Secure-Ibex standard SW; created `CLAUDE.md`, `rules.md`, `tree.md`, `status.md`, `reference/{papabench,ibex-platform,port-harness,conventions}.md` | |

---

## Task: Port PapaBench (FBW and Autopilot separately) to bare-metal standard Ibex — 2026-09-23

Goal: build `fbw.elf` and `autopilot.elf` as two independent rv32im bare-metal programs from the unmodified sources in `bench/parallel/PapaBench/`, run each on the Verilator `sim_ibex` model, and print `mcycle` per task over UART.

| Date | Asked | Done | User revision |
|---|---|---|---|
| 2026-09-23 | Create `papabench_ibex/` and start the port | Created `papabench_ibex/include/arch/sfr_defs.h` (AVR registers → `papabench_sfr[]`). Toolchain: `/tools/riscv/riscv32` GCC 10.2, only multilib is rv32imc (libgcc has compressed code). Simulator: reusing the prebuilt `~/01_Progetti_PC/Secure-Ibex/build/.../sim_ibex-verilator/Vreference_system` (same commit `b04c064`) | |
| 2026-09-23 | (same request) | Trial compile: FBW = `__AVR_ATmega8__`, Autopilot = `__AVR_ATmega128__` (the other device leaves registers undefined). Autopilot needs `-DUBX`, `-I fly_by_wire` (for `link_autopilot.h`), `-fcommon` (`ck_a`/`ck_b` in two files), `-fgnu89-inline` (non-static `inline` in `main.c`). Excluded `ad7714.c`, `gps_sirf.c` (unused, do not compile). Added `patches/autopilot_main_modeupdate.patch` for the upstream `ModeUpdate(...); else` syntax error | |
| 2026-09-23 | (same request) | Built the harness: `Makefile`, `harness/{papabench_harness.h,harness.c,fbw_glue.c,autopilot_glue.c,runtime.c}`, `.gitignore`. Design: harness `main()` calls init once, then each task directly via a table, `ITER` times, `mcycle` around each call; upstream FBW `main()` renamed with `-Dmain=`, Autopilot `mainloop.c` not built (no `timer_periodic()` dependency any more). `navigation_task`, `reporting_task`, `receive_gps_data_task` rebuilt from their inlined pieces | |
| 2026-09-23 | (same request) | Both run on Verilator (`ITER=10`, `-Os`): FBW 5 tasks, Autopilot 8 tasks, overhead 5 cycles; results in `build/<prog>/reference_system.log`. Sizes: FBW 11.8 KB text, Autopilot 50.4 KB text (fits the 56 KiB region). Compressed instructions come only from libgcc (`__adddf3`, `__muldf3`, `__udivdi3`, …) | |

---

## Task: Drive each application's own scheduler with an Ibex HW timer — 2026-09-23

Decisions (user, 2026-09-23): Timer `0x80000000` (IRQ 7) — the prebuilt simulator has only this one (TimerA/B exist only in the current submodule RTL); tick = real AVR Timer2 period at 50 MHz, 16.384 ms = 819200 cycles (`TICK_CYCLES`); measure `mcycle` per task as before, but with tasks activated by the upstream scheduler; the direct table mode is replaced, not kept.

| Date | Asked | Done | User revision |
|---|---|---|---|
| 2026-09-23 | Scheduler of each application driven by a HW timer | Checked the timers: 3 in the submodule RTL (Timer, TimerA, TimerB), only Timer `0x80000000` in the prebuilt simulator; `mtime` +1 per cycle. Upstream `main()` of FBW and Autopilot (`mainloop.c`) now run unchanged as `papabench_upstream_main`; `harness_timer_isr` on IRQ 7 sets `papabench_tick_pending` every `TICK_CYCLES`; `sfr_defs.h` makes `bit_is_set( TIFR, TOV2 )` a test-and-clear of it (AVR write-1-to-clear emulation) | |
| 2026-09-23 | (same request) | Per-task `mcycle` with selective `-finstrument-functions`: Makefile builds the exclude list (plain build + `nm`) from `PAPABENCH_TASK(...)` lines, with checks; hooks in `harness.c` subtract ISR time with a consistent (ISR, mcycle) read; `calib.c` for overhead. Direct table mode removed (`ITER`/`RAW` gone; `TICKS`/`TICK_CYCLES` added). Fixed a race that produced negative samples (ISR between the two reads) | |
| 2026-09-23 | (same request) | Verilator, defaults (61 ticks × 819200): FBW 50 M cycles/84 s, ~370 loop iterations per tick, `check_failsafe_task` 1652–1746, `servo_transmit` 0 activations (upstream `_20Hz` defect); Autopilot 75 M cycles/126 s, activations 4/4/20/4/0/0/10/20 match the upstream rates, `stabilisation_task` 1819, `navigation_task` 1405, overhead 23. Autopilot image ~54.7 KB of 56 KiB | |

---

## Task: Use the submodule's Ibex and document how to run PapaBench in the README — 2026-09-23

| Date | Asked | Done | User revision |
|---|---|---|---|
| 2026-09-23 | Project must use the Ibex in the repo (submodule), README must explain how to run PapaBench and what was done | Simulator now built from `Secure-Ibex/` sources with FuseSoC `--build-root=papabench_ibex/build/sim` (`make sim`, auto-triggered by `run`; default `SIM` points there; `FUSESOC` var); submodule stays clean (`git status` empty). Needs the FuseSoC venv activated (`pip3 show edalize` check). The new model has Timer, TimerA, TimerB. Root `README.md` rewritten: quick start, prerequisites, targets/variables, output, results, what was done, known limitations, layout; upstream TACLeBench README kept below | |

## Task: Real interrupts and peripherals — SoC hardware patches — 2026-09-23

Goal: drive PapaBench with real Ibex interrupts and peripherals instead of RAM-emulated AVR registers. Decisions (user, 2026-09-23): stimuli must be deterministic (clock-driven sources only, no host pty input); servo outputs on the PWM; SoC hardware may be extended, but only through patches kept in this repo and applied to a build copy (`papabench_ibex/hw/patches/`), never by editing or committing the `Secure-Ibex/` submodule. First content: TimerC (own timer so SPI and ADC need not share TimerB) and a wide PWM counter (real 1–2 ms servo pulses at 50 MHz).

| Date | Asked | Done | User revision |
|---|---|---|---|
| 2026-09-23 | Proceed with the SoC patch | Created `papabench_ibex/hw/`: patches `0001-soc-timer-c` (TimerC `0x80030000`, IRQ 20) and `0002-soc-pwm-ctr-size` (PWM counter parameter, 20 bits in the sim top); our FuseSoC cores `papabench:soc:reference_system[_core]`; `make sim` now stages the SoC files into `build/sim/hw/`, patches them and builds from there. Submodule `git status` still empty. `make hwtest` (`hw/test/hw_smoke.c` + `check_pwm.sh`): TimerC 4/4 periodic IRQs, never early, max latency 18 cycles, TimerA/B quiet, `pwm_o[0]` 75000/100001 cycles — PASS. FBW and Autopilot `reference_system.log` and `Executed cycles` identical to the unpatched model | |
| 2026-09-23 | Proceed (wire PapaBench to real peripherals + deterministic environment) | Own `link.ld` (whole 128 KiB RAM; Autopilot now 58 KB text). Peripheral models: `harness/ibex_io.h`, `periph.{h,c}` (event channels on TimerA/B/C, AVR time, Timer1 compare, `gp_o`, SPDR slots), `fbw_periph.c`, `autopilot_periph.c`; `harness.c` wrappers on IRQ 16–20 + per-ISR stats (`isr,` lines, `isr_overhead`); idle sync in `papabench_tick_take()`; `sfr_defs.h` routes SPDR through `papabench_spdr_access()` (AVR SPDR is two registers). Mapping: Timer1 compare → TimerA, ADC → TimerB, SPI + UART TX → TimerC, servos → PWM ch 0–9, UART TX → real UART | |
| 2026-09-23 | (same request) | Bug found and fixed: arming OCR1A from "now" after the ISR lost ~263 AVR clocks of model work, so `link_fbw`'s `OCR1A = TCNT1 + 200` fell a full wrap later (each SPI byte 4 ms, half the frames lost). Now measured from the counter snapshot the ISR read; time conversion uses 32-bit hardware division | |
| 2026-09-23 | (same request) | Environment: `hw/rtl/papabench_env.sv` (PPM / modem clock edges into the `gp_i[0]` latch, ack by toggling `gp_o[0]`, mode on `gp_o[2:1]`; GPS UBX bursts on `uart_rx`), `hw/stimulus/gen_stimulus.py` (PPM 40 frames; GPS 120 epochs, circle flight), patch `0003-sim-top-papabench-env`; staged by `make sim` | |
| 2026-09-23 | (same request) | Verilator, defaults: FBW 50.0 M cycles — PPM 400 ISRs, `test_ppm_task` up to 5684, `check_mega128_values_task` up to 2969 (SPI frames accepted), servo ISR 701 → PWM, boot string on `uart0.log`; Autopilot 74.6 M cycles — `radio_control_task` 20, `receive_gps_data_task` 12 (up to 7997), modem 4129 ISRs, GPS RX 470. Two consecutive runs cycle-identical for both. `make hwtest` still passes. Scenario: Autopilot mode MANUAL→AUTO1→AUTO2 in the first 4 frames (AUTO2 from frame 10 left too few navigations in AUTO2) | |
| 2026-09-23 | (same request) | Long Autopilot run, TICKS=600 (~10 s of flight, 13 min 46 s wall): after the flight plan leaves the take-off block, `altitude_control_task` reaches 416 cycles (was 38), `climb_control_task` 1668, `navigation_task` 6300, `receive_gps_data_task` 12881 (118 activations); ISR maxima unchanged. Not in the README (pending user decision) | Report everything: long run added to the README (tasks and ISRs) |
| 2026-09-23 | Scenario OK but describe it in the README; keep the trap cost in task samples but explain it | README: new section 7 "The simulated scenario" (every FBW and Autopilot input: source, content, timing; resulting mode sequence; how to change it); Known limitations entry on trap entry/exit cost rewritten (what is and is not subtracted, how often, effect on min/max/avg) | |

---

## Task: Make FBW `servo_transmit` run — 2026-09-24

Goal: patch the upstream `_20Hz` defect so that `servo_transmit` is activated once every 3 ticks (~20 Hz), as intended, with `bench/` untouched.

| Date | Asked | Done | User revision |
|---|---|---|---|
| 2026-09-24 | Create the patch that makes the servo work | `papabench_ibex/patches/fbw_main_servo_transmit.patch`: `fbw_schedule()` resets `_20Hz` and calls `servo_transmit()` when it reaches 3; `main()` no longer wraps it. Makefile: FBW `main.c` now built from `build/fbw/patched/main.c` (same rule as the Autopilot, `MAIN_SRC`/`MAIN_PATCH`). Verilator, defaults: `servo_transmit` 20 activations (61 ticks / 3), 715 / 738 / 735 cycles; `uart_tx` ISR 523 calls; `uart0.log` 523 bytes = 63 boot + 20 × 23-byte servo frames. Autopilot ELF bit-identical. Found (pre-existing at HEAD `c09dd30`, not caused by the patch): negative task samples, see To-do | |

---

## Task: Run without measurements, "as on a real UAV" — 2026-09-24

Decisions (user, 2026-09-24): only a build without measurements (same scenario, programs still separate, no flight-dynamics model); run length still `TICKS`.

| Date | Asked | Done | User revision |
|---|---|---|---|
| 2026-09-24 | A version where the tasks run without analysis | Makefile `MEASURE ?= 1`; `MEASURE=0` builds in `build/<prog>-nomeasure/` with plain PapaBench objects (no `-finstrument-functions`, no exclude list), no `calib.o`, `-DPAPABENCH_MEASURE=0`; `harness.c` compiles out hooks, stats, calibrations and wrapper timing, prints only the header (`,measure=0`) and `END`. Verilator, defaults: FBW 49.97 M cycles (89 s), Autopilot 74.55 M cycles (116 s); no warnings; measured FBW results unchanged | |
| 2026-09-24 | (same request) | Found while checking the servo frames: FBW outputs the **failsafe** widths (neutrals, all commands 0) almost all the time, in both builds. Cause (upstream PapaBench): FBW `main()` calls `fbw_schedule()` on every loop iteration (~370 per tick), and `fbw_schedule()` increments `time_since_last_ppm`/`time_since_last_mega128`, meant per 60 Hz tick (`STALLED_TIME 30 // 500ms with a 60Hz timer`), so `radio_ok`/`mega128_ok` drop within a fraction of a tick and `check_failsafe_task` calls `servo_set( failsafe )`. `PAPABENCH_SINGLE` calls `fbw_schedule()` only on a tick. Pending user decision | |
| 2026-09-24 | Patch it, in both builds | Patch renamed `fbw_main_servo_transmit.patch` → `fbw_main_schedule.patch`, now also moves `fbw_schedule()` inside `if ( timer_periodic() )`. Verilator, defaults, both builds: servo frames follow the scenario (ailerons sweep 1430–1680 µs in MANUAL; from frame 16, ~0.78 s, AUTO: motor 1999 µs, elevator 1559 µs); the two builds differ only by a few µs of PPM jitter. Measured FBW: tasks 60 activations, `servo_transmit` 19 (669 / 727 / 700), `check_failsafe_task` has a negative sample (max 4294967295) that makes its avg meaningless; README FBW rows updated, `check_failsafe_task` marked invalid | |
| 2026-09-24 | The UART writes random things | Not random: `servo_transmit` writes the upstream binary frame (`0x00 0x00`, 10 × big-endian width in AVR clocks, `\n`). Format kept (it is the measured task); added `papabench_ibex/scripts/decode_uart.py` and a README note. Decoded default run: 19 frames matching the scenario | |

---

## Task: One peripheral, one function — 2026-09-24

Goal: the two programs must never use the same Ibex peripheral for different functions, so that one peripheral map is valid for a future joint run on the same SoC (runs stay separate for now). Decisions (user, 2026-09-24): granularity "one peripheral = one function" (TimerB = ADC and TimerC = SPI link stay shared, same function in both programs); missing resources come from new SoC patches; no UART for the servos, servo outputs only on the PWM; FBW's AVR UART (boot string + `servo_transmit` frames) becomes a virtual, timed UART (bytes dropped, TX-complete `__vector_13` still a real interrupt on its own timer); servo outputs verified from a PWM log written by the simulator, replacing `decode_uart.py`. Map: `gp_i[0]`/IRQ 17 = radio PPM (FBW); new `gp_i[1]`/IRQ 21 = modem clock (Autopilot); TimerA = servo compare (FBW); new TimerD = `link_fbw` byte pacing (Autopilot); new TimerE = FBW virtual UART; TimerB = ADC; TimerC = SPI link; UART0 = GPS (Autopilot).

| Date | Asked | Done | User revision |
|---|---|---|---|
| 2026-09-24 | Applications must not use the same peripherals for different things | Decisions collected (above); implementation started | |
| 2026-09-24 | (same request) | SoC: patch `0004-soc-timers-d-e-gpio-irq` (TimerD `0x80040000` IRQ 22, TimerE `0x80050000` IRQ 23, `gp_i[1]` IRQ 21), `0005-sim-top-pwm-monitor` + `hw/rtl/papabench_pwm_monitor.sv` (`pwm.log`). `papabench_env.sv`: separate PPM (`gp_i[0]`, ack `gp_o[0]`) and modem (`gp_i[1]`, ack `gp_o[6]`) latches, per-program enables `gp_o[1]`/`gp_o[2]` (replace the 2-bit mode, same bits). Software: map in `ibex_io.h`; models list their IRQs in `papabench_irqs[]`; harness has one `mcause`-dispatched wrapper, trap calibration moved from TimerC to the machine timer; `pb_now()` reads the machine timer; Autopilot `link_fbw` compare on TimerD; FBW UART virtual on TimerE (no Ibex UART write). `scripts/decode_uart.py` replaced by `scripts/decode_pwm.py`. `make hwtest` PASS (TimerC/D/E, IRQ 21, `pwm.log`) | |
| 2026-09-24 | (same request) | Verilator, defaults: FBW 50.0 M cycles, `uart0.log` empty, `uart_tx` ISR 500 (63 boot + 19 × 23), `pwm.log` → MANUAL aileron sweep 1435–1706 µs, AUTO from the period at 780 ms (motor ch 9 2000 µs, elevator 1560 µs); Autopilot 74.58 M cycles, two runs cycle-identical, all 20 SPI frames valid (`link_fbw_nb_err` 0, checked with temporary debug code, removed). `MEASURE=0`: FBW 49.98 M, Autopilot 74.55 M, same servo output. Timing shift moved the too-low samples (To-do): now `send_data_to_autopilot_task` −2, `radio_control_task` 38, `link_fbw_send` 97, besides `check_failsafe_task` −1. README (map, patches, outputs, results, limitations), `CLAUDE.md`, `port-harness.md`, `ibex-platform.md` updated | |
| 2026-09-24 | (same request) | Bug found (pre-existing, exposed by the new timing): Autopilot SPI model started a spurious transfer when a sync point fell between `SPI_START()` and the `SPDR` write of `link_fbw_send()` — the `SPDR` read before `SPI_STOP()` at the end of the previous frame was never consumed (SPI off) — so one frame had 24 transfers, was shifted by a byte and failed its checksum (`spi` 461 vs 460, `radio_control_task` 38 cycles, long run 4603 vs 4600). Fix in `ap_spi_sync()`: accesses with SPE or MSTR off are consumed. Default Autopilot now `spi` 460, `radio_control_task` 291 / 1492 / 419, `link_fbw_send` 108 | |
| 2026-09-24 | (same request) | After the fix: Autopilot default 74.58 M cycles, `MEASURE=0` 74.55 M; long run (`TICKS=600`, 516 M cycles) `spi` 4600 = `link_fbw_oc1a` 4600; `receive_gps_data_task` 119 activations, max 7870 (was 118 / 12881 before the split), `navigation_task` max 6117 (was 6300), `altitude_control_task` 38 / 416 / 57 unchanged; a few minima slightly low (`link_fbw_send` 97, `radio_control_task` 280), the trap-cost To-do. README long-run table refreshed | |

---

## Task: Stop subtracting the trap cost — 2026-09-24

Decision (user, 2026-09-24): task samples no longer subtract the calibrated trap entry/exit cost (`trap_overhead`); only the interrupt wrapper bodies are subtracted. Fixes the negative/too-low samples (`check_failsafe_task` −1, `send_data_to_autopilot_task` −2) at the price of ~100 cycles in every sample hit by an interrupt.

| Date | Asked | Done | User revision |
|---|---|---|---|
| 2026-09-24 | Proceed without subtracting the trap | `harness.c`: wrapper adds only its body to `harness_isr_cycles`; `harness_trap_calib()` kept, `trap_overhead` printed for reference (98). Verilator, defaults: no negative samples; FBW `check_failsafe_task` 34 / 2258 / 119, `send_data_to_autopilot_task` 33 / 1353 / 451, `test_ppm_task` 46 / 7707 / 3311, `servo_transmit` 825 / 1257 / 989; Autopilot `link_fbw_send` 108 / 195 / 112, `radio_control_task` 291 / 1492 / 429. Checked with temporary debug code (removed): `check_failsafe_task` clean-sample min is 34, only 2/60 samples hit; the old 22 was a hit sample over-subtracted by ~12 cycles; `test_ppm_task` 44/60 hit, clean min 46; `servo_transmit` hit on all 19 (in step with the virtual SPI frame every 3 ticks). README (results, output format, Known limitations), `port-harness.md` updated | |
| 2026-09-24 | (same request) | Long run (`TICKS=600`, 516 M cycles): minima now equal the tasks' shortest paths (`link_fbw_send` 108, `radio_control_task` 291, `receive_gps_data_task` 282, `navigation_task` 2082 — was 1910, also over-subtracted); `receive_gps_data_task` max 13136, `navigation_task` 6299, `climb_control_task` 1668. README long-run table refreshed | |

---

## To-do

- [ ] Decide with the user: libgcc is rv32imc-only (compressed instructions inside soft-float helpers, executed by both FBW and Autopilot) — accept, or build an rv32im libgcc.
- [x] Decide with the user the input stimulus: without it several tasks take their short path (e.g. `altitude_control_task` ~33 cycles because `pprz_mode` is not AUTO2/HOME) and `radio_control_task`/`receive_gps_data_task` are never activated (no SPI/UART ISR sets `link_fbw_receive_complete`/`gps_msg_received`). Options: preset state (modes, flags, buffers), call the `SIGNAL` handlers (`__vector_N`) from timer ticks, or measure as-is. → Done 2026-09-23 with real interrupts + deterministic environment + virtual other MCU (task "Real interrupts and peripherals").
- [x] Confirm the scenario with the user: radio sticks, GPS circle, ADC values, virtual-MCU frames (mode MANUAL→AUTO1→AUTO2, full throttle) are my choices (`gen_stimulus.py`, `*_periph.c`). → accepted, described in README section 7 "The simulated scenario" (user, 2026-09-23).
- [x] `altitude_control_task` long path needs > 8 s of flight after take-off (flight plan block 0): decide whether to report a long run (TICKS ≈ 600) in the README. → yes, full results of the TICKS=600 run in the README (user, 2026-09-23).
- [x] Trap entry/exit cost of every interrupt is inside task samples (only handler bodies are subtracted); the ADC fires every 5200 cycles. Decide: subtract a calibrated per-interrupt constant, make the ADC optional, or keep raw. → keep raw, explained in the README Known limitations (user, 2026-09-23).
- [x] Decide with the user about FBW `servo_transmit`: never activated by the upstream two-program scheduler (`_20Hz` reset before `fbw_schedule()` tests `>= 3`). Leave faithful, or patch the condition. → patched (user, 2026-09-24), now in `patches/fbw_main_schedule.patch`.
- [x] Wire PapaBench to the real peripherals (next steps of the real-interrupts task): TimerA as AVR Timer1 (FBW servo `__vector_6`, Autopilot `link_fbw` `__vector_12`), servos on PWM channels 0–9, TimerB/TimerC as SPI and ADC event sources, UART TX for downlink, generic ISR cycle accounting + per-ISR stats. → done 2026-09-23 (downlink is the modem, not the UART: UART TX serves FBW's boot string).
- [x] Own simulation top with a deterministic stimulus generator (UBX stream on `uart_rx`, PPM / modem clock on `gp_i[0]` with an edge latch acknowledged through a `gp_o` bit), replacing the `uartdpi` RX input. → patch 0003 + `hw/rtl/papabench_env.sv`, 2026-09-23.
- [x] Other AVR timer registers stay frozen in RAM (e.g. `TCNT1`, read by `link_fbw.c` `OCR1A = TCNT1 + 200`); decide whether they should follow `mtime`. → `TCNT1`/`TCNT2`/`ICR1` refreshed from TimerA before each upstream ISR (only ISRs read them), 2026-09-23.
- [x] Autopilot image has ~2.6 KB left in the 56 KiB `ram` region of the stock `link.ld`: a harness linker script may be needed for `-O2` or more harness code. → `papabench_ibex/link.ld` (128 KiB), 2026-09-23.
- [x] Decide with the user whether the ISRs (`__vector_5/6/10` FBW, `__vector_5/12/17/30` Autopilot) are measured too. → measured since they run from real interrupts (`isr,` lines, plus ADC and UART TX handlers), 2026-09-23.
- [ ] Upstream `pp_sqrt()` (`sw/lib/c/math.c`) has its body under `#if 0` and returns garbage; used by `nav.c:159`. Decide whether to leave it (faithful to TACLeBench) or provide a working one via a patch.
- [x] FBW failsafe almost always active (see task "Run without measurements"): decide whether to patch FBW `main()` so `fbw_schedule()` runs once per tick (as in `PAPABENCH_SINGLE`), in both builds or only with `MEASURE=0`. → both builds (user, 2026-09-24), `patches/fbw_main_schedule.patch`.
- [x] FBW task samples can go negative (seen at HEAD `c09dd30`, with and without the servo patch, default run): `send_data_to_autopilot_task` max 4294967294 (= −2), `test_ppm_task` 4294967290 (= −6) without the patch; the `trap_overhead` (102) added per interrupt seems to exceed the real trap cost for some interrupts. The README results table predates the `trap_overhead` subtraction (min values differ). Investigate, then refresh the README table. After the once-per-tick patch: `check_failsafe_task` max 4294967295 (= −1), README row marked invalid. After the peripheral split (2026-09-24): `send_data_to_autopilot_task` −2 and `check_failsafe_task` −1 (the Autopilot's low `radio_control_task`/`link_fbw_send` samples seen at first were the SPI model race, fixed). → trap cost no longer subtracted, no negative samples (user, 2026-09-24).
- [ ] Verify the Ibex bus behaviour on an access to an unmapped low address (0x20–0xFF) — no longer blocking, since no SFR access reaches low memory.

---

## Completed

History that predates this file.

| Date | Commit | What |
|---|---|---|
| 2026-09-23 | `db6a049` | Added the `Secure-Ibex` submodule (`.gitmodules`, pinned at `b04c0643`). |
| upstream | `c6a0d73` | Last upstream TACLeBench commit included (README link removal). |
| upstream | `cd12e21` | Upstream: corrected dijkstra loop bounds. |
| upstream | `406036f` | Upstream: added missing PapaBench loop bounds. |
| upstream | `ffdc3f5` | Upstream: PapaBench code-style adjustments. |
