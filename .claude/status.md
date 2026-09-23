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

---

## To-do

- [ ] Decide with the user: libgcc is rv32imc-only (compressed instructions inside soft-float helpers, executed by both FBW and Autopilot) — accept, or build an rv32im libgcc.
- [ ] Decide with the user the input stimulus: without it several tasks take their short path (e.g. `altitude_control_task` ~33 cycles because `pprz_mode` is not AUTO2/HOME) and `radio_control_task`/`receive_gps_data_task` are never activated (no SPI/UART ISR sets `link_fbw_receive_complete`/`gps_msg_received`). Options: preset state (modes, flags, buffers), call the `SIGNAL` handlers (`__vector_N`) from timer ticks, or measure as-is.
- [ ] Decide with the user about FBW `servo_transmit`: never activated by the upstream two-program scheduler (`_20Hz` reset before `fbw_schedule()` tests `>= 3`). Leave faithful, or patch the condition.
- [ ] If TimerA/TimerB are wanted later: they are in the `make sim` model (fast IRQ 18/19); make the timer base/IRQ a Makefile parameter.
- [ ] Other AVR timer registers stay frozen in RAM (e.g. `TCNT1`, read by `link_fbw.c` `OCR1A = TCNT1 + 200`); decide whether they should follow `mtime`.
- [ ] Autopilot image has ~2.6 KB left in the 56 KiB `ram` region of the stock `link.ld`: a harness linker script may be needed for `-O2` or more harness code.
- [ ] Decide with the user whether the ISRs (`__vector_5/6/10` FBW, `__vector_5/12/17/30` Autopilot) are measured too.
- [ ] Upstream `pp_sqrt()` (`sw/lib/c/math.c`) has its body under `#if 0` and returns garbage; used by `nav.c:159`. Decide whether to leave it (faithful to TACLeBench) or provide a working one via a patch.
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
