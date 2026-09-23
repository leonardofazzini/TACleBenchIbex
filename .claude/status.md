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

---

## To-do

- [ ] Choose the top-level directory name for the harness (proposal: `papabench_ibex/`) and add it to `tree.md`.
- [ ] Verify which AVR device macro each program expects (FBW `__AVR_ATmega8__`, Autopilot `__AVR_ATmega128__`?) by compiling each with the other and checking for undefined registers.
- [ ] Decide how AVR SFR accesses are redirected to RAM without editing `bench/` (e.g. a harness-side `sfr_defs.h` shadowing the upstream one via include order, or a fixed-address RAM section) — see `reference/port-harness.md`.
- [ ] Decide how `TIFR.TOV2` is driven so `timer_periodic()` returns TRUE (set before each loop iteration vs. from the Ibex timer IRQ) and how many main-loop iterations one run executes.
- [ ] Decide how per-task `mcycle` is collected without editing the task functions (e.g. `-Wl,--wrap=<task>` wrappers, or a harness `main()` calling the entry points directly with `NO_MAINLOOP`).
- [ ] List exactly which `.c` files make up FBW and Autopilot (e.g. `gps_ubx.c` vs `gps_sirf.c`, whether `sw/lib/c/math.c` is needed by FBW).
- [ ] Verify the Ibex bus behaviour on an access to an unmapped low address (0x20–0xFF): error response, hang, or silent read of 0.
- [ ] Check the 56 KiB RAM + 8 KiB stack in `common/link.ld` fits `autopilot.elf` with soft-float libgcc.

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
