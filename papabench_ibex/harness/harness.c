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

  The AVR peripherals are modelled on real Ibex peripherals (periph.h). The
  program's model lists the Ibex interrupts it owns (papabench_irqs[]); the
  harness installs one wrapper on them and on the tick, which calls the
  dispatcher of the interrupt taken and then the models' synchronisation
  point. The upstream ISRs the model calls are timed one by one
  (papabench_isr_run). For every interrupt, tick included, the wrapper body
  (between its two mcycle reads) is added to harness_isr_cycles and
  subtracted from the task samples. The trap entry/exit cost (register save
  and restore, mret) is not subtracted: a sample hit by an interrupt keeps
  it. It is measured at start-up and printed as trap_overhead, for
  reference only.

  After PAPABENCH_TICKS scheduler ticks (plus the program's startup ticks)
  the ISR prints the results on SimCtrl and halts the simulation:
    PAPABENCH,<prog>,ticks=<n>,startup_ticks=<s>,tick_cycles=<c>
    overhead,<cycles>
    task,<name>,<count>,<min>,<max>,<avg>
    trap_overhead,<cycles>
    isr_overhead,<cycles>
    isr,<name>,<count>,<min>,<max>,<avg>
    END

  With PAPABENCH_MEASURE=0 (make MEASURE=0) nothing is timed: no hooks (the
  PapaBench objects are not instrumented), no calibration, the wrappers only
  dispatch and synchronise the models. Same scenario, same stop after the
  ticks; the output is the header line (with ",measure=0") and END.
*/

#include "reference_system_common.h"
#include "timer.h"
#include "papabench_harness.h"
#include "ibex_io.h"

#ifndef PAPABENCH_TICK_CYCLES
#define PAPABENCH_TICK_CYCLES 819200
#endif

#ifndef PAPABENCH_TICKS
#define PAPABENCH_TICKS 61
#endif

#ifndef PAPABENCH_MEASURE
#define PAPABENCH_MEASURE 1
#endif

#define PAPABENCH_MAX_TASKS 16
#define HARNESS_CALIB PAPABENCH_MAX_TASKS
#define PAPABENCH_MAX_ISRS 8
#define HARNESS_ISR_CALIB PAPABENCH_MAX_ISRS

const unsigned int papabench_tick_cycles = PAPABENCH_TICK_CYCLES;

#define TIMER_IRQ_NUM 7


static volatile uint32_t harness_ticks;
static uint64_t harness_timecmp;

/* Dispatcher of each interrupt (mcause number), for harness_irq */
static papabench_fn_t harness_dispatch[ 32 ];


#if PAPABENCH_MEASURE
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

/* Upstream ISRs 0..nisrs-1, then an empty function at HARNESS_ISR_CALIB */
static struct harness_stat harness_isr_stat[ PAPABENCH_MAX_ISRS + 1 ];

/* Task whose first function has been entered and whose last function has not
   exited yet. Tasks never nest (no task calls another, the ISR is not
   instrumented), so one pointer is enough and the exit hook needs no search
   inside the timed window. */
static struct harness_stat *harness_open;

/* Cycles spent in the timer ISR body, subtracted from task samples */
static volatile uint32_t harness_isr_cycles;
/* Trap entry/exit cycles of one interrupt, outside the wrapper's reads:
   printed as trap_overhead, not subtracted from the samples */
static uint32_t harness_trap_cycles;
#endif


static inline uint32_t harness_mcycle( void )
{
  uint32_t v;

  asm volatile( "csrr %0, mcycle" : "=r"( v ) : : "memory" );
  return v;
}


static inline uint32_t harness_mcause( void )
{
  uint32_t v;

  asm volatile( "csrr %0, mcause" : "=r"( v ) );
  return v;
}


#if PAPABENCH_MEASURE
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


#endif /* PAPABENCH_MEASURE */


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


#if PAPABENCH_MEASURE
static void harness_field( uint32_t v )
{
  putchar( ',' );
  harness_putdec( v );
}


static void harness_stat_line( const char *kind, const char *name,
                               const struct harness_stat *s )
{
  puts( kind );
  puts( name );
  harness_field( s->count );
  harness_field( s->count ? s->min : 0 );
  harness_field( s->max );
  harness_field( s->count ? ( uint32_t )( s->sum / s->count ) : 0 );
  putchar( '\n' );
}


static void harness_report( void )
{
  struct harness_stat *s;
  unsigned int t;

  s = &harness_stat[ HARNESS_CALIB ];
  puts( "overhead" );
  harness_field( s->min );
  putchar( '\n' );

  for ( t = 0; t < harness_ntasks; t++ )
    harness_stat_line( "task,", papabench_tasks[ t ].name, &harness_stat[ t ] );

  puts( "trap_overhead" );
  harness_field( harness_trap_cycles );
  putchar( '\n' );
  puts( "isr_overhead" );
  harness_field( harness_isr_stat[ HARNESS_ISR_CALIB ].min );
  putchar( '\n' );
  for ( t = 0; t < papabench_nisrs; t++ )
    harness_stat_line( "isr,", papabench_isrs[ t ].name, &harness_isr_stat[ t ] );
  puts( "END\n" );
}
#else
static void harness_report( void )
{
  puts( "END\n" );
}
#endif


/* ---------------------------------------------------------------- timer --- */

/* Body of the tick interrupt (wrapper below) */
static void __attribute__( ( noinline ) ) harness_tick( void )
{
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
}


/* ------------------------------------------------- peripheral interrupts --- */

#if PAPABENCH_MEASURE
static void harness_stat_add( struct harness_stat *s, uint32_t c )
{
  if ( c < s->min )
    s->min = c;
  if ( c > s->max )
    s->max = c;
  s->sum += c;
  s->count++;
}
#endif


#if PAPABENCH_MEASURE
/* Upstream ISRs are never nested (Ibex clears mstatus.MIE on trap entry) */
void papabench_isr_run( unsigned int id, papabench_fn_t fn )
{
  uint32_t t0 = harness_mcycle();

  fn();
  harness_stat_add( &harness_isr_stat[ id ], harness_mcycle() - t0 );
}
#else
void papabench_isr_run( unsigned int id, papabench_fn_t fn )
{
  ( void ) id;
  fn();
}
#endif


#if PAPABENCH_MEASURE
static void harness_isr_calib_fn( void )
{
}
#endif


/*
  Every Ibex interrupt, the scheduler tick included, enters through this one
  wrapper: read mcycle, call the dispatcher of the interrupt taken (mcause),
  synchronise the models, read mcycle. Only the body between the two reads
  is added to harness_isr_cycles. The cycles outside them (trap entry,
  register save, register restore, mret) stay in the task sample they hit:
  subtracting a calibrated constant (harness_trap_cycles) instead gave
  samples below the task's real cost, even negative, because the real trap
  cost depends on where the interrupt lands.
*/
static void __attribute__( ( interrupt ) ) harness_irq( void )
{
#if PAPABENCH_MEASURE
  uint32_t t0 = harness_mcycle();
#endif

  harness_dispatch[ harness_mcause() & 0x1F ]();
  papabench_periph_poll();
#if PAPABENCH_MEASURE
  harness_isr_cycles += harness_mcycle() - t0;
#endif
}


static void harness_install( unsigned int irq, papabench_fn_t dispatch )
{
  harness_dispatch[ irq ] = dispatch;
  install_exception_handler( irq, harness_irq );
}


#if PAPABENCH_MEASURE
/* Start-up measurement of harness_trap_cycles: a machine-timer interrupt
   hits a loop that reads mcycle back to back. The iteration it lands in
   takes the loop's normal time + trap + wrapper body; the body is known
   (harness_isr_cycles), so trap = gap - normal - body. Minimum over a few
   trials. Reported only (trap_overhead): it tells how much an interrupt
   adds to the sample it hits. Runs before the tick and the models are set
   up; the dispatcher disarms the timer. */
static void __attribute__( ( noinline ) ) harness_calib_dispatch( void )
{
  IBEX_REG( IBEX_TIMER + IBEX_MTIMECMP ) = 0xFFFFFFFFu;
  IBEX_REG( IBEX_TIMER + IBEX_MTIMECMPH ) = 0xFFFFFFFFu;
}


static void harness_trap_calib( void )
{
  uint32_t best = 0xFFFFFFFF;
  unsigned int trial;

  harness_trap_cycles = 0;
  harness_install( TIMER_IRQ_NUM, harness_calib_dispatch );
  enable_interrupts( TIMER_IRQ );

  for ( trial = 0; trial < 8; trial++ ) {
    uint32_t isr0, body, prev, cur, d, dmin = 0xFFFFFFFF, dmax = 0;
    uint32_t now = IBEX_REG( IBEX_TIMER + IBEX_MTIME );
    int extra = 2;

    IBEX_REG( IBEX_TIMER + IBEX_MTIMECMP ) = 0xFFFFFFFFu;
    IBEX_REG( IBEX_TIMER + IBEX_MTIMECMPH ) =
      IBEX_REG( IBEX_TIMER + IBEX_MTIMEH );
    IBEX_REG( IBEX_TIMER + IBEX_MTIMECMP ) = now + 300;
    isr0 = harness_isr_cycles;
    set_global_interrupt_enable( 1 );
    prev = harness_mcycle();
    do {
      cur = harness_mcycle();
      d = cur - prev;
      prev = cur;
      if ( d < dmin )
        dmin = d;
      if ( d > dmax )
        dmax = d;
    } while ( harness_isr_cycles == isr0 || extra-- > 0 );
    set_global_interrupt_enable( 0 );
    body = harness_isr_cycles - isr0;
    if ( dmax - dmin - body < best )
      best = dmax - dmin - body;
  }

  disable_interrupts( TIMER_IRQ );
  harness_isr_cycles = 0;
  harness_trap_cycles = best;
}
#endif


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
#if !PAPABENCH_MEASURE
  puts( ",measure=0" );
#endif
  putchar( '\n' );

  if ( papabench_ntasks > PAPABENCH_MAX_TASKS ||
       papabench_nisrs > PAPABENCH_MAX_ISRS ) {
    puts( "ERROR,too many tasks or ISRs\n" );
    return 1;
  }
  for ( t = 0; t < papabench_nirqs; t++ ) {
    unsigned int irq = papabench_irqs[ t ].irq;

    if ( irq >= 32 || irq == TIMER_IRQ_NUM || harness_dispatch[ irq ] ) {
      puts( "ERROR,bad or duplicate interrupt in papabench_irqs\n" );
      return 1;
    }
    harness_dispatch[ irq ] = papabench_irqs[ t ].dispatch;
  }
  if ( papabench_check() ) {
    puts( "ERROR,PAPABENCH_TIFR_ADDR/PAPABENCH_TICK_BIT do not match the device\n" );
    return 1;
  }

#if PAPABENCH_MEASURE
  harness_ntasks = papabench_ntasks;
  for ( t = 0; t <= PAPABENCH_MAX_TASKS; t++ )
    harness_stat[ t ].min = 0xFFFFFFFF;
  for ( t = 0; t < harness_ntasks; t++ ) {
    harness_stat[ t ].first = papabench_tasks[ t ].first;
    harness_stat[ t ].last = papabench_tasks[ t ].last;
  }
  harness_stat[ HARNESS_CALIB ].first = papabench_calib;
  harness_stat[ HARNESS_CALIB ].last = papabench_calib;

  for ( t = 0; t <= PAPABENCH_MAX_ISRS; t++ )
    harness_isr_stat[ t ].min = 0xFFFFFFFF;

  /* Hook overhead: minimum over a few samples, before the timer runs */
  for ( t = 0; t < 8; t++ )
    papabench_calib();
  /* ISR timing overhead, same way */
  for ( t = 0; t < 8; t++ )
    papabench_isr_run( HARNESS_ISR_CALIB, harness_isr_calib_fn );

  /* Trap entry/exit cost of the interrupt wrappers */
  harness_trap_calib();
#endif

  /* Peripheral models: real peripherals set up, the model's interrupts
     installed. Timers and the UART only interrupt once the model arms them;
     GPIO lines are enabled by the model when the upstream code enables its
     AVR interrupt. */
  papabench_periph_init();
  for ( t = 0; t < papabench_nirqs; t++ ) {
    harness_install( papabench_irqs[ t ].irq, papabench_irqs[ t ].dispatch );
    if ( papabench_irqs[ t ].enable )
      enable_interrupts( 1u << papabench_irqs[ t ].irq );
  }

  harness_install( TIMER_IRQ_NUM, harness_tick );
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
