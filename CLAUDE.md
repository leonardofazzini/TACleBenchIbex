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

**Hard constraints: `bench/` is upstream TACLeBench and read-only. `Secure-Ibex/` is a submodule with its own `CLAUDE.md` and workflow — do not edit it from this repo. New code (harness, Makefiles, linker scripts, scripts) goes in a new top-level directory.**

---

## 3. Routing

| If you need to… | Open… |
|---|---|
| Understand PapaBench: tasks, entry points, macros, AVR layer, float math | [`.claude/reference/papabench.md`](.claude/reference/papabench.md) |
| Know what we use from Secure-Ibex: memory map, `common/` lib, crt0, link.ld, Verilator build/run, `mcycle` | [`.claude/reference/ibex-platform.md`](.claude/reference/ibex-platform.md) |
| Design or change the port/harness (SFR remap, timer emulation, per-task measurement, FBW vs Autopilot builds) | [`.claude/reference/port-harness.md`](.claude/reference/port-harness.md) |
| Build, run, debug; coding style; step-by-step recipes | [`.claude/reference/conventions.md`](.claude/reference/conventions.md) |
| Find where a file lives | [`.claude/tree.md`](.claude/tree.md) |
| Know what is done and what is open | [`.claude/status.md`](.claude/status.md) |
| Upstream TACLeBench overview and citation (for humans) | [`README.md`](README.md) |
| Add, rename, move or delete a file | update `.claude/tree.md` **first** |
| Close a step or open a to-do | update `.claude/status.md` **first** |

---

## 4. Invariants worth knowing before touching anything

- **AVR peripheral registers are plain memory accesses at addresses 0x20–0xFF.** In C, `arch/include/avr/arch/sfr_defs.h` defines `_SFR_IO8(x)` as `_MMIO_BYTE( (x) + 0x20 )` = `*(volatile uint8_t *)(x + 0x20)`, and registers like `TIFR`, `SPDR`, `TCNT1` are used that way. The `+ 0x20` is hard-coded: `__SFR_OFFSET` only matters under `_SFR_ASM_COMPAT`, so the PapaBench `README` advice to set `SFR_OFFSET` does not fix anything. On the Ibex SoC RAM starts at `0x00100000` and nothing is mapped at 0x0–0xFF, so the harness has to redirect `_MMIO_BYTE`/`_MMIO_WORD` to a RAM-backed array. How the bus reacts to the unmapped access is `TODO: verify`.
- **Time only advances if the harness makes it advance.** `timer_periodic()` (both `sw/airborne/*/timer.h`) polls the `TOV2` bit of `TIFR` and clears it. Nothing sets it on Ibex: the Autopilot `main()` in `mainloop.c` spins forever in its `while ( init_cpt )` wait loop, and in FBW `_20Hz` never reaches 3, so `servo_transmit()` never runs. The harness has to set the flag (or replace the scheduler) to get deterministic task activations.
- **The two programs are separate by design; never define `PAPABENCH_SINGLE`.** Each has its own `main()` (FBW in `fly_by_wire/main.c`, Autopilot in `autopilot/mainloop.c`) with a `while ( 1 )` loop unless `NO_MAINLOOP` is defined. `PAPABENCH_SINGLE` merges FBW into the Autopilot executable, which is exactly what this project does not want.
- **The Autopilot needs soft-float, which lives in libgcc.** `pid.c`, `estimator.c`, `nav.c`, `sw/lib/c/math.c` use `float`/`double`; the core has no FPU. Secure-Ibex `common.mk` links with `-nostdlib` and no `-lgcc`, so `__adddf3`-style helpers stay undefined unless the build adds `-lgcc`. libgcc is allowed; libc and heap are not.
- **ISA is `rv32im`, not the Secure-Ibex default.** `common.mk` sets `ARCH ?= rv32imc`; this project overrides it with `ARCH=rv32im` (no compressed instructions).
- **Loop-bound pragmas and `_Pragma( "entrypoint" )` are sacred.** They are WCET flow facts (`_Pragma( "loopbound min 8 max 8" )` etc., cross-checked in `Loops_Bounds.txt`). Build with `-Wno-unknown-pragmas`; never delete or edit them.
- **The AVR device macro selects the register header.** `arch/io.h` includes `iom8.h` or `iom128.h` only if `__AVR_ATmega8__` / `__AVR_ATmega128__` is defined; otherwise it just warns and `TIFR` etc. are undefined. FBW includes the headers bare (`<io.h>`, `<signal.h>`), the Autopilot with the `arch/` prefix, so both include paths are needed. Which device goes with which program is `TODO: verify` (expected: FBW → ATmega8, Autopilot → ATmega128).

---

## 5. Notes

- `README.md` (root) is the upstream TACLeBench README, for humans; it says nothing about Ibex. `.claude/reference/` is for the agent and is the one kept current. When they diverge, the code wins, then `.claude/reference/`.
- `Secure-Ibex/CLAUDE.md` governs the submodule; its rules (e.g. its own `tree.md`/`status.md`) apply only when working inside it, which this repo does not do.
- Language: chat with the user in **Italian**; code, comments, docs and commit messages in **English**.
