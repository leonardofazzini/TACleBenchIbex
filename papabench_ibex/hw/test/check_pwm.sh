#!/bin/sh
# Measures pwm_o[0] on a hw_smoke waveform and checks it against the values
# programmed by hw/test/hw_smoke.c: high for PULSE cycles, period PERIOD+1
# cycles (the PWM counter runs 0..PERIOD inclusive).
#
#   check_pwm.sh <sim.fst> [PERIOD] [PULSE]
#
# Streams fst2vcd through awk (the VCD is ~1 GB, it is never written), counts
# rising edges of clk_sys_i between pwm_o[0] transitions. The first pulse is
# skipped: the counter starts when the period is written and the width lands
# two cycles later, so it is short by 2. Measured: the second pulse and the
# period between the second and third rising edges. Exit 0 = PASS.

fst=${1:?usage: check_pwm.sh <sim.fst> [PERIOD] [PULSE]}
period=${2:-100000}
pulse=${3:-75000}

fst2vcd "$fst" 2>/dev/null | awk -v period="$period" -v pulse="$pulse" '
  # identifiers of the first clk_sys_i and pwm_o declarations
  $1 == "$var" && $5 == "clk_sys_i" && clk == "" { clk = $4 }
  $1 == "$var" && $5 == "pwm_o" && pwm == "" { pwm = $4 }
  $1 == "$enddefinitions" { hdr = 1; next }
  !hdr { next }
  $0 == "1" clk { cyc++; next }
  /^b/ && $2 == pwm {
    b = substr( $1, length( $1 ), 1 ) + 0
    if ( b == last ) next
    last = b
    if ( b == 1 ) {
      nrise++
      if ( nrise == 3 ) per_seen = cyc - rise
      rise = cyc
    } else if ( nrise == 2 ) {
      high_seen = cyc - rise
    }
  }
  END {
    printf "pwm_o[0]: high %d cycles (expected %d), period %d cycles (expected %d)\n",
      high_seen, pulse, per_seen, period + 1
    ok = ( high_seen == pulse && per_seen == period + 1 )
    print ( ok ? "PASS PWM 20-bit counter" : "FAIL PWM 20-bit counter" )
    exit ok ? 0 : 1
  }'
