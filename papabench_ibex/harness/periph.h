/*
  AVR peripheral models on real Ibex peripherals (PapaBench side).

  The upstream code talks to AVR registers, which live in papabench_sfr[]
  (include/arch/sfr_defs.h). A model reads what the code wrote there and acts
  on the real SoC: an AVR interrupt source becomes an Ibex interrupt (a timer
  compare, the UART RX FIFO, the SPI RX FIFO, a GPIO edge), and the
  upstream SIGNAL() handler (__vector_N) is called from the Ibex interrupt,
  through papabench_isr_run() so that it is timed.

  Models look at the registers at two kinds of synchronisation points, both
  deterministic:
    - after every Ibex interrupt handler (papabench_periph_poll() at the end
      of each harness wrapper);
    - in the idle loop: timer_periodic() of both programs calls
      papabench_tick_take() on every main-loop iteration, which polls too.
  Counters that the upstream code reads (TCNT1, TCNT2, ICR1) are only read
  inside ISRs, so they are refreshed on ISR entry.

  One peripheral, one function: every Ibex peripheral serves one function
  across both programs (map in ibex_io.h). Each program's model lists the
  interrupts it owns in papabench_irqs[] and touches no other peripheral.

  Event channels: each of TimerA, B, D, E carries a few one-shot channels; the
  timer compare is set to the earliest armed channel and its interrupt runs
  every channel that is due, in channel order. The machine timer stays
  dedicated to the scheduler tick (harness.c); the models only read it.

  Time: mtime counts Ibex cycles (50 MHz); the machine timer's is the time
  base of every model (all timers count the same clock from the same
  reset). AVR time (16 MHz, the CLOCK of link_autopilot.h) is derived from
  it: avr = ibex * 8 / 25.
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
extern struct pb_timer pb_timer_d;
extern struct pb_timer pb_timer_e;

/* Dispatchers for papabench_irqs[]: run the due channels of that timer */
void papabench_irq_timer_a( void );
void papabench_irq_timer_b( void );
void papabench_irq_timer_d( void );
void papabench_irq_timer_e( void );

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

/* GPIO: each program owns one bank (hw/patches/0003-soc-gpio-banks.patch),
   FBW bank 0, Autopilot bank 1, and touches no register of the other;
   papabench_gpio (defined by the program's model) is its base. Bits of a
   bank (shared with hw/rtl/papabench_env.sv and papabench_dual.sv):
     gp_o[0]  environment enable: starts the program's stimuli
     gp_o[1]  FBW: 4017 servo clock (toggles on every servo compare);
              Autopilot: modem TX data (MODEM_TX_DATA)
     gp_o[2]  FBW: 4017 reset (toggles on every servo frame restart)
     gp_o[7]  run over (papabench_periph_done()): the joint simulation ends
              when both MCUs have set it
     gp_i[0]  the program's input line (FBW radio PPM, Autopilot modem
              clock): its falling edges set IRQ_STATUS bit 0 and raise the
              bank's interrupt (pb_gpio_init())
   gp_o is written from a shadow (pb_gpo_write(), pb_gpo_toggle()). */
extern const unsigned int papabench_gpio;
#define PB_GPO_ENV      0x01u
#define PB_GPO_4017_CLK 0x02u
#define PB_GPO_4017_RST 0x04u
#define PB_GPO_MODEM_TX 0x02u
#define PB_GPO_DONE     0x80u
#define PB_GPI_LINE     0x01u
void pb_gpo_write( unsigned int mask, unsigned int value );
void pb_gpo_toggle( unsigned int mask );
/* Falling-edge interrupt on PB_GPI_LINE; the model enables the core line */
void pb_gpio_init( void );
/* Acknowledge the edge (write 1 to clear IRQ_STATUS), first thing in the
   interrupt wrapper */
static inline void pb_gpi_ack( void )
{
  IBEX_REG( papabench_gpio + IBEX_GPIO_IRQ_STATUS ) = PB_GPI_LINE;
}

/* SPDR: AVR's SPI data register is two registers at one address (write =
   transmit, read = last byte received); the Ibex SPI has two (TXDATA,
   RXDATA), and a C lvalue cannot tell a read from a write. sfr_defs.h
   routes every SPDR access to papabench_spdr_access(), which returns a
   fresh slot holding papabench_spdr_rx (the byte the model last popped from
   RXDATA): a write lands in its own slot and a read sees the received byte.
   papabench_spdr_accesses counts accesses; the last PB_SPDR_SLOTS stay
   readable.

   The models tell writes from reads by the upstream access pattern, the
   same in both programs (fly_by_wire/spi.c, autopilot/link_fbw.c): outside
   interrupts SPDR is only written (spi_reset(), link_fbw_send()); an SPI
   ISR that touches SPDR reads it last and writes it before (write + read,
   or read alone on the last byte of a frame). pb_spdr_flush() applies this
   rule to the accesses since *seen and pushes the written bytes into an SPI
   TX FIFO.

   The AVR SPDR holds one byte, the FIFO sixteen, and the RTL has no flush:
   a byte written too late (the transfer it was meant for has started, and
   the slave sent 0x00 instead) would stay queued and shift every later
   byte and frame by one. On the AVR such a write is dropped (write
   collision), so the frame fails its checksum and the next one is aligned
   again. pb_spdr_flush() does the same: a write that finds the TX FIFO not
   empty is dropped and counted in pb_spdr_dropped. */
#define PB_SPDR_SLOTS 8
extern volatile unsigned char papabench_spdr_rx;
extern volatile unsigned int papabench_spdr_accesses;
extern unsigned int pb_spdr_dropped;
/* last_is_read: 1 after an SPI ISR, 0 for accesses made outside ISRs.
   spi_base: SPI whose TX FIFO gets the written bytes, 0 = drop them. */
void pb_spdr_flush( unsigned int *seen, int last_is_read,
                    unsigned int spi_base );

#endif /* PERIPH_H */
