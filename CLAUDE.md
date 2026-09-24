# TACleBenchIbex — CLAUDE.md

Router: project identity, objective, hard constraints, and which file to open for which task.

---

## 1. Mandatory reading (every session)

1. This `CLAUDE.md`.
2. [`.claude/rules.md`](.claude/rules.md) — when and how to update `tree.md` and `status.md`.
3. [`.claude/tree.md`](.claude/tree.md) — map of the repository.
4. [`.claude/status.md`](.claude/status.md) — what was asked, what is done, what is open.

Then open only the reference file the task needs. Do not read all of `.claude/reference/` to "get an idea": it costs context and produces generic answers.

---

## 2. Objective

Run TACLeBench on our own Ibex SoC (the `Secure-Ibex` reference system, **standard Ibex core, no Smtctx isolation**). The focus is **PapaBench** (`bench/parallel/PapaBench/`): its two programs, **Fly-By-Wire** and **Autopilot**, are built and run **separately** (one run executes FBW only, another executes Autopilot only), on the **Verilator** simulation, measuring **`mcycle` per task**.

- **Owner:** Leonardo Fazzini (University of L'Aquila).
- **Project type:** bare-metal benchmark port (C, RV32) + measurement harness.
- **Upstream:** TACLeBench (WCET 2016 paper, `doc/20160705-wcet-falk.pdf`); PapaBench task list in `bench/parallel/PapaBench/PapaBench_for_wcet.txt`.
- **Platform:** `Secure-Ibex/` git submodule (`git@github.com:leonardofazzini/Secure-Ibex.git`), Verilator target `sim_ibex`.

**Hard constraints: `bench/` is upstream TACLeBench and read-only. `Secure-Ibex/` is a submodule with its own `CLAUDE.md` and workflow — do not edit it from this repo (not even build artifacts). New code (harness, Makefiles, linker scripts, scripts, patches) goes in `papabench_ibex/`. SoC hardware changes are patches in `papabench_ibex/hw/patches/`, applied by `make sim` to a copy in `build/sim/hw/` — never edits or commits in the submodule.**

---

## 3. Routing

| If you need to… | Open… |
|---|---|
| Understand PapaBench: tasks, entry points, macros, AVR layer, float math | [`.claude/reference/papabench.md`](.claude/reference/papabench.md) |
| Know what we use from Secure-Ibex: memory map, IRQ lines, `common/` lib, crt0, link.ld, Verilator build/run, SoC hardware patches, `mcycle` | [`.claude/reference/ibex-platform.md`](.claude/reference/ibex-platform.md) |
| Design or change the port/harness (SFR remap, timer emulation, per-task measurement, FBW vs Autopilot builds) | [`.claude/reference/port-harness.md`](.claude/reference/port-harness.md) |
| Build, run, debug; coding style; step-by-step recipes | [`.claude/reference/conventions.md`](.claude/reference/conventions.md) |
| Find where a file lives | [`.claude/tree.md`](.claude/tree.md) |
| Know what is done and what is open | [`.claude/status.md`](.claude/status.md) |
| How to run PapaBench and what was done (for humans); upstream TACLeBench citation | [`README.md`](README.md) |
| Add, rename, move or delete a file | update `.claude/tree.md` **first** |
| Close a step or open a to-do | update `.claude/status.md` **first** |

---

## 4. Invariants worth knowing before touching anything

- **AVR peripheral registers are plain memory accesses at addresses 0x20–0xFF.** In C, `arch/include/avr/arch/sfr_defs.h` defines `_SFR_IO8(x)` as `_MMIO_BYTE( (x) + 0x20 )` = `*(volatile uint8_t *)(x + 0x20)`, and registers like `TIFR`, `SPDR`, `TCNT1` are used that way. The `+ 0x20` is hard-coded: `__SFR_OFFSET` only matters under `_SFR_ASM_COMPAT`, so the PapaBench `README` advice to set `SFR_OFFSET` does not fix anything. On the Ibex SoC RAM starts at `0x00100000` and nothing is mapped at 0x0–0xFF, so `papabench_ibex/include/arch/sfr_defs.h` shadows the upstream header (it must stay first in `-I`) and redirects `_MMIO_BYTE`/`_MMIO_WORD` into `papabench_sfr[]`. How the bus reacts to an unmapped access is `TODO: verify` (no longer reached).
- **Each program's own upstream scheduler runs, paced by the Ibex machine timer (`0x80000000`, IRQ 7).** `timer_periodic()` (both `sw/airborne/*/timer.h`) polls `TIFR.TOV2` and "clears" it by writing 1, which on plain RAM would set it. So `harness_timer_isr` sets `papabench_tick_pending` every `TICK_CYCLES` (default 819200 = 16.384 ms at 50 MHz), and our `sfr_defs.h` turns `bit_is_set( TIFR, TOV2 )` into a test-and-clear of that flag. Upstream `main()` is renamed `papabench_upstream_main` and called by the harness.
- **AVR peripherals are models on real Ibex peripherals; upstream ISRs run from real interrupts.** `harness/periph.c` + `harness/{fbw,autopilot}_periph.c` read what the code wrote in `papabench_sfr[]` at deterministic sync points (end of every IRQ wrapper; idle loop via `papabench_tick_take()`) and drive the real SoC. **One peripheral, one function**: no Ibex peripheral serves two different functions across FBW and Autopilot (map in `harness/ibex_io.h`): TimerA FBW servo compare, TimerB ADC (both), TimerC SPI link (both), TimerD Autopilot `link_fbw` compare, TimerE FBW virtual UART (bytes dropped), UART RX GPS, `gp_i[0]`/IRQ 17 PPM, `gp_i[1]`/IRQ 21 modem clock (edge latches acked by toggling `gp_o[0]`/`gp_o[6]`), PWM servos only (logged to `pwm.log`). Each model lists the IRQs it owns in `papabench_irqs[]` and touches no other peripheral. Upstream `__vector_N` are called through `papabench_isr_run()` (per-ISR `isr,` lines). The other MCU is virtual (fixed frames over the SPI model). `SPDR` accesses go through `papabench_spdr_access()` (write/read are different registers on the AVR). `OCR1A` written by an ISR is measured from the counter snapshot the ISR read, never from "now". Details: `.claude/reference/port-harness.md` → Peripheral models.
- **The two programs are separate by design; never define `PAPABENCH_SINGLE`.** Each has its own `main()` (FBW in `fly_by_wire/main.c`, Autopilot in `autopilot/mainloop.c`) with a `while ( 1 )` loop unless `NO_MAINLOOP` is defined. `PAPABENCH_SINGLE` merges FBW into the Autopilot executable, which is exactly what this project does not want.
- **Both programs need soft-float, which lives in libgcc — and the only libgcc is rv32imc.** FBW (`servo.c`) and the Autopilot (`pid.c`, `estimator.c`, `nav.c`, `sw/lib/c/math.c`) use `float`/`double`; the core has no FPU, so the build links `-lgcc`. The installed toolchain has a single `rv32imc` multilib, so `__adddf3`, `__muldf3`, `__udivdi3` … execute compressed instructions inside our rv32im binaries. libgcc is allowed; libc and heap are not (`harness/runtime.c` provides `memcpy`/`memset`). The link uses `papabench_ibex/link.ld` (whole 128 KiB RAM); the Autopilot no longer fits the stock 56 KiB `link.ld`.
- **The simulated SoC is Secure-Ibex plus our patches.** `make sim` builds `papabench:soc:reference_system` (`papabench_ibex/hw/*.core`) from submodule files copied to `build/sim/hw/` and patched there: TimerC (`0x80030000`, IRQ 20), TimerD/TimerE (`0x80040000`/`0x80050000`, IRQ 22/23), `gp_i[1]` as IRQ 21, a 20-bit PWM counter (8 upstream), and a simulation top that instantiates `papabench_env` and a PWM monitor (`hw/rtl/`), which drives `gp_i` and `uart_rx` with deterministic stimuli (PPM, modem clock, GPS UBX; tables generated by `hw/stimulus/gen_stimulus.py`). Changing the stimulus needs `make sim`. A patch that stops applying after a submodule update fails the build on purpose. `make hwtest` checks the patches. Details: `.claude/reference/ibex-platform.md` → SoC patches.
- **ISA is `rv32im`, not the Secure-Ibex default.** `common.mk` sets `ARCH ?= rv32imc`; this project overrides it with `ARCH=rv32im` (no compressed instructions).
- **Per-task timing relies on `-finstrument-functions` applied only to task boundaries.** The Makefile builds an exclude list of every other PapaBench function (plain build + `nm`) from the `PAPABENCH_TASK( "name", first, last ),` lines of the glue files; a function with hooks that is not a task boundary adds cycles inside task samples. Samples include the printed `overhead`. Details: `.claude/reference/port-harness.md`.
- **Loop-bound pragmas and `_Pragma( "entrypoint" )` are sacred.** They are WCET flow facts (`_Pragma( "loopbound min 8 max 8" )` etc., cross-checked in `Loops_Bounds.txt`). Build with `-Wno-unknown-pragmas`; never delete or edit them.
- **The AVR device macro selects the register header.** `arch/io.h` includes `iom8.h` or `iom128.h` only if `__AVR_ATmega8__` / `__AVR_ATmega128__` is defined; otherwise it just warns and `TIFR` etc. are undefined. Verified: FBW → ATmega8, Autopilot → ATmega128 (+ `-DUBX`). FBW includes the headers bare (`<io.h>`, `<signal.h>`), the Autopilot with the `arch/` prefix, so both include paths are needed.
- **Upstream PapaBench does not compile as-is with a modern GCC, and three Autopilot tasks do not exist as functions.** `autopilot/main.c` has a `ModeUpdate(...); else` syntax error (fixed by `papabench_ibex/patches/`, applied to a build copy), needs `-fgnu89-inline` and `-fcommon`; `pp_sqrt()` returns garbage (body under `#if 0`); FBW upstream `main()` calls `fbw_schedule()` every loop iteration while its timeouts count ticks (servos stuck in failsafe) and never reaches `servo_transmit` (`_20Hz` reset before `fbw_schedule()` sees 3); both fixed by `patches/fbw_main_schedule.patch` (once per tick, as `PAPABENCH_SINGLE`). `navigation_task`, `reporting_task`, `receive_gps_data_task` are rebuilt in `harness/autopilot_glue.c` from the pieces `periodic_task()` and `mainloop.c` inline. Details: `.claude/reference/papabench.md`.

---

## 5. Notes

- `README.md` (root) is for humans: how to build/run PapaBench on Ibex, results, what was done, known limitations, followed by the upstream TACLeBench README. Keep its commands, results table and limitations in sync when they change. `.claude/reference/` is for the agent. When they diverge, the code wins, then `.claude/reference/`.
- The Verilator simulator is built from the `Secure-Ibex/` submodule into `papabench_ibex/build/sim/` (`make sim`, FuseSoC `--build-root`); the submodule stays clean.
- `Secure-Ibex/CLAUDE.md` governs the submodule; its rules (e.g. its own `tree.md`/`status.md`) apply only when working inside it, which this repo does not do.
- Language: chat with the user in **Italian**; code, comments, docs and commit messages in **English**.
