/*
  Generic part of the AVR peripheral models (see periph.h): event channels on
  TimerA/B/C, Timer1 compare arithmetic, timer counters, GPIO output shadow,
  the SPDR double register. Compiled with the PapaBench flags of the program
  being built, but uses no AVR register name, so it is the same for both.
*/

#include "papabench_harness.h"
#include "periph.h"

struct pb_timer pb_timer_a = { IBEX_TIMER_A };
struct pb_timer pb_timer_b = { IBEX_TIMER_B };
struct pb_timer pb_timer_c = { IBEX_TIMER_C };


/* -------------------------------------------------------------- timers --- */

pb_time_t pb_now( void )
{
  unsigned int hi, lo;

  /* All timers count the same clock from the same reset: TimerA's mtime
     is the time base of every model */
  do {
    hi = IBEX_REG( IBEX_TIMER_A + IBEX_MTIMEH );
    lo = IBEX_REG( IBEX_TIMER_A + IBEX_MTIME );
  } while ( hi != IBEX_REG( IBEX_TIMER_A + IBEX_MTIMEH ) );
  return ( ( pb_time_t ) hi << 32 ) | lo;
}


/* Compare = earliest armed channel, or all ones (never) if none. Writing
   mtimecmp also clears the sticky interrupt. */
static void pb_program( struct pb_timer *t )
{
  pb_time_t next = ~( pb_time_t ) 0;
  unsigned int ch;

  for ( ch = 0; ch < PB_MAX_CH; ch++ )
    if ( t->armed[ ch ] && t->when[ ch ] < next )
      next = t->when[ ch ];
  IBEX_REG( t->base + IBEX_MTIMECMP ) = 0xFFFFFFFFu;
  IBEX_REG( t->base + IBEX_MTIMECMPH ) = ( unsigned int )( next >> 32 );
  IBEX_REG( t->base + IBEX_MTIMECMP ) = ( unsigned int ) next;
}


void pb_arm( struct pb_timer *t, unsigned int ch, pb_time_t when )
{
  t->when[ ch ] = when;
  t->armed[ ch ] = 1;
  pb_program( t );
}


void pb_disarm( struct pb_timer *t, unsigned int ch )
{
  if ( !t->armed[ ch ] )
    return;
  t->armed[ ch ] = 0;
  pb_program( t );
}


void pb_timer_irq( struct pb_timer *t )
{
  pb_time_t now = pb_now();
  unsigned int ch;

  for ( ch = 0; ch < PB_MAX_CH; ch++ ) {
    if ( t->armed[ ch ] && t->when[ ch ] <= now ) {
      t->armed[ ch ] = 0;
      t->fn[ ch ]();
    }
  }
  pb_program( t );
}


void papabench_irq_timer_a( void )
{
  pb_timer_irq( &pb_timer_a );
}


void papabench_irq_timer_b( void )
{
  pb_timer_irq( &pb_timer_b );
}


void papabench_irq_timer_c( void )
{
  pb_timer_irq( &pb_timer_c );
}


/* ------------------------------------------------------------- Timer1 --- */

/* floor( c * 8 / 25 ) with 32-bit hardware division while mtime fits 32
   bits (86 s of simulated time), libgcc's 64-bit one after */
pb_time_t pb_avr_of_ibex( pb_time_t c )
{
  unsigned int lo = ( unsigned int ) c;

  if ( !( c >> 32 ) )
    return ( pb_time_t )( ( lo / 25 ) * 8 + ( lo % 25 ) * 8 / 25 );
  return c * 8 / 25;
}


static pb_time_t pb_avr_ref;
static unsigned char pb_avr_ref_valid;


pb_time_t pb_avr_snapshot( void )
{
  pb_avr_ref = pb_avr_of_ibex( pb_now() );
  pb_avr_ref_valid = 1;
  return pb_avr_ref;
}


void pb_avr_snapshot_done( void )
{
  pb_avr_ref_valid = 0;
}


void pb_oc_sync( struct pb_oc *oc, int enabled, unsigned int ocr )
{
  pb_time_t base;
  unsigned int delta;

  if ( !enabled ) {
    pb_disarm( oc->t, oc->ch );
    return;
  }
  ocr &= 0xFFFF;
  if ( oc->t->armed[ oc->ch ] && ocr == oc->armed_ocr )
    return;
  /* First AVR clock after 'base' at which TCNT1 == OCR1A (a full wrap if
     equal). 'base' is the TCNT1 the ISR just read, if any: the model's own
     cycles between that snapshot and here are not AVR time (an ISR that
     sets OCR1A = TCNT1 + 200 must see its match 200 AVR clocks after its
     read). A target already in the past fires at once. */
  base = pb_avr_ref_valid ? pb_avr_ref : pb_avr_of_ibex( pb_now() );
  delta = ( ocr - ( unsigned int )( base & 0xFFFF ) ) & 0xFFFF;
  if ( !delta )
    delta = 0x10000;
  oc->armed_ocr = ocr;
  pb_arm( oc->t, oc->ch, PB_IBEX_OF_AVR( base + delta ) );
}


/* --------------------------------------------------------------- GPIO --- */

static unsigned int pb_gpo;


void pb_gpo_write( unsigned int mask, unsigned int value )
{
  pb_gpo = ( pb_gpo & ~mask ) | ( value & mask );
  IBEX_REG( IBEX_GPIO + IBEX_GPIO_OUT ) = pb_gpo;
}


void pb_gpo_toggle( unsigned int mask )
{
  pb_gpo ^= mask;
  IBEX_REG( IBEX_GPIO + IBEX_GPIO_OUT ) = pb_gpo;
}


void pb_gpio_ack( void )
{
  pb_gpo_toggle( PB_GPO_ACK );
}


/* --------------------------------------------------------------- SPDR --- */

volatile unsigned char papabench_spdr_rx;
volatile unsigned int papabench_spdr_accesses;

static volatile unsigned char pb_spdr_slot[ 4 ];
static unsigned int pb_spdr_next;


volatile unsigned char *papabench_spdr_access( void )
{
  volatile unsigned char *p = &pb_spdr_slot[ pb_spdr_next++ & 3 ];

  *p = papabench_spdr_rx;
  papabench_spdr_accesses++;
  return p;
}
