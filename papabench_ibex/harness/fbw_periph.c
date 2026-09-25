/*
  Fly-By-Wire (ATmega8) peripheral model on the Ibex SoC (see periph.h).

  AVR source              Ibex resource               upstream ISR
  Timer1 compare A        TimerA channel 0            __vector_6  servo
  SPI slave (autopilot)   SPI slave, IRQ 21           __vector_10 SPI
  UART transmit complete  TimerE channel 0            __vector_13
  ADC conversion          TimerB channel 0            __vector_14
  Timer1 input capture    GPIO bank 0 gp_i[0], IRQ 17 __vector_5  radio PPM

  Servos: every servo compare also drives the real PWM. The 4017 decade
  counter turns the interval after compare k into the pulse of servo k, so
  the width just added to OCR1A becomes the pulse of PWM channel k (20 ms
  period); gp_o of GPIO bank 0 carries the 4017 clock and reset for the
  waveform.

  SPI: the FBW is the slave of the inter-MCU link, on the real SPI slave.
  Every byte received raises IRQ 21: the model pops it from RXDATA (the
  byte the ISR reads from SPDR) and runs the upstream ISR; the bytes the
  code writes to SPDR (the ISR, spi_reset()) are pushed into TXDATA (see
  pb_spdr_flush()). PINB.SS (SpiIsSelected()) follows the slave's chip
  select, and the SPI enable/mode follow SPCR.

  Who is the master depends on the build. Joint run (PAPABENCH_JOINT=1):
  the real Autopilot on the other MCU (hw/rtl/papabench_dual.sv). Single
  run: a virtual Autopilot, played by this model on the SoC's SPI master
  (looped back to the slave on chip) and on TimerD (the Autopilot's
  link_fbw pacing timer): a frame of FRAME_LENGTH bytes every 3 scheduler
  ticks (the Autopilot's link_fbw_send() rate), one byte per SPI byte time
  (1 MHz, fck/16 as link_fbw_send()) + the Autopilot's OCR1A delay of 200
  AVR clocks. Its commands come from fbw_spi_frame() (deterministic
  scenario); it checks the checksum of the frames the FBW sends back.

  UART: the servos are only on the PWM. The upstream code still writes its
  boot string and the servo_transmit() frames to the AVR UART, so the UART
  is virtual: bytes are dropped, but the transmitter takes one byte time
  (38400 baud) per byte on TimerE and the transmit-complete ISR runs from
  its interrupt, exactly as with a real UART.

  The radio PPM train comes from the simulation environment
  (hw/rtl/papabench_env.sv) on gp_i[0] of GPIO bank 0, whose falling edges
  raise IRQ 17; the input-capture value is the AVR time when the interrupt
  is taken.
*/

#include <arch/io.h>
#include "link_autopilot.h"
#include "papabench_harness.h"
#include "periph.h"

void __vector_5( void );
void __vector_6( void );
void __vector_10( void );
void __vector_13( void );
void __vector_14( void );

enum { FBW_ISR_PPM, FBW_ISR_SERVO, FBW_ISR_SPI, FBW_ISR_UART_TX, FBW_ISR_ADC };

/* PapaBench_for_wcet.txt: I1 radio, I2 servo, I3 SPI; then the two others */
const struct papabench_isr papabench_isrs[] = {
  { "radio_ppm(__vector_5)" },
  { "servo(__vector_6)" },
  { "spi(__vector_10)" },
  { "uart_tx(__vector_13)" },
  { "adc(__vector_14)" },
};

const unsigned int papabench_nisrs =
  sizeof( papabench_isrs ) / sizeof( papabench_isrs[ 0 ] );


/* servo.c _4017_NB_CHANNELS */
#define FBW_4017_CHANNELS 10
/* 20 ms servo period on the PWM */
#define FBW_SERVO_PERIOD ( IBEX_HZ / 50 )
/* ADC: 13 ADC clocks at Clk/128 (VOLTAGE_TIME) */
#define FBW_ADC_CYCLES PB_IBEX_OF_AVR( 13 * 128 )
/* UART: 10 bits at 38400 baud (UBRRL = 25 at 16 MHz) */
#define FBW_UART_BYTE_CYCLES PB_IBEX_OF_AVR( 10ul * 16000000ul / 38400ul )
/* Virtual master: SPI byte at fck/16 (SCK 1 MHz: DIV = 24), then
   link_fbw's OCR1A = TCNT1 + 200 */
#define FBW_SPI_DIV 24
#define FBW_SPI_GAP_CYCLES PB_IBEX_OF_AVR( 8 * 16 + 200 )
/* fly_by_wire/spi.h: SS on PINB2 */
#define FBW_SPI_SS_PIN 2


/* ------------------------------------------------------------- Timer1 --- */

static struct pb_oc fbw_oc = { &pb_timer_a, 0 };
static unsigned int fbw_servo;


static pb_time_t fbw_counters( void )
{
  pb_time_t avr = pb_avr_snapshot();

  TCNT1 = PB_TCNT1( avr );
  TCNT2 = PB_TCNT2( avr );
  return avr;
}


static void fbw_oc1a_event( void )
{
  unsigned int before = OCR1A, width;

  fbw_counters();
  papabench_isr_run( FBW_ISR_SERVO, __vector_6 );

  /* Mirror of the ISR's own channel index (static in servo.c) */
  width = ( OCR1A - before ) & 0xFFFF;
  if ( fbw_servo >= FBW_4017_CHANNELS ) {
    fbw_servo = 0;
    pb_gpo_toggle( PB_GPO_4017_RST );
  }
  IBEX_REG( IBEX_PWM_WIDTH( fbw_servo ) ) =
    ( unsigned int ) PB_IBEX_OF_AVR( width );
  fbw_servo++;
  pb_gpo_toggle( PB_GPO_4017_CLK );
}


/* ---------------------------------------------------------------- ADC --- */

/* Channel 3: supply, 11.1 V; channel 6: servo supply, 5 V
   (VoltageOfAdc( adc ) = 0.0175 * adc + 0.088, airframe.h) */
static unsigned int fbw_adc_sample( unsigned int ch )
{
  if ( ch == 3 )
    return 629;
  if ( ch == 6 )
    return 281;
  return 0;
}


static void fbw_adc_event( void )
{
  ADCW = fbw_adc_sample( ADMUX & 0x7 );
  ADCSRA &= ~_BV( ADSC );
  if ( ADCSRA & _BV( ADIE ) )
    papabench_isr_run( FBW_ISR_ADC, __vector_14 );
}


static void fbw_adc_sync( void )
{
  const unsigned int run = _BV( ADEN ) | _BV( ADIE ) | _BV( ADSC );

  if ( ( ADCSRA & run ) == run && !pb_timer_b.armed[ 0 ] )
    pb_arm( &pb_timer_b, 0, pb_now() + FBW_ADC_CYCLES );
}


/* ------------------------------------------------------- virtual UART --- */

static unsigned char fbw_tx_busy;


static void fbw_uart_event( void )
{
  fbw_tx_busy = 0;
  if ( UCSRB & _BV( TXCIE ) )
    papabench_isr_run( FBW_ISR_UART_TX, __vector_13 );
}


/* A byte is written to UDR exactly when uart_transmit() or the ISR leaves
   TXCIE set with the transmitter idle; it is dropped after its byte time */
static void fbw_uart_sync( void )
{
  if ( !( UCSRB & _BV( TXCIE ) ) || fbw_tx_busy )
    return;
  fbw_tx_busy = 1;
  pb_arm( &pb_timer_e, 0, pb_now() + FBW_UART_BYTE_CYCLES );
}


/* ---------------------------------------------------------------- SPI --- */

/* fly_by_wire/spi.h */
extern struct inter_mcu_msg to_mega128;

static unsigned int fbw_spdr_seen;
static unsigned int fbw_spi_ctrl = ~0u;
static unsigned int fbw_spi_frames, fbw_spi_underruns;


/* PINB.SS low while the master selects the slave */
static void fbw_spi_ss( void )
{
  if ( IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_STATUS ) & IBEX_SPI_BUSY )
    PINB &= ~_BV( FBW_SPI_SS_PIN );
  else
    PINB |= _BV( FBW_SPI_SS_PIN );
}


static void fbw_spi_irq( void )
{
  while ( !( IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_STATUS ) &
             IBEX_SPI_RX_EMPTY ) ) {
    papabench_spdr_rx =
      ( unsigned char ) IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_RXDATA );
    fbw_spi_ss();
    /* Writes made outside the ISR (spi_reset()) go first */
    pb_spdr_flush( &fbw_spdr_seen, 0, IBEX_SPI_SLAVE );
    if ( ( SPCR & _BV( SPE ) ) && ( SPCR & _BV( SPIE ) ) )
      papabench_isr_run( FBW_ISR_SPI, __vector_10 );
    pb_spdr_flush( &fbw_spdr_seen, 1, IBEX_SPI_SLAVE );
  }
}


static void fbw_spi_sync( void )
{
  /* SPCR: SPE = enable, CPOL, CPHA; the RX interrupt pops every byte */
  unsigned int ctrl = ( SPCR & _BV( SPE ) ) ?
    IBEX_SPI_EN | ( ( SPCR & _BV( CPOL ) ) ? IBEX_SPI_CPOL : 0 ) |
    ( ( SPCR & _BV( CPHA ) ) ? IBEX_SPI_CPHA : 0 ) : 0;

  if ( ctrl != fbw_spi_ctrl ) {
    fbw_spi_ctrl = ctrl;
    IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_CTRL ) = ctrl;
    IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_IE ) = ctrl ? IBEX_SPI_IE_RX : 0;
  }
  /* A frame is over: did the slave run out of bytes during it? */
  if ( IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_STATUS ) & IBEX_SPI_FRAME_DONE ) {
    fbw_spi_frames++;
    if ( IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_STATUS ) & IBEX_SPI_TX_UNDERRUN )
      fbw_spi_underruns++;
    IBEX_REG( IBEX_SPI_SLAVE + IBEX_SPI_STATUS ) =
      IBEX_SPI_FRAME_DONE | IBEX_SPI_TX_UNDERRUN;
  }
  fbw_spi_ss();
  pb_spdr_flush( &fbw_spdr_seen, 0, IBEX_SPI_SLAVE );
}


#if !PAPABENCH_JOINT
/* ------------------------------------------------ virtual Autopilot --- */

static unsigned char fbw_frame[ FRAME_LENGTH ];
/* 0: start a frame; k = 1..FRAME_LENGTH: byte k-1 is being sent */
static unsigned int fbw_vap_state;
static unsigned int fbw_vap_nframe, fbw_vap_errors;
static unsigned char fbw_vap_xor;
static pb_time_t fbw_vap_frame_time;


/* Autopilot commands, frame n: full throttle, roll sweeping +-MAX_PPRZ/4
   over 16 frames, pitch slightly up */
static void fbw_spi_frame( unsigned int n )
{
  struct inter_mcu_msg m;
  unsigned char *b = ( unsigned char * ) &m, x = 0;
  unsigned int i, p = n % 16;
  int tri = p < 8 ? ( int ) p - 4 : 12 - ( int ) p;

  for ( i = 0; i < sizeof( m ); i++ )
    b[ i ] = 0;
  m.channels[ RADIO_THROTTLE ] = MAX_PPRZ;
  m.channels[ RADIO_ROLL ] = tri * ( MAX_PPRZ / 16 );
  m.channels[ RADIO_PITCH ] = MAX_PPRZ / 10;
  m.status = _BV( STATUS_AUTO_OK );
  for ( i = 0; i < sizeof( m ); i++ ) {
    fbw_frame[ i ] = b[ i ];
    x ^= b[ i ];
  }
  fbw_frame[ FRAME_LENGTH - 1 ] = x;
}


/* TimerD channel 0: one byte per event, FBW_SPI_GAP_CYCLES apart (the
   transfer, 17 * ( DIV + 1 ) cycles, is over by the next event) */
static void fbw_vap_event( void )
{
  pb_time_t now = pb_now();
  unsigned char rx;

  if ( fbw_vap_state == 0 ) {
    fbw_spi_frame( fbw_vap_nframe );
    fbw_vap_xor = 0;
    IBEX_REG( IBEX_SPI_MASTER + IBEX_SPI_CS ) = 1;
    IBEX_REG( IBEX_SPI_MASTER + IBEX_SPI_TXDATA ) = fbw_frame[ 0 ];
    fbw_vap_state = 1;
    pb_arm( &pb_timer_d, 0, now + FBW_SPI_GAP_CYCLES );
    return;
  }

  /* Byte fbw_vap_state - 1 of the FBW's frame; the last one is its
     checksum */
  rx = ( unsigned char ) IBEX_REG( IBEX_SPI_MASTER + IBEX_SPI_RXDATA );
  if ( fbw_vap_state < FRAME_LENGTH ) {
    fbw_vap_xor ^= rx;
    IBEX_REG( IBEX_SPI_MASTER + IBEX_SPI_TXDATA ) = fbw_frame[ fbw_vap_state ];
    fbw_vap_state++;
    pb_arm( &pb_timer_d, 0, now + FBW_SPI_GAP_CYCLES );
  } else {
    if ( rx != fbw_vap_xor )
      fbw_vap_errors++;
    IBEX_REG( IBEX_SPI_MASTER + IBEX_SPI_CS ) = 0;
    fbw_vap_state = 0;
    fbw_vap_nframe++;
    fbw_vap_frame_time += 3 * ( pb_time_t ) papabench_tick_cycles;
    pb_arm( &pb_timer_d, 0, fbw_vap_frame_time );
  }
}


static void fbw_vap_init( void )
{
  pb_timer_d.fn[ 0 ] = fbw_vap_event;
  IBEX_REG( IBEX_SPI_MASTER + IBEX_SPI_DIV ) = FBW_SPI_DIV;
  IBEX_REG( IBEX_SPI_MASTER + IBEX_SPI_CTRL ) = IBEX_SPI_EN;
  fbw_vap_frame_time = pb_now() + papabench_tick_cycles;
  pb_arm( &pb_timer_d, 0, fbw_vap_frame_time );
}
#endif


/* ---------------------------------------------------------------- PPM --- */

static unsigned char fbw_ppm_on;


static void fbw_ppm_irq( void )
{
  pb_gpi_ack();
  ICR1 = PB_TCNT1( fbw_counters() );
  if ( TIMSK & _BV( TICIE1 ) )
    papabench_isr_run( FBW_ISR_PPM, __vector_5 );
}


static void fbw_ppm_sync( void )
{
  unsigned char on = ( TIMSK & _BV( TICIE1 ) ) != 0;

  if ( on == fbw_ppm_on )
    return;
  fbw_ppm_on = on;
  if ( on )
    ibex_irq_enable( IBEX_IRQ_GPIO0 );
  else
    ibex_irq_disable( IBEX_IRQ_GPIO0 );
}


/* --------------------------------------------------------------- init --- */

const unsigned int papabench_gpio = IBEX_GPIO0;

const struct papabench_irq papabench_irqs[] = {
  { IBEX_IRQ_TIMER_A, papabench_irq_timer_a, 1 },
  { IBEX_IRQ_TIMER_B, papabench_irq_timer_b, 1 },
  { IBEX_IRQ_TIMER_E, papabench_irq_timer_e, 1 },
  { IBEX_IRQ_SPI_S, fbw_spi_irq, 1 },
  { IBEX_IRQ_GPIO0, fbw_ppm_irq, 0 },
#if !PAPABENCH_JOINT
  /* virtual Autopilot */
  { IBEX_IRQ_TIMER_D, papabench_irq_timer_d, 1 },
#endif
};

const unsigned int papabench_nirqs =
  sizeof( papabench_irqs ) / sizeof( papabench_irqs[ 0 ] );


void papabench_periph_init( void )
{
  unsigned int i;

  pb_timer_a.fn[ 0 ] = fbw_oc1a_event;
  pb_timer_b.fn[ 0 ] = fbw_adc_event;
  pb_timer_e.fn[ 0 ] = fbw_uart_event;

  for ( i = 0; i < FBW_4017_CHANNELS; i++ ) {
    IBEX_REG( IBEX_PWM_PERIOD( i ) ) = FBW_SERVO_PERIOD - 1;
    IBEX_REG( IBEX_PWM_WIDTH( i ) ) = 0;
  }

  /* Slave not selected until the first frame */
  PINB |= _BV( FBW_SPI_SS_PIN );
  fbw_spdr_seen = papabench_spdr_accesses;
#if !PAPABENCH_JOINT
  fbw_vap_init();
#endif

  pb_gpio_init();
  pb_gpo_write( PB_GPO_ENV, PB_GPO_ENV );
}


void papabench_periph_poll( void )
{
  pb_oc_sync( &fbw_oc, TIMSK & _BV( OCIE1A ), OCR1A );
  fbw_adc_sync();
  fbw_uart_sync();
  fbw_ppm_sync();
  fbw_spi_sync();
  pb_avr_snapshot_done();
}


/* frames: chip-select releases seen by the slave; frames_err: upstream
   count of frames from the Autopilot with a bad checksum
   (to_mega128.nb_err); tx_underrun: frames in which the slave had no byte
   queued when the master started one (sent 0x00: the ISR wrote SPDR late,
   or nothing was written, as before the first frame); spdr_dropped: SPDR
   writes dropped because the previous byte was still queued (see periph.h);
   virtual_*: FBW frames checked by the virtual Autopilot (single run) */
void papabench_periph_report( void )
{
  papabench_report_value( "frames", fbw_spi_frames );
  papabench_report_value( "frames_err", to_mega128.nb_err );
  papabench_report_value( "tx_underrun", fbw_spi_underruns );
  papabench_report_value( "spdr_dropped", pb_spdr_dropped );
#if !PAPABENCH_JOINT
  papabench_report_value( "virtual_frames", fbw_vap_nframe );
  papabench_report_value( "virtual_err", fbw_vap_errors );
#endif
}
