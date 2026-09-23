# PapaBench-on-Ibex port and measurement harness

Status: **design only, no code yet.** This file records the requirements and the open decisions; update it in the same step that the harness code is written.

## Requirements (decided with the user, 2026-09-23)

- Two independent executables: `fbw.elf` and `autopilot.elf`. One run = one program. `PAPABENCH_SINGLE` is never defined.
- Target: standard Ibex (`sim_ibex`) on Verilator only. No Smtctx.
- ISA/ABI: `-march=rv32im -mabi=ilp32`; no libc, no heap; libgcc allowed (soft-float).
- Output: `mcycle` per task, printed over the Ibex UART/SimCtrl, for a fixed number of main-loop iterations.
- Sources in `bench/` are compiled unmodified. All adaptation lives in the new top-level harness directory (name `TODO: describe` until chosen; proposal `papabench_ibex/`).
- Loop-bound and entry-point pragmas stay untouched.

## Problems the harness must solve

1. **SFR accesses to 0x20–0xFF.** Options, none chosen yet:
   - Harness-side replacement for `sfr_defs.h` found first on the include path, defining `_MMIO_BYTE(a)` / `_MMIO_WORD(a)` as an index into a RAM array (e.g. `papabench_sfr[ a ]`). Upstream headers stay untouched; relies on include order. Must preserve `volatile`.
   - Keep addresses as-is but place a RAM region at a low address — impossible on this SoC without HW changes (RAM is at `0x00100000`), so rejected unless the HW changes.
2. **`timer_periodic()` never fires.** Set `TIFR |= _BV( TOV2 )` from the harness (once per loop iteration, or from the Ibex timer ISR). Remember that on plain memory the upstream "clear" writes `1 << TOV2`, i.e. keeps the flag set.
3. **Infinite main loops.** Either build with `NO_MAINLOOP` and call upstream `main()` N times (it re-runs init each time — probably wrong), or provide a harness `main()` that calls `fbw_init()`/`fbw_schedule()` (FBW) or replicates the Autopilot init + loop body. Autopilot `main()` is monolithic (`mainloop.c`), so a harness loop would duplicate its body — `TODO: verify` the cleanest option.
4. **Per-task timing without editing tasks.** Candidate: link with `-Wl,--wrap=servo_transmit,…` and time `__real_<task>` in `__wrap_<task>`. Caveat: `--wrap` only intercepts calls across translation units; FBW tasks are called from `fbw_schedule()` in the same file (`main.c`), so GCC may resolve them locally — `TODO: verify` (possibly needs `-fno-inline` / `-fno-ipa-*` or a different strategy such as calling the entry points directly from the harness).
5. **Type clash.** PapaBench's `inttypes.h` (`unsigned long`) vs newlib `stdint.h` (`unsigned int`): the harness file that talks to Secure-Ibex `common/` (UART, `PCOUNT_READ`) must not include PapaBench headers, and vice versa. Pass data across with `unsigned`-typed interfaces or `extern` declarations.
6. **Stimulus.** Without ISRs, `ppm_valid`, `spi_was_interrupted`, `gps_msg_received`, `link_fbw_receive_complete` stay 0, so several tasks take their short path. Whether to inject inputs (call `SIGNAL` handlers, preset buffers) is a measurement-design decision `TODO: describe` with the user.

## Invariants for whatever gets built

- Nothing under `bench/` or `Secure-Ibex/` is edited.
- Each measurement reads `mcycle` immediately before and after the task call; the print happens outside the measured window.
- Same compiler flags for all tasks of a run; flags are printed or recorded with the results.
