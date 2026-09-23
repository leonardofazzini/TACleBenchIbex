/*
  AVR peripheral models on real Ibex peripherals (PapaBench side).

  The upstream code talks to AVR registers, which live in papabench_sfr[]
  (include/arch/sfr_defs.h). A model reads what the code wrote there and acts
  on the real SoC: an AVR interrupt source becomes an Ibex interrupt (a timer
  compare, the UART RX FIFO, the GPIO line), and the upstream SIGNAL() handler
  (__vector_N) is called from the Ibex interrupt, through papabench_isr_run()
  so that it is timed.

  Models look at the registers at two kinds of synchronisation points, both
  deterministic:
    - after every Ibex interrupt handler (papabench_periph_poll() at the end
      of each harness wrapper);
    - in the idle loop: timer_periodic() of both programs calls
      papabench_tick_take() on every main-loop iteration, which polls too.
  Counters that the upstream code reads (TCNT1, TCNT2, ICR1) are only read
  inside ISRs, so they are refreshed on ISR entry.

  Event channels: each of TimerA/B/C carries a few one-shot channels; the
  timer compare is set to the earliest armed channel and its interrupt runs
  every channel that is due, in channel order. The machine timer stays
  dedicated to the scheduler tick (harness.c).

  Time: mtime counts Ibex cycles (50 MHz). AVR time (16 MHz, the CLOCK of
  link_autopilot.h) is derived from it: avr = ibex * 8 / 25.
*/

#ifndef PERIPH_H
#define PERIPH_H

#include "ibex_io.h"

typedef unsigned long long pb_time_t;

#define PB_MAX_CH 4

struct pb_timer {
  unsigned int base;
  pb_time_t when[ PB_MAX_CH ];
  unsigned char armed[ PB_MAX_CH ];
  void ( *fn[ PB_MAX_CH ] )( void );
};

extern struct pb_timer pb_timer_a;
extern struct pb_timer pb_timer_b;
extern struct pb_timer pb_timer_c;

pb_time_t pb_now( void );
void pb_arm( struct pb_timer *t, unsigned int ch, pb_time_t when );
void pb_disarm( struct pb_timer *t, unsigned int ch );
void pb_timer_irq( struct pb_timer *t );

/* AVR clock at 16 MHz <-> Ibex cycles at 50 MHz (first Ibex cycle at or
   after the AVR clock) */
pb_time_t pb_avr_of_ibex( pb_time_t c );
#define PB_IBEX_OF_AVR( a ) ( ( ( pb_time_t )( a ) * 25 + 7 ) / 8 )

/* AVR time now, remembered as the reference of the counters (TCNT1,
   TCNT2, ICR1) that the ISR about to run will read. pb_oc_sync() measures
   an OCR1A written by that ISR from this reference; the program's poll
   calls pb_avr_snapshot_done() when it is over. */
pb_time_t pb_avr_snapshot( void );
void pb_avr_snapshot_done( void );

/* Timer1 output compare A: match when TCNT1 reaches OCR1A. Re-armed when
   the enable is set and OCR1A differs from the armed value, or after the
   match fired (next match one full 16-bit wrap later). */
struct pb_oc {
  struct pb_timer *t;
  unsigned int ch;
  unsigned int armed_ocr;
};
void pb_oc_sync( struct pb_oc *oc, int enabled, unsigned int ocr );

/* Counter values from AVR time: TCNT1 (Clk/1), TCNT2 (Clk/1024) */
#define PB_TCNT1( avr ) ( ( unsigned int )( avr ) & 0xFFFF )
#define PB_TCNT2( avr ) ( ( unsigned int )( ( avr ) >> 10 ) & 0xFF )

/* gp_o: shadow + bit update. Bits (shared with hw/rtl/papabench_env.sv):
     0    IRQ 17 acknowledge: every toggle clears the environment's latch
     2:1  environment mode (PB_ENV_*)
     3    Autopilot modem TX data (MODEM_TX_DATA)
     4    FBW 4017 servo clock (toggles on every servo compare)
     5    FBW 4017 reset (toggles on every servo frame restart) */
#define PB_GPO_ACK       0x01u
#define PB_GPO_MODE_SHIFT 1
#define PB_GPO_MODE_MASK 0x06u
#define PB_GPO_MODEM_TX  0x08u
#define PB_GPO_4017_CLK  0x10u
#define PB_GPO_4017_RST  0x20u
#define PB_ENV_OFF       0
#define PB_ENV_FBW       1
#define PB_ENV_AUTOPILOT 2
void pb_gpo_write( unsigned int mask, unsigned int value );
void pb_gpo_toggle( unsigned int mask );
/* IRQ 17: acknowledge the environment's edge latch */
void pb_gpio_ack( void );

/* SPDR: AVR's SPI data register is two registers at one address (write =
   transmit, read = last byte received). sfr_defs.h routes every SPDR access
   to papabench_spdr_access(), which returns a fresh slot holding
   papabench_spdr_rx: a write lands in its own slot and a later read sees
   the received byte. papabench_spdr_accesses counts accesses: a master
   transfer starts after the software has touched SPDR. */
extern volatile unsigned char papabench_spdr_rx;
extern volatile unsigned int papabench_spdr_accesses;

#endif /* PERIPH_H */
