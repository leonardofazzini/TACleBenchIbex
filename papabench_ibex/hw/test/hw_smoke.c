// Smoke test of the PapaBench SoC patches (hw/patches/), run by 'make hwtest'.
//
// TimerC (0x80030000, fast IRQ 20), TimerD (0x80040000, IRQ 22), TimerE
// (0x80050000, IRQ 23), one after the other: mtime counts, 4 periodic
// interrupts on the timer's own line, each taken after its compare time, and
// no other line raised (TimerA/B 18/19, the other new timers, the GPIO lines
// 17/21).
// gp_i[1] (fast IRQ 21, patch 0004): with the Autopilot environment enabled
// (gp_o[2]) the modem clock sets the gp_i[1] latch; each interrupt is
// acknowledged by toggling gp_o[6]; the PPM line (IRQ 17) stays quiet.
// PWM channel 0 is programmed with a period and a pulse wider than 16 bits;
// its output is checked on the waveform and in pwm.log ('make hwtest' runs
// with -t), since the PWM cannot be read back.
//
// Prints PASS/FAIL lines to the SimCtrl log and halts.

#include <stdint.h>

#include "pwm.h"
#include "reference_system_common.h"
#include "reference_system_regs.h"

#define PERIOD 2000u
#define NPERIODS 4u

// Modem clock 4800 Hz: one falling edge every 10417 cycles
#define MODEM_EDGES 3u
#define MODEM_WAIT ( ( MODEM_EDGES + 1 ) * 10417u + 1000u )

// gp_o bits (harness/periph.h)
#define GPO_ENV_AUTOPILOT 0x04u
#define GPO_MODEM_ACK 0x40u

// PWM channel 0: 100000-cycle period (> 2^16), 75000-cycle pulse
#define PWM_PERIOD 100000u
#define PWM_PULSE 75000u

struct hw_timer {
  const char *name;
  uint32_t base;
  uint32_t irq;
};

static const struct hw_timer hw_timers[] = {
  { "TimerC", 0x80030000, 20 },
  { "TimerD", 0x80040000, 22 },
  { "TimerE", 0x80050000, 23 },
};

#define NTIMERS ( sizeof( hw_timers ) / sizeof( hw_timers[ 0 ] ) )

// Lines that must stay quiet while one timer is tested
static const uint32_t hw_quiet_irqs[] = { 17, 18, 19, 20, 21, 22, 23 };

#define NQUIET ( sizeof( hw_quiet_irqs ) / sizeof( hw_quiet_irqs[ 0 ] ) )

static const struct hw_timer *hw_cur;
static volatile uint32_t hw_timer_count;
static volatile uint32_t hw_timer_late;  // taken before its compare time
static volatile uint32_t hw_timer_latency_max;
static volatile uint32_t hw_other_count;
static volatile uint32_t hw_modem_count;
static uint32_t hw_timer_next;
static uint32_t hw_gpo;

static uint32_t hw_mtime( void ) {
  return DEV_READ( hw_cur->base + TIMER_MTIME );
}

static void hw_timecmp( uint32_t t ) {
  DEV_WRITE( hw_cur->base + TIMER_MTIMECMP, -1 );
  DEV_WRITE( hw_cur->base + TIMER_MTIMECMPH, 0 );
  DEV_WRITE( hw_cur->base + TIMER_MTIMECMP, t );
}

static void hw_gpo_toggle( uint32_t mask ) {
  hw_gpo ^= mask;
  DEV_WRITE( GPIO_BASE, hw_gpo );
}

void hw_timer_isr( void ) __attribute__( ( interrupt ) );
void hw_timer_isr( void ) {
  uint32_t now = hw_mtime();
  uint32_t lat = now - hw_timer_next;
  if ( ( int32_t )lat < 0 )
    hw_timer_late++;
  if ( lat > hw_timer_latency_max )
    hw_timer_latency_max = lat;
  hw_timer_count++;
  if ( hw_timer_count < NPERIODS ) {
    hw_timer_next += PERIOD;
    hw_timecmp( hw_timer_next );
  } else {
    // disarm: all ones never matches, the write also clears the IRQ
    DEV_WRITE( hw_cur->base + TIMER_MTIMECMP, -1 );
    DEV_WRITE( hw_cur->base + TIMER_MTIMECMPH, -1 );
  }
}

void hw_other_isr( void ) __attribute__( ( interrupt ) );
void hw_other_isr( void ) {
  uint32_t cause;

  asm volatile( "csrr %0, mcause" : "=r"( cause ) );
  hw_other_count++;
  disable_interrupts( 1u << ( cause & 0x1F ) );
}

void hw_modem_isr( void ) __attribute__( ( interrupt ) );
void hw_modem_isr( void ) {
  hw_gpo_toggle( GPO_MODEM_ACK );
  hw_modem_count++;
}

static int hw_check_of( const char *who, const char *what, int ok ) {
  puts( ok ? "PASS " : "FAIL " );
  puts( who );
  puts( what );
  puts( "\n" );
  return ok ? 0 : 1;
}

static int hw_check( const char *what, int ok ) {
  return hw_check_of( "", what, ok );
}

static int hw_test_timer( const struct hw_timer *t ) {
  int fails = 0;
  uint32_t t0, t1, start, deadline, mask = 0;
  unsigned int i;

  hw_cur = t;
  hw_timer_count = 0;
  hw_timer_late = 0;
  hw_timer_latency_max = 0;
  hw_other_count = 0;

  t0 = hw_mtime();
  t1 = hw_mtime();
  fails += hw_check_of( t->name, ": mtime increments", t1 > t0 );

  for ( i = 0; i < NQUIET; i++ ) {
    uint32_t irq = hw_quiet_irqs[ i ];
    install_exception_handler( irq, irq == t->irq ? &hw_timer_isr : &hw_other_isr );
    mask |= 1u << irq;
  }
  hw_timer_next = hw_mtime() + PERIOD;
  hw_timecmp( hw_timer_next );
  enable_interrupts( mask );
  set_global_interrupt_enable( 1 );

  start = hw_mtime();
  deadline = start + ( NPERIODS + 2 ) * PERIOD;
  while ( hw_timer_count < NPERIODS && ( int32_t )( hw_mtime() - deadline ) < 0 )
    ;
  // let a spurious extra interrupt show up, if any
  while ( ( int32_t )( hw_mtime() - deadline ) < 0 )
    ;
  set_global_interrupt_enable( 0 );
  disable_interrupts( mask );

  fails += hw_check_of( t->name, ": 4 periodic interrupts on its own line, no extra",
                     hw_timer_count == NPERIODS );
  fails += hw_check_of( t->name, ": never taken before its compare time", hw_timer_late == 0 );
  fails += hw_check_of( t->name, ": no other line raised", hw_other_count == 0 );
  puts( t->name );
  puts( " max latency (cycles): 0x" );
  puthex( hw_timer_latency_max );
  puts( "\n" );
  return fails;
}

int main( void ) {
  int fails = 0;
  uint32_t start;
  unsigned int i;

  puts( "hw_smoke: PapaBench SoC patches\n" );

  // PWM channel 0, 20-bit counter (checked on the waveform and pwm.log)
  set_pwm( PWM_FROM_ADDR_AND_INDEX( PWM_BASE, 0 ), PWM_PERIOD, PWM_PULSE );

  for ( i = 0; i < NTIMERS; i++ )
    fails += hw_test_timer( &hw_timers[ i ] );

  // gp_i[1] on IRQ 21: modem clock edges; the PPM line (IRQ 17) stays quiet
  hw_cur = &hw_timers[ 0 ];
  hw_other_count = 0;
  install_exception_handler( 21, &hw_modem_isr );
  install_exception_handler( 17, &hw_other_isr );
  enable_interrupts( ( 1u << 21 ) | ( 1u << 17 ) );
  hw_gpo_toggle( GPO_ENV_AUTOPILOT );
  set_global_interrupt_enable( 1 );
  start = hw_mtime();
  while ( hw_mtime() - start < MODEM_WAIT )
    ;
  set_global_interrupt_enable( 0 );
  disable_interrupts( ( 1u << 21 ) | ( 1u << 17 ) );
  hw_gpo_toggle( GPO_ENV_AUTOPILOT );
  fails += hw_check( "gp_i[1] IRQ 21: one interrupt per modem clock edge",
                     hw_modem_count >= MODEM_EDGES && hw_modem_count <= MODEM_EDGES + 1 );
  fails += hw_check( "gp_i[0] IRQ 17 not raised", hw_other_count == 0 );

  // Keep running for two PWM periods so the waveform shows them
  start = hw_mtime();
  while ( hw_mtime() - start < 2 * PWM_PERIOD + 1000 )
    ;

  puts( fails ? "hw_smoke: FAIL\n" : "hw_smoke: PASS\n" );
  sim_halt();
  return 0;
}
