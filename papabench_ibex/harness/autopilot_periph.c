/*
  Autopilot (ATmega128) peripheral model on the Ibex SoC (see periph.h).

  AVR source              Ibex resource               upstream ISR
  Timer1 compare A        TimerD channel 0            __vector_12 link_fbw
  SPI master              SPI master, IRQ 20          __vector_17 SPI
  ADC conversion          TimerB channel 0            __vector_21
  UART1 receive (GPS)     UART RX, IRQ 16             __vector_30 GPS
  INT4 (modem clock)      GPIO bank 1 gp_i[0], IRQ 24 __vector_5  modem

  SPI: the Autopilot is the master of the inter-MCU link, on the real SPI
  master. The model follows SPCR (SPE + MSTR = enabled, SPR1:0 = clock:
  fck/16 is 1 MHz, DIV = 24) and PORTB.0 (slave 0 select = CS). The bytes
  link_fbw_send() and the OCR1A ISR write to SPDR are pushed into TXDATA
  (pb_spdr_flush()), which starts the transfer; the byte received raises
  IRQ 20, the model pops it from RXDATA (what the OCR1A ISR later reads
  from SPDR) and runs the SPI ISR with SPIF set.

  Who is the slave depends on the build. Joint run (PAPABENCH_JOINT=1): the
  real FBW on the other MCU (hw/rtl/papabench_dual.sv). Single run: a
  virtual FBW, played by this model on the SoC's SPI slave (looped back
  from the master on chip, IRQ 21), which answers each frame with the bytes
  of ap_spi_frame() (deterministic scenario: radio OK, mode switch
  MANUAL -> AUTO1 -> AUTO2 within the first 4 frames, full throttle), what
  radio_control_task receives, and checks the checksum of the Autopilot's
  frames.

  GPS UBX bytes and the modem clock come from the simulation environment
  (hw/rtl/papabench_env.sv) through the UART and gp_i[0] of GPIO bank 1
  (falling edges raise IRQ 24); the modem data bit (PORTD.6) goes out on
  gp_o[1] of the same bank.
*/

#include <arch/io.h>
#include "link_autopilot.h"
#include "papabench_harness.h"
#include "periph.h"

void __vector_5( void );
void __vector_12( void );
void __vector_17( void );
void __vector_21( void );
void __vector_30( void );

enum { AP_ISR_MODEM, AP_ISR_OC1A, AP_ISR_SPI, AP_ISR_GPS, AP_ISR_ADC };

/* PapaBench_for_wcet.txt: I4 modem, I5 __vector_12, I6 SPI, I7 GPS; then
   the ADC */
const struct papabench_isr papabench_isrs[] = {
  { "modem(__vector_5)" },
  { "link_fbw_oc1a(__vector_12)" },
  { "spi(__vector_17)" },
  { "gps_uart1_rx(__vector_30)" },
  { "adc(__vector_21)" },
};

const unsigned int papabench_nisrs =
  sizeof( papabench_isrs ) / sizeof( papabench_isrs[ 0 ] );


/* ADC: 13 ADC clocks at Clk/128 (VOLTAGE_TIME) */
#define AP_ADC_CYCLES PB_IBEX_OF_AVR( 13 * 128 )
/* modem.h (CTL_BRD_V1_2_1): TX data on PORTD.6 */
#define AP_MODEM_TX_DATA 6
/* airframe.h: IR sensors on ADC 1 and 2 */
#define AP_ADC_IR1 1
#define AP_ADC_IR2 2


static void ap_counters( void )
{
  pb_time_t avr = pb_avr_snapshot();

  TCNT1 = PB_TCNT1( avr );
  TCNT2 = PB_TCNT2( avr );
}


/* ------------------------------------------------------------- Timer1 --- */

static struct pb_oc ap_oc = { &pb_timer_d, 0 };

static void ap_spi_flush( int last_is_read );


/* The ISR sends the next byte (SPDR write, then read of the byte
   received) or, after the last one, reads it and stops the SPI */
static void ap_oc1a_event( void )
{
  ap_counters();
  ap_spi_flush( 0 );
  papabench_isr_run( AP_ISR_OC1A, __vector_12 );
  ap_spi_flush( 1 );
}


/* ---------------------------------------------------------------- SPI --- */

/* autopilot/spi.h: slave 0 (the FBW) selected by PORTB.0 low */
#define AP_SPI_SS0_PIN 0

/* autopilot/link_fbw.h */
extern volatile uint8_t link_fbw_nb_err;

static unsigned int ap_spdr_seen;
static unsigned int ap_spi_ctrl = ~0u, ap_spi_div = ~0u, ap_spi_cs;
static unsigned int ap_spi_frames;


static void ap_spi_irq( void )
{
  while ( !( IBEX_REG( IBEX_SPI_MASTER + IBEX_SPI_STATUS ) &
             IBEX_SPI_RX_EMPTY ) ) {
    papabench_spdr_rx =
      ( unsigned char ) IBEX_REG( IBEX_SPI_MASTER + IBEX_SPI_RXDATA );
    ap_counters();
    SPSR |= _BV( SPIF );
    if ( SPCR & _BV( SPIE ) ) {
      papabench_isr_run( AP_ISR_SPI, __vector_17 );
      /* SPIF is cleared by hardware when the vector executes */
      SPSR &= ~_BV( SPIF );
    }
  }
}


/* Push what the software wrote to SPDR into the master's TX FIFO, which
   starts the transfer; an access made with the SPI off starts nothing */
static void ap_spi_flush( int last_is_read )
{
  unsigned int on = ( SPCR & _BV( SPE ) ) && ( SPCR & _BV( MSTR ) );

  pb_spdr_flush( &ap_spdr_seen, last_is_read, on ? IBEX_SPI_MASTER : 0 );
}


static void ap_spi_sync( void )
{
  static const unsigned int div[ 4 ] = { 4, 16, 64, 128 };
  unsigned int on = ( SPCR & _BV( SPE ) ) && ( SPCR & _BV( MSTR ) );
  /* The select pin drives the slave only once it is an output (DDRB):
     PORTB.0 is 0 from reset until spi_init() */
  unsigned int cs = ( DDRB & _BV( AP_SPI_SS0_PIN ) ) &&
                    !( PORTB & _BV( AP_SPI_SS0_PIN ) );
  unsigned int ctrl = on ?
    IBEX_SPI_EN | ( ( SPCR & _BV( CPOL ) ) ? IBEX_SPI_CPOL : 0 ) |
    ( ( SPCR & _BV( CPHA ) ) ? IBEX_SPI_CPHA : 0 ) : 0;
  /* SCK = 16 MHz / div (AVR) = 50 MHz / ( 2 * ( DIV + 1 ) ), rounded down */
  unsigned int d = ( div[ SPCR & 0x3 ] * 25 + 15 ) / 16 - 1;

  /* Chip select first, then enable and clock, then the bytes */
  if ( cs != ap_spi_cs ) {
    ap_spi_cs = cs;
    IBEX_REG( IBEX_SPI_MASTER + IBEX_SPI_CS ) = cs;
    if ( !cs )
      ap_spi_frames++;
  }
  if ( d != ap_spi_div ) {
    ap_spi_div = d;
    IBEX_REG( IBEX_SPI_MASTER + IBEX_SPI_DIV ) = d;
  }
  if ( ctrl != ap_spi_ctrl ) {
    ap_spi_ctrl = ctrl;
    IBEX_REG( IBEX_SPI_MASTER + IBEX_SPI_CTRL ) = ctrl;
    IBEX_REG( IBEX_SPI_MASTER + IBEX_SPI_IE ) = ctrl ? IBEX_SPI_IE_RX : 0;
  }
  ap_spi_flush( 0 );
}


#if !PAPABENCH_JOINT
/* ------------------------------------------------------ virtual FBW --- */

static unsigned char ap_frame[ FRAME_LENGTH ];
/* Next byte of ap_frame to queue, bytes received in this frame */
static unsigned int ap_vfbw_tx, ap_vfbw_rx;
static unsigned int ap_vfbw_nframe, ap_vfbw_errors;
static unsigned char ap_vfbw_xor;


/* FBW status, frame n: radio OK with averaged channels; mode stick
   MANUAL for frames 0-1, AUTO1 for 2-3, AUTO2 from 4; full throttle
   (above GAZ_THRESHOLD_TAKEOFF, so AUTO2 launches) */
static void ap_spi_frame( unsigned int n )
{
  struct inter_mcu_msg m;
  unsigned char *b = ( unsigned char * ) &m, x = 0;
  unsigned int i;

  for ( i = 0; i < sizeof( m ); i++ )
    b[ i ] = 0;
  m.channels[ RADIO_THROTTLE ] = MAX_PPRZ;
  m.channels[ RADIO_MODE ] = n < 2 ? MIN_PPRZ : ( n < 4 ? 0 : MAX_PPRZ );
  m.ppm_cpt = 40;
  m.status = _BV( STATUS_RADIO_OK ) | _BV( AVERAGED_CHANNELS_SENT );
  m.vsupply = 111;
  for ( i = 0; i < sizeof( m ); i++ ) {
    ap_frame[ i ] = b[ i ];
    x ^= b[ i ];
  }
  ap_frame[ FRAME_LENGTH - 1 ] = x;
}


/* Keep the slave's TX FIFO ahead of the master (with CPHA = 0 a byte must
   be queued before the master starts it) */
static void ap_vfbw_fill( void )
{
  while ( ap_vfbw_tx < FRAME_LENGTH &&
          !( IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_STATUS ) & IBEX_SPI_TX_FULL ) )
    IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_TXDATA ) = ap_frame[ ap_vfbw_tx++ ];
}


static void ap_vfbw_start( void )
{
  ap_spi_frame( ap_vfbw_nframe );
  ap_vfbw_tx = 0;
  ap_vfbw_rx = 0;
  ap_vfbw_xor = 0;
  ap_vfbw_fill();
}


/* IRQ 21: a byte from the Autopilot, or the end of its frame (chip select
   released): check it, queue the next frame */
static void ap_vfbw_irq( void )
{
  while ( !( IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_STATUS ) &
             IBEX_SPI_RX_EMPTY ) ) {
    unsigned char b =
      ( unsigned char ) IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_RXDATA );

    if ( ++ap_vfbw_rx < FRAME_LENGTH )
      ap_vfbw_xor ^= b;
    else if ( ap_vfbw_rx == FRAME_LENGTH && b != ap_vfbw_xor )
      ap_vfbw_errors++;
    ap_vfbw_fill();
  }
  /* A select without transfers (pin glitch) leaves the frame queued: the
     TX FIFO cannot be flushed, a restart would shift every later frame */
  if ( IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_STATUS ) & IBEX_SPI_FRAME_DONE ) {
    IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_STATUS ) = IBEX_SPI_FRAME_DONE;
    if ( ap_vfbw_rx ) {
      if ( ap_vfbw_rx != FRAME_LENGTH )
        ap_vfbw_errors++;
      ap_vfbw_nframe++;
      ap_vfbw_start();
    }
  }
}


static void ap_vfbw_init( void )
{
  IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_CTRL ) = IBEX_SPI_EN;
  IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_IE ) =
    IBEX_SPI_IE_RX | IBEX_SPI_IE_DONE;
  ap_vfbw_start();
}
#endif


/* ---------------------------------------------------------------- ADC --- */

/* Infrared sensors: level attitude is IR1 + IR2 = 915 and IR2 - IR1 = 110
   (IR_ROLL/PITCH_NEUTRAL_DEFAULT, IR_*OfIrs in airframe.h); a roll
   oscillation of +-40 counts with a 2 s triangle is added to both */
static unsigned int ap_adc_sample( unsigned int ch, pb_time_t now )
{
  unsigned int ms = ( unsigned int ) now / ( IBEX_HZ / 1000 ) % 2000;
  int roll = ms < 1000 ? ( int ) ms / 25 - 20 : 60 - ( int ) ms / 25;

  if ( ch == AP_ADC_IR1 )
    return ( unsigned int )( 402 + roll );
  if ( ch == AP_ADC_IR2 )
    return ( unsigned int )( 512 + roll );
  return 0;
}


static void ap_adc_event( void )
{
  ADCW = ap_adc_sample( ADMUX & 0x7, pb_now() );
  ADCSR &= ~_BV( ADSC );
  if ( ADCSR & _BV( ADIE ) )
    papabench_isr_run( AP_ISR_ADC, __vector_21 );
}


static void ap_adc_sync( void )
{
  const unsigned int run = _BV( ADEN ) | _BV( ADIE ) | _BV( ADSC );

  if ( ( ADCSR & run ) == run && !pb_timer_b.armed[ 0 ] )
    pb_arm( &pb_timer_b, 0, pb_now() + AP_ADC_CYCLES );
}


/* ---------------------------------------------------------------- GPS --- */

static void ap_gps_irq( void )
{
  while ( !( IBEX_REG( IBEX_UART + IBEX_UART_STATUS ) & IBEX_UART_RX_EMPTY ) ) {
    UDR1 = ( unsigned char ) IBEX_REG( IBEX_UART + IBEX_UART_RX );
    if ( UCSR1B & _BV( RXCIE ) )
      papabench_isr_run( AP_ISR_GPS, __vector_30 );
  }
}


/* -------------------------------------------------------------- modem --- */

static unsigned char ap_modem_on;


static void ap_modem_irq( void )
{
  pb_gpi_ack();
  if ( EIMSK & _BV( INT4 ) ) {
    papabench_isr_run( AP_ISR_MODEM, __vector_5 );
    pb_gpo_write( PB_GPO_MODEM_TX,
                  ( PORTD & _BV( AP_MODEM_TX_DATA ) ) ? PB_GPO_MODEM_TX : 0 );
  }
}


/* INT4 enabled by MODEM_CHECK_RUNNING(), disabled by the ISR when the
   buffer is empty */
static void ap_modem_sync( void )
{
  unsigned char on = ( EIMSK & _BV( INT4 ) ) != 0;

  if ( on == ap_modem_on )
    return;
  ap_modem_on = on;
  if ( on )
    ibex_irq_enable( IBEX_IRQ_GPIO1 );
  else
    ibex_irq_disable( IBEX_IRQ_GPIO1 );
}


/* --------------------------------------------------------------- init --- */

const unsigned int papabench_gpio = IBEX_GPIO1;

const struct papabench_irq papabench_irqs[] = {
  { IBEX_IRQ_TIMER_B, papabench_irq_timer_b, 1 },
  { IBEX_IRQ_TIMER_D, papabench_irq_timer_d, 1 },
  { IBEX_IRQ_SPI_M, ap_spi_irq, 1 },
  { IBEX_IRQ_UART, ap_gps_irq, 1 },
  { IBEX_IRQ_GPIO1, ap_modem_irq, 0 },
#if !PAPABENCH_JOINT
  /* virtual FBW */
  { IBEX_IRQ_SPI_S, ap_vfbw_irq, 1 },
#endif
};

const unsigned int papabench_nirqs =
  sizeof( papabench_irqs ) / sizeof( papabench_irqs[ 0 ] );


void papabench_periph_init( void )
{
  pb_timer_d.fn[ 0 ] = ap_oc1a_event;
  pb_timer_b.fn[ 0 ] = ap_adc_event;
  ap_spdr_seen = papabench_spdr_accesses;
#if !PAPABENCH_JOINT
  ap_vfbw_init();
#endif

  pb_gpio_init();
  pb_gpo_write( PB_GPO_ENV | PB_GPO_MODEM_TX, PB_GPO_ENV | PB_GPO_MODEM_TX );
}


void papabench_periph_poll( void )
{
  pb_oc_sync( &ap_oc, TIMSK & _BV( OCIE1A ), OCR1A );
  ap_spi_sync();
  ap_adc_sync();
  ap_modem_sync();
  pb_avr_snapshot_done();
}


/* frames: frames selected by the master (chip-select releases); frames_err:
   upstream count of FBW frames with a bad checksum (link_fbw_nb_err);
   spdr_dropped: SPDR writes dropped, previous byte still queued (periph.h);
   virtual_*: Autopilot frames checked by the virtual FBW (single run) */
void papabench_periph_report( void )
{
  papabench_report_value( "frames", ap_spi_frames );
  papabench_report_value( "frames_err", link_fbw_nb_err );
  papabench_report_value( "spdr_dropped", pb_spdr_dropped );
#if !PAPABENCH_JOINT
  papabench_report_value( "virtual_frames", ap_vfbw_nframe );
  papabench_report_value( "virtual_err", ap_vfbw_errors );
#endif
}
