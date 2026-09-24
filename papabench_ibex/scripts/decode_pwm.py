#!/usr/bin/env python3
"""Decode the PWM log (pwm.log) into FBW servo frames.

The simulation top logs every PWM pulse (hw/rtl/papabench_pwm_monitor.sv):
one line '<rise> <channel> <width>' per pulse, in cycles since reset. The
FBW drives the servos on PWM channels 0-9 with a 20 ms period
(harness/fbw_periph.c); each channel's width is the pulse the 4017 decade
counter would give that servo.

One output line per 20 ms servo period: start time and the width of every
channel in us ('-' = no pulse in that period). Channel names come from
sw/var/include/airframe.h.

Usage: decode_pwm.py [pwm.log] [--every N]
"""

import argparse

NB_SERVOS = 10
CPU_HZ = 50_000_000
SERVO_PERIOD = CPU_HZ // 50

# airframe.h: SERVO_<name> index; unnamed channels are unused
NAMES = {0: "ail_L", 2: "ail_R", 3: "mot_L", 6: "elev", 7: "rudder",
         9: "mot_R"}


def main():
  ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  ap.add_argument("log", nargs="?", default="pwm.log")
  ap.add_argument("--every", type=int, default=1,
                  help="print one servo period out of N (default 1)")
  args = ap.parse_args()

  pulses = []
  for line in open(args.log):
    if line.startswith("#") or not line.strip():
      continue
    rise, ch, width = (int(x) for x in line.split())
    if ch < NB_SERVOS:
      pulses.append((rise, ch, width))
  if not pulses:
    print("no servo pulses")
    return

  # Channel counters start a few cycles apart: a period is the 20 ms window
  # from the first pulse of the run
  t0 = min(p[0] for p in pulses)
  frames = {}
  for rise, ch, width in pulses:
    frames.setdefault((rise - t0) // SERVO_PERIOD, {})[ch] = width

  header = ["period", "t_ms"] + [NAMES.get(c, "ch%d" % c)
                                 for c in range(NB_SERVOS)]
  print("servo widths in us")
  print(" ".join("%6s" % h for h in header))
  for n in sorted(frames):
    if n % args.every:
      continue
    f = frames[n]
    t = (t0 + n * SERVO_PERIOD) * 1000.0 / CPU_HZ
    cols = ["%6.0f" % (f[c] * 1e6 / CPU_HZ) if c in f else "%6s" % "-"
            for c in range(NB_SERVOS)]
    print("%6d %6.0f " % (n, t) + " ".join(cols))


if __name__ == "__main__":
  main()
