# status.md — task log

Git records what changed in the code. This file records what was asked, what was decided, and what is still open.

Format: one `## Task: <short name> — YYYY-MM-DD` section per distinct request, each with a table `| Date | Asked | Done | User revision |`. One row per completed step. A user correction goes in the **User revision** column of the row it corrects; rows are never rewritten or deleted. See `rules.md`.

---


> **Compacted 2026-09-28:** the complete previous log is preserved in [status-history.md](./status-history.md). The live log keeps the latest task and all open work below.

## Task: Move the SoC hardware to a Secure-Ibex branch (`system/papabench`) — 2026-09-25

Goal: `Secure-Ibex` became branch-friendly (`master` = core + reference SoC, `system/<name>` branches carry non-reference SoCs, merging `master` for core updates — `Secure-Ibex/doc/Branching.md`, submodule `3fbd2ea`). The local patch series (`papabench_ibex/hw/patches/*.patch`, applied by `make sim` to a copy in `build/sim/hw/`) becomes a `system/papabench` branch of the submodule instead: real commits, reviewed with `Secure-Ibex`'s own `CLAUDE.md`. Decisions (user): branch name `system/papabench`; move all the hardware (SoC additions **and** the simulation environment, PWM monitor, two-MCU top/testbench/stimulus generator, `hwtest`) — none of it stays as local patches; commit locally in both repos, no push (the user pushes `system/papabench` when ready).

| Date | Asked | Done | User revision |
|---|---|---|---|
| 2026-09-25 | Turn the patch system into a `system/papabench` branch | Created the branch from `master` (`3fbd2ea`), `.gitattributes` (`/README.md merge=ours`, `/.claude/status.md merge=union`) and `git config merge.ours.driver true` per `doc/Branching.md`. Ported every patch's effect straight into `rtl/system/reference_system_core.sv` (`NrDevices` 12→15: TimerD/E, `gpio_irq.sv` replacing `gpio.sv` on both banks incl. new bank 1 at `0x80060000`, `PwmCtrSize` parameter = 20, `SimCtrlLogName` parameter) and `rtl/top_simulation/reference_system.sv` (`papabench_env.sv`, `papabench_pwm_monitor.sv` instantiated, `gp1_i`/`gp1_o` wired, `uartdpi` TX left unconnected). Added `papabench_dual.sv`, `gen_papabench_stim.py` (+ generated `papabench_stim.svh`, verified identical to the old patch-generated one) under `rtl/top_simulation/`; `dv/papabench/papabench_dual.cc`; root `papabench_dual.core` (`riscv:soc:papabench_dual`); `sw/System_papabench/hw_smoke/` (`hw_smoke.c`, `crt0.S`, `check_pwm.sh`, `Makefile`, `README.md`) replacing `papabench_ibex/hw/test/`. `reference_system_core.core` now lists `gpio_irq.sv`; `reference_system.core`'s `additional_files_simulation` adds the environment/monitor/stimulus. Peripheral additions (`TIMERD_BASE` etc., `GPIO_IRQ_*_REG`) added to `sw/Standard_Only_SW/Standard_Tests/common/reference_system_regs.h` (no clash: `gpio.h`'s `GPIO_OUT_SHIFT_REG` at the same offset as our `IRQ_EN` is an unimplemented demo-system register on `gpio.sv`, not read by anything we touch). Branch `README.md` rewritten (system overview, block diagram, links); `rtl/system/README.md`, `.claude/tree.md`, `.claude/status.md` updated on the branch | |
| 2026-09-25 | (same request) | `TACleBenchIbex` side: submodule pointer moved `d5e2fb5` → `3fbd2ea` (checked out to `system/papabench`); `papabench_ibex/hw/` deleted; `Makefile` `sim`/`sim-dual` now run FuseSoC directly on the submodule (`riscv:soc:reference_system` / `riscv:soc:papabench_dual`), no staging or patching; new `check-branch` target (fails with a clear message if the submodule is not on `system/papabench`) and `checkout-branch` (switches it); `hwtest` builds `Secure-Ibex/sw/System_papabench/hw_smoke/hw_smoke.c`. `CLAUDE.md`, `tree.md`, `ibex-platform.md`, `port-harness.md`, `conventions.md`, root `README.md` updated (every `hw/patches`/`hw/rtl`/`hw/stimulus`/`hw/test`/`hw/dv` reference repointed to the branch) | |
| 2026-09-25 | (same request) | **Verified bit-for-bit**: built both simulators fresh from the old submodule commit (`d5e2fb5`, patch-based, matching the committed `TACleBenchIbex` state before this task) and from the new branch (`3fbd2ea`), then ran every configuration (`fbw`, `autopilot`, both `MEASURE=0`, `run-joint`, `run-joint MEASURE=0`, `hwtest`) on each. `Executed cycles`, every per-task/per-ISR `,` line, `pwm.log` (single-MCU and joint) and ELF `md5sum` are identical between the two builds in every configuration. `hwtest` PASS on both; its `pwm.log` differs by a constant 10-cycle offset because the smoke test's own banner string is one byte shorter on the branch (`hw_smoke: system/papabench SoC` vs `hw_smoke: PapaBench SoC patches`), not a hardware difference — `check_pwm.sh`'s pulse/period checks are unaffected | |

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
- [ ] SPI link, joint run: FBW's `__vector_10` must write the next byte within the Autopilot's 200 AVR clocks (625 cycles); our wrapper overhead can make it late when IRQ 21 arrives during another handler (seen once in `MEASURE=0 run-joint`: 1 frame of 20 rejected). Decide whether to accept (as now, counted in `link,` lines) or reduce FBW's interrupt latency.
- [ ] Decide whether to report a joint long run (`make run-joint TICKS=600`, ~30 min estimated wall time at 0.28 M cycles/s, not measured) in the README.
- [ ] Peripherals still shared by the two programs (user chose to separate only the GPIO for now, 2026-09-24): TimerB (ADC of both), the machine timer (scheduler tick of both, time base of the models), SimCtrl (results output). Decide whether to separate them before virtualising the two programs on one core.
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
