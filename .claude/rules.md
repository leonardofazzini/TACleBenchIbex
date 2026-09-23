# rules.md — protocol for `tree.md`, `status.md` and reference files

Instructions to myself for every future session.

## Two files, two triggers

- `tree.md` changes when the **structure** changes: a file or directory is added, renamed, moved or deleted.
- `status.md` changes when a **task** moves: a step is completed, a decision is taken, or the user corrects something.
- Most actions touch both (e.g. creating the harness Makefile adds a line to `tree.md` and a row to `status.md`).

## The failure mode is forgetting

As soon as an action changes structure or state, record it, **then** continue. Not at the end of the session, not "after this last thing".

## Do not log noise

Reading, searching, exploring and reasoning are not events. Only what changes state or structure gets recorded.

## Before writing

- Read the current `tree.md` and `status.md` first, to avoid duplicates and to **match the format already in use** — continue it, do not impose a new one.
- Dates are `YYYY-MM-DD`, obtained with `date -I`, never guessed.

## `tree.md` mechanics

- Add the line in the right section, keeping the existing order.
- A rename or move **edits the existing line**; it does not add a second one.
- A deletion removes the line.
- If I do not know what a file contains, write `TODO: describe`. Never invent a description.

Example — the harness Makefile was created, then renamed:

```
- `papabench_ibex/Makefile`: builds `fbw.elf` or `autopilot.elf` (`PROG=fbw|autopilot`), rv32im + `-lgcc`.
```

becomes, after the rename, the same single line with the new path:

```
- `papabench_ibex/fbw/Makefile`: builds `fbw.elf`, rv32im + `-lgcc`.
```

## `status.md` mechanics

- A new, distinct request from the user → a new `## Task: <short name> — YYYY-MM-DD` section with its own table.
- A step inside an open task → a new row in that task's table.
- A correction from the user → fill that row's **User revision** column. Never rewrite or delete the row.

Example — a fresh row:

```
| 2026-09-24 | Make FBW link on rv32im | Added `sfr_ram[]` backing array, `-D__SFR_OFFSET=0`, linked `-lgcc`; `fbw.elf` builds | |
```

The same row after the user corrected it:

```
| 2026-09-24 | Make FBW link on rv32im | Added `sfr_ram[]` backing array, `-D__SFR_OFFSET=0`, linked `-lgcc`; `fbw.elf` builds | SFR array must live in its own section at a fixed address, not in `.bss` |
```

## Reference-file mechanics

- Reference files describe **how the system works**, so they change when the system changes, not when a task advances — and in the same step that changed the code.
- If a reference file and the code disagree, the code wins; fix the reference file.
- A new subsystem big enough for its own file also gets a routing row in `CLAUDE.md` and a line in `tree.md`.

## What not to do

- Do not rewrite history or tidy old tasks in `status.md`.
- Do not invent progress to fill a table.
- Do not touch the constrained paths in `CLAUDE.md`: `bench/` (read-only upstream) and `Secure-Ibex/` (submodule with its own workflow).
- Do not scaffold these files from scratch if they already exist — align them.
