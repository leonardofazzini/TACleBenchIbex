// Smoke test of the PapaBench SoC patches (hw/patches/), run by 'make hwtest'.
//
// TimerC (0x80030000, fast IRQ 20): mtime counts, a compare raises IRQ 20 and
// no other timer IRQ (18, 19) fires; 4 periodic interrupts, each taken after
// its compare time. PWM channel 0 is programmed with a period and a pulse
// wider than 16 bits; its output is checked on the waveform ('make hwtest'
// runs with -t), since the PWM cannot be read back.
//
// Prints PASS/FAIL lines to the SimCtrl log and halts.

#include <stdint.h>

#include "pwm.h"
#include "reference_system_common.h"
#include "reference_system_regs.h"

#define TIMERC_BASE 0x80030000
#define TIMERC_IRQ_NUM 20

#define PERIOD 2000u
#define NPERIODS 4u

// PWM channel 0: 100000-cycle period (> 2^16), 75000-cycle pulse
#define PWM_PERIOD 100000u
#define PWM_PULSE 75000u

static volatile uint32_t hw_timerc_count;
static volatile uint32_t hw_timerc_late;  // taken before its compare time
static volatile uint32_t hw_timerc_latency_max;
static volatile uint32_t hw_other_count;
static uint32_t hw_timerc_next;

static uint32_t hw_mtime_c( void ) {
  return DEV_READ( TIMERC_BASE + TIMER_MTIME );
}

static void hw_timecmp_c( uint32_t t ) {
  DEV_WRITE( TIMERC_BASE + TIMER_MTIMECMP, -1 );
  DEV_WRITE( TIMERC_BASE + TIMER_MTIMECMPH, 0 );
  DEV_WRITE( TIMERC_BASE + TIMER_MTIMECMP, t );
}

void hw_timerc_isr( void ) __attribute__( ( interrupt ) );
void hw_timerc_isr( void ) {
  uint32_t now = hw_mtime_c();
  uint32_t lat = now - hw_timerc_next;
  if ( ( int32_t )lat < 0 )
    hw_timerc_late++;
  if ( lat > hw_timerc_latency_max )
    hw_timerc_latency_max = lat;
  hw_timerc_count++;
  if ( hw_timerc_count < NPERIODS ) {
    hw_timerc_next += PERIOD;
    hw_timecmp_c( hw_timerc_next );
  } else {
    // disarm: all ones never matches, the write also clears the IRQ
    DEV_WRITE( TIMERC_BASE + TIMER_MTIMECMP, -1 );
    DEV_WRITE( TIMERC_BASE + TIMER_MTIMECMPH, -1 );
  }
}

void hw_other_isr( void ) __attribute__( ( interrupt ) );
void hw_other_isr( void ) {
  hw_other_count++;
  disable_interrupts( ( 1u << 18 ) | ( 1u << 19 ) );
}

static int hw_check( const char *what, int ok ) {
  puts( ok ? "PASS " : "FAIL " );
  puts( what );
  puts( "\n" );
  return ok ? 0 : 1;
}

int main( void ) {
  int fails = 0;
  uint32_t t0, t1, start, deadline;

  puts( "hw_smoke: PapaBench SoC patches\n" );

  // TimerC counts
  t0 = hw_mtime_c();
  t1 = hw_mtime_c();
  fails += hw_check( "TimerC mtime increments", t1 > t0 );

  // PWM channel 0, 20-bit counter (checked on the waveform)
  set_pwm( PWM_FROM_ADDR_AND_INDEX( PWM_BASE, 0 ), PWM_PERIOD, PWM_PULSE );

  // TimerC periodic interrupt on fast IRQ 20; 18/19 must stay quiet
  install_exception_handler( TIMERC_IRQ_NUM, &hw_timerc_isr );
  install_exception_handler( 18, &hw_other_isr );
  install_exception_handler( 19, &hw_other_isr );
  hw_timerc_next = hw_mtime_c() + PERIOD;
  hw_timecmp_c( hw_timerc_next );
  enable_interrupts( ( 1u << TIMERC_IRQ_NUM ) | ( 1u << 18 ) | ( 1u << 19 ) );
  set_global_interrupt_enable( 1 );

  start = hw_mtime_c();
  deadline = start + ( NPERIODS + 2 ) * PERIOD;
  while ( hw_timerc_count < NPERIODS && ( int32_t )( hw_mtime_c() - deadline ) < 0 )
    ;
  // let a spurious extra interrupt show up, if any
  while ( ( int32_t )( hw_mtime_c() - ( start + ( NPERIODS + 2 ) * PERIOD ) ) < 0 )
    ;
  set_global_interrupt_enable( 0 );

  fails += hw_check( "TimerC IRQ 20: 4 periodic interrupts, no extra",
                     hw_timerc_count == NPERIODS );
  fails += hw_check( "TimerC IRQ 20 never taken before its compare time",
                     hw_timerc_late == 0 );
  fails += hw_check( "TimerA/TimerB IRQ 18/19 not raised", hw_other_count == 0 );
  puts( "TimerC max latency (cycles): 0x" );
  puthex( hw_timerc_latency_max );
  puts( "\n" );

  // Keep running for two PWM periods so the waveform shows them
  start = hw_mtime_c();
  while ( hw_mtime_c() - start < 2 * PWM_PERIOD + 1000 )
    ;

  puts( fails ? "hw_smoke: FAIL\n" : "hw_smoke: PASS\n" );
  sim_halt();
  return 0;
}
