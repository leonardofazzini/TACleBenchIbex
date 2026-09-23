# PapaBench-on-Ibex port and measurement harness (`papabench_ibex/`)

Status: FBW and Autopilot each run their **own upstream scheduler**, paced by the Ibex machine timer, on Verilator (standard Ibex); `mcycle` is recorded per task activation. Open decisions are in `.claude/status.md` → To-do.

## Requirements (decided with the user)

- Two independent executables: `fbw.elf` and `autopilot.elf`. One run = one program. `PAPABENCH_SINGLE` is never defined.
- Target: standard Ibex (`sim_ibex`) on Verilator only. No Smtctx.
- ISA/ABI: `-march=rv32im -mabi=ilp32`; no libc, no heap; libgcc allowed (soft-float).
- Scheduler time base: HW timer `0x80000000` (IRQ 7); tick = AVR Timer2 period (16.384 ms) at 50 MHz = 819200 cycles (2026-09-23).
- Output: `mcycle` per task, tasks activated by the upstream scheduler (the earlier direct-call table mode was replaced, 2026-09-23).
- Sources in `bench/` are compiled unmodified; the only source change is a patch applied to a build-dir copy. Loop-bound and entry-point pragmas stay untouched.

## Architecture

Two sides that never include each other's headers (PapaBench `inttypes.h` makes `uint32_t` an `unsigned long`, newlib an `unsigned int`):

| Side | Files | Includes | Flags |
|---|---|---|---|
| PapaBench | upstream `.c` (instrumented), `harness/{fbw,autopilot}_glue.c` | `include/` (our `sfr_defs.h`) first, then PapaBench | `PB_CFLAGS` (+ `INSTR_FLAGS` for upstream files) |
| Ibex | `harness/harness.c`, `harness/runtime.c`, `harness/calib.c`, Secure-Ibex `common/{reference_system_common,timer,uart}.c`, `hello_test/crt0.S` | Secure-Ibex `common/` | `IBEX_CFLAGS` |

They meet only through `harness/papabench_harness.h` (no fixed-width types): `papabench_tasks[]` (`{ name, first, last }`), `papabench_ntasks`, `papabench_prog_name[]`, `papabench_startup_ticks`, `papabench_check()`, `papabench_upstream_main()`, `papabench_calib()`, `papabench_tick_pending`.

Run flow (`harness.c` `main()`): `pcount_enable` → header line → `papabench_check()` → fill stats from the task table → 8 calls to `papabench_calib()` (overhead) → `install_exception_handler( 7, harness_timer_isr )`, `mtimecmp = mtime + TICK_CYCLES`, enable IRQ 7 and `mstatus.MIE` → `papabench_upstream_main()` (never returns). The ISR, after `TICKS + papabench_startup_ticks` ticks, prints the report and calls `sim_halt()`.

## Time base: Ibex timer → AVR Timer2 overflow

- Upstream schedulers call `timer_periodic()`, which tests `bit_is_set( TIFR, TOV2 )` and then writes `TIFR = 1 << TOV2` (AVR write-1-to-clear).
- `harness_timer_isr()` (`__attribute__((interrupt))`): `timecmp += TICK_CYCLES` (from the previous compare, so no drift), `timecmp_update()`, `papabench_tick_pending = 1`, count the tick, add its own body cycles to `harness_isr_cycles`.
- `include/arch/sfr_defs.h`: `bit_is_set( sfr, bit )` is `papabench_tick_take()` (test-and-clear of `papabench_tick_pending`, `runtime.c`) when `_SFR_ADDR( sfr ) == PAPABENCH_TIFR_ADDR && bit == PAPABENCH_TICK_BIT`, a plain memory test otherwise. The address test folds at compile time (verified in the FBW disassembly: one call to `papabench_tick_take`, inside the inlined `timer_periodic()`). The upstream "clear" write lands in `papabench_sfr[]` and is ignored.
- `PAPABENCH_TIFR_ADDR` comes from the Makefile (FBW ATmega8 `0x58`, Autopilot ATmega128 `0x56`), `PAPABENCH_TICK_BIT=6`; `papabench_check()` in the glue verifies both against the device header at run time.
- Resulting activation pattern over 61 ticks (verified): FBW loops `fbw_schedule()` continuously (~370 iterations per tick at `-Os`); Autopilot `periodic_task()` once per tick → navigation/altitude/climb every 15 ticks (4), `stabilisation_task`/`link_fbw_send` every 3 (20), reporting every 6 (10). The Autopilot first consumes 30 ticks in `mainloop.c`'s init wait (`papabench_startup_ticks`).

## Per-task measurement: selective `-finstrument-functions`

- Only the `first`/`last` function of each `PAPABENCH_TASK(...)` entry is instrumented. Makefile pipeline per program:
  1. `build/<prog>/plain/*.o`: PapaBench sources compiled with `-fkeep-inline-functions -fkeep-static-functions` (only for `nm`).
  2. `instrumented.txt`: `first`/`last` names parsed with `sed` from the `PAPABENCH_TASK( "name", first, last ),` lines of the glue file (one per line, this exact form).
  3. `functions.txt` (all `T`/`t` symbols of the plain objects) minus `instrumented.txt` → `exclude.txt`. The build fails if a task function is missing, or if an exclude entry is a substring of a task name (GCC's `-finstrument-functions-exclude-function-list` matches substrings).
  4. `build/<prog>/pb/*.o`: compiled with `-finstrument-functions -finstrument-functions-exclude-function-list=<exclude.txt>`; these are linked.
- Hooks (`harness.c`): `__cyg_profile_func_enter` finds the task whose `first` matches, stores it in `harness_open`, reads the start; `__cyg_profile_func_exit` reads the end first, then accepts only `fn == harness_open->last` (O(1), no search inside the window). Tasks never nest, so one open task is enough.
- `harness_now()` reads (`harness_isr_cycles`, `mcycle`) as a consistent pair (retry if the ISR fired between the reads); sample = Δmcycle − ΔISR-body cycles.
- `calib.c` is compiled with the same instrumentation; its minimum over 8 calls is printed as `overhead` (23 cycles at `-Os`) and is **included** in every sample.
- Composite Autopilot tasks: `navigation_task` = `estimator_propagate_state` … `course_run`, `reporting_task` = `send_boot` … `send_nav_ref`, `receive_gps_data_task` = `parse_gps_msg` … `send_takeOff`; the calls in between are not instrumented and are part of the sample.

## Upstream compile problems (unchanged from the first port)

- `autopilot/main.c`: `ModeUpdate(...); else` → `patches/autopilot_main_modeupdate.patch`, applied to `build/autopilot/patched/main.c`.
- `ad7714.c`, `gps_sirf.c` → not built. `ck_a`/`ck_b` → `-fcommon`. Non-static `inline` → `-fgnu89-inline`.
- Upstream `main()` of FBW (`fly_by_wire/main.c`) and Autopilot (`autopilot/mainloop.c`) → `-Dmain=papabench_upstream_main` on that object (plain and instrumented).
- No libc → `runtime.c` provides `memcpy`/`memset`; `-lgcc` for soft-float and 64-bit division.

## Invariants

- Nothing under `bench/` or `Secure-Ibex/` is written; all outputs go to `papabench_ibex/build/<prog>/`.
- Every PapaBench function that is not a task boundary must be excluded from instrumentation, otherwise its hooks add cycles inside task samples. The generated lists enforce this; do not hand-edit `exclude.txt`.
- Samples are raw: they include `overhead`. A sample during which the tick fired also contains the ISR entry/exit cost (register save/restore, `mret`), which is not subtracted — this is why short FBW tasks show max ≈ min + ~90.
- `libgcc.a` in the only available toolchain is rv32imc: soft-float helpers execute compressed instructions (open decision).
- Size: the Autopilot uses ~54.7 KB of the 56 KiB `ram` region of the stock `link.ld` (~2.6 KB left).

## Traps

- Adding a Secure-Ibex header to a glue file, or a PapaBench header to `harness.c`/`runtime.c`/`calib.c`, breaks the build with conflicting `uint32_t` typedefs.
- A glue task entry split over several lines, or not in the `PAPABENCH_TASK( "name", first, last ),` form, is invisible to the Makefile: the table still compiles but the function gets no hooks and the task reports count 0.
- A new task whose `first` is also called outside the task sequence would open a window that the next `last` closes; check call sites before adding composite tasks.
- Headers with the same name exist in `autopilot/` and `fly_by_wire/` (`spi.h`, `uart.h`, `timer.h`): for the Autopilot, `-I autopilot` must precede `-I fly_by_wire`.
- Wall time: ~0.5–0.6 M simulated cycles/s, so the default FBW run (61 ticks, 50 M cycles) takes ~1.5 min and the Autopilot (91 ticks, 75 M cycles) ~2 min. Use `TICK_CYCLES` smaller for quick checks (must stay above the per-tick work, a few thousand cycles).
