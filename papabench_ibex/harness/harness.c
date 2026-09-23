/*
  Measurement harness (Ibex side). Includes only Secure-Ibex headers; talks to
  PapaBench through papabench_harness.h.

  The upstream scheduler of the program (FBW main() or Autopilot mainloop.c
  main(), renamed papabench_upstream_main) runs unchanged. Its time base, the
  AVR Timer2 overflow polled by timer_periodic(), is driven by the Ibex
  machine timer (TIMER_BASE, IRQ 7): every PAPABENCH_TICK_CYCLES cycles the
  ISR below sets papabench_tick_pending, which timer_periodic() consumes (see
  include/arch/sfr_defs.h).

  Tasks are timed with mcycle through -finstrument-functions hooks. Only the
  first/last function of each task in papabench_tasks[] is instrumented (the
  Makefile excludes every other PapaBench function), so the hooks below only
  ever see task boundaries. Cycles spent in the timer ISR while a task runs
  are subtracted. Counts are raw: they include the hook overhead, reported
  separately on the "overhead" line (timing of an instrumented empty
  function); a task timed from first to last function of a sequence also
  contains the calls between them.

  After PAPABENCH_TICKS scheduler ticks (plus the program's startup ticks)
  the ISR prints the results on SimCtrl and halts the simulation:
    PAPABENCH,<prog>,ticks=<n>,startup_ticks=<s>,tick_cycles=<c>
    overhead,<cycles>
    task,<name>,<count>,<min>,<max>,<avg>
    END
*/

#include "reference_system_common.h"
#include "timer.h"
#include "papabench_harness.h"

#ifndef PAPABENCH_TICK_CYCLES
#define PAPABENCH_TICK_CYCLES 819200
#endif

#ifndef PAPABENCH_TICKS
#define PAPABENCH_TICKS 61
#endif

#define PAPABENCH_MAX_TASKS 16
#define HARNESS_CALIB PAPABENCH_MAX_TASKS

#define TIMER_IRQ_NUM 7


struct harness_stat {
  papabench_fn_t first;
  papabench_fn_t last;
  uint32_t start;
  uint32_t isr_start;
  uint32_t count;
  uint32_t min;
  uint32_t max;
  uint64_t sum;
};

/* Tasks 0..ntasks-1, then the calibration function at HARNESS_CALIB */
static struct harness_stat harness_stat[ PAPABENCH_MAX_TASKS + 1 ];
static unsigned int harness_ntasks;

/* Task whose first function has been entered and whose last function has not
   exited yet. Tasks never nest (no task calls another, the ISR is not
   instrumented), so one pointer is enough and the exit hook needs no search
   inside the timed window. */
static struct harness_stat *harness_open;

/* Cycles spent in the timer ISR body, subtracted from task samples */
static volatile uint32_t harness_isr_cycles;
static volatile uint32_t harness_ticks;
static uint64_t harness_timecmp;


static inline uint32_t harness_mcycle( void )
{
  uint32_t v;

  asm volatile( "csrr %0, mcycle" : "=r"( v ) : : "memory" );
  return v;
}


/* ------------------------------------------------ instrumentation hooks --- */

/*
  Reads mcycle together with the ISR cycle total. If the timer ISR fires
  between the two reads, the pair is inconsistent (ISR cycles counted on one
  side of the window only, giving negative or inflated samples): retry.
*/
static inline uint32_t harness_now( uint32_t *isr )
{
  uint32_t i, m;

  do {
    i = harness_isr_cycles;
    m = harness_mcycle();
  } while ( i != harness_isr_cycles );
  *isr = i;
  return m;
}


void __cyg_profile_func_enter( void *fn, void *site )
{
  struct harness_stat *s = harness_stat;
  unsigned int t;

  ( void ) site;
  for ( t = 0; t <= PAPABENCH_MAX_TASKS; t++, s++ ) {
    if ( ( void * ) s->first == fn ) {
      harness_open = s;
      s->start = harness_now( &s->isr_start );
      return;
    }
  }
}


void __cyg_profile_func_exit( void *fn, void *site )
{
  uint32_t isr;
  uint32_t now = harness_now( &isr );
  struct harness_stat *s = harness_open;
  uint32_t c;

  ( void ) site;
  if ( !s || ( void * ) s->last != fn )
    return;
  harness_open = 0;
  c = now - s->start - ( isr - s->isr_start );
  if ( c < s->min )
    s->min = c;
  if ( c > s->max )
    s->max = c;
  s->sum += c;
  s->count++;
}


/* --------------------------------------------------------------- output --- */

static void harness_putdec( uint32_t v )
{
  char buf[ 10 ];
  int i = 0;

  do {
    buf[ i++ ] = '0' + ( v % 10 );
    v /= 10;
  } while ( v );
  while ( i )
    putchar( buf[ --i ] );
}


static void harness_field( uint32_t v )
{
  putchar( ',' );
  harness_putdec( v );
}


static void harness_report( void )
{
  struct harness_stat *s;
  unsigned int t;

  s = &harness_stat[ HARNESS_CALIB ];
  puts( "overhead" );
  harness_field( s->min );
  putchar( '\n' );

  for ( t = 0; t < harness_ntasks; t++ ) {
    s = &harness_stat[ t ];
    puts( "task," );
    puts( papabench_tasks[ t ].name );
    harness_field( s->count );
    harness_field( s->count ? s->min : 0 );
    harness_field( s->max );
    harness_field( s->count ? ( uint32_t )( s->sum / s->count ) : 0 );
    putchar( '\n' );
  }
  puts( "END\n" );
}


/* ---------------------------------------------------------------- timer --- */

static void __attribute__( ( interrupt ) ) harness_timer_isr( void )
{
  uint32_t t0 = harness_mcycle();

  /* Next compare from the previous one, not from mtime: no drift */
  harness_timecmp += PAPABENCH_TICK_CYCLES;
  timecmp_update( harness_timecmp );
  papabench_tick_pending = 1;

  if ( ++harness_ticks >= PAPABENCH_TICKS + papabench_startup_ticks ) {
    harness_report();
    sim_halt();
    while ( 1 )
      ;
  }
  harness_isr_cycles += harness_mcycle() - t0;
}


/* ----------------------------------------------------------------- main --- */

int main( void )
{
  unsigned int t;

  pcount_enable( 0 );
  pcount_reset();
  pcount_enable( 1 );

  puts( "PAPABENCH," );
  puts( papabench_prog_name );
  puts( ",ticks=" );
  harness_putdec( PAPABENCH_TICKS );
  puts( ",startup_ticks=" );
  harness_putdec( papabench_startup_ticks );
  puts( ",tick_cycles=" );
  harness_putdec( PAPABENCH_TICK_CYCLES );
  putchar( '\n' );

  if ( papabench_ntasks > PAPABENCH_MAX_TASKS ) {
    puts( "ERROR,too many tasks\n" );
    return 1;
  }
  if ( papabench_check() ) {
    puts( "ERROR,PAPABENCH_TIFR_ADDR/PAPABENCH_TICK_BIT do not match the device\n" );
    return 1;
  }

  harness_ntasks = papabench_ntasks;
  for ( t = 0; t <= PAPABENCH_MAX_TASKS; t++ )
    harness_stat[ t ].min = 0xFFFFFFFF;
  for ( t = 0; t < harness_ntasks; t++ ) {
    harness_stat[ t ].first = papabench_tasks[ t ].first;
    harness_stat[ t ].last = papabench_tasks[ t ].last;
  }
  harness_stat[ HARNESS_CALIB ].first = papabench_calib;
  harness_stat[ HARNESS_CALIB ].last = papabench_calib;

  /* Hook overhead: minimum over a few samples, before the timer runs */
  for ( t = 0; t < 8; t++ )
    papabench_calib();

  install_exception_handler( TIMER_IRQ_NUM, harness_timer_isr );
  harness_timecmp = timer_read() + PAPABENCH_TICK_CYCLES;
  timecmp_update( harness_timecmp );
  enable_interrupts( TIMER_IRQ );
  set_global_interrupt_enable( 1 );

  papabench_upstream_main();

  /* Only reached if the upstream loop ever returns */
  set_global_interrupt_enable( 0 );
  puts( "ERROR,upstream main returned\n" );
  harness_report();
  return 1;
}
