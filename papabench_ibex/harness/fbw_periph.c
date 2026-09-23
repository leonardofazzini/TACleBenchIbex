/*
  Fly-By-Wire (ATmega8) peripheral model on the Ibex SoC (see periph.h).

  AVR source              Ibex resource               upstream ISR
  Timer1 compare A        TimerA channel 0            __vector_6  servo
  SPI slave (autopilot)   TimerC channel 0            __vector_10 SPI
  UART transmit complete  TimerC channel 2 + UART TX  __vector_13
  ADC conversion          TimerB channel 0            __vector_14
  Timer1 input capture    GPIO gp_i[0], IRQ 17        __vector_5  radio PPM

  Servos: every servo compare also drives the real PWM. The 4017 decade
  counter turns the interval after compare k into the pulse of servo k, so
  the width just added to OCR1A becomes the pulse of PWM channel k (20 ms
  period); gp_o carries the 4017 clock and reset for the waveform.

  The autopilot MCU does not exist in this build: a virtual SPI master sends
  it a frame of FRAME_LENGTH bytes every 3 scheduler ticks (the Autopilot's
  link_fbw_send() rate), selecting the slave through PINB.SS, one byte per
  master byte time + the master's OCR1A delay. Its commands come from
  fbw_spi_frame() (deterministic scenario).

  The radio PPM train comes from the simulation environment
  (hw/rtl/papabench_env.sv) on gp_i[0]; the input-capture value is TimerA's
  AVR time when the interrupt is taken.
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
/* Virtual master: SPI byte at fck/16, then link_fbw's OCR1A = TCNT1 + 200 */
#define FBW_SPI_BYTE_CYCLES PB_IBEX_OF_AVR( 8 * 16 )
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


/* --------------------------------------------------------------- UART --- */

static unsigned char fbw_tx_busy;


static void fbw_uart_event( void )
{
  fbw_tx_busy = 0;
  if ( UCSRB & _BV( TXCIE ) )
    papabench_isr_run( FBW_ISR_UART_TX, __vector_13 );
}


/* A byte is written to UDR exactly when uart_transmit() or the ISR leaves
   TXCIE set with the transmitter idle */
static void fbw_uart_sync( void )
{
  if ( !( UCSRB & _BV( TXCIE ) ) || fbw_tx_busy )
    return;
  if ( !( IBEX_REG( IBEX_UART + IBEX_UART_STATUS ) & IBEX_UART_TX_FULL ) )
    IBEX_REG( IBEX_UART + IBEX_UART_TX ) = UDR;
  fbw_tx_busy = 1;
  pb_arm( &pb_timer_c, 2, pb_now() + FBW_UART_BYTE_CYCLES );
}


/* ---------------------------------------------------------------- SPI --- */

static unsigned char fbw_frame[ FRAME_LENGTH ];
/* 0: select the slave; 1..FRAME_LENGTH: byte 0..FRAME_LENGTH-1 */
static unsigned int fbw_spi_state;
static unsigned int fbw_spi_nframe;
static pb_time_t fbw_spi_frame_time;


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


static void fbw_spi_event( void )
{
  pb_time_t now = pb_now();

  if ( fbw_spi_state == 0 ) {
    fbw_spi_frame( fbw_spi_nframe );
    PINB &= ~_BV( FBW_SPI_SS_PIN );
    fbw_spi_state = 1;
    pb_arm( &pb_timer_c, 0, now + FBW_SPI_BYTE_CYCLES );
    return;
  }

  papabench_spdr_rx = fbw_frame[ fbw_spi_state - 1 ];
  if ( ( SPCR & _BV( SPE ) ) && ( SPCR & _BV( SPIE ) ) )
    papabench_isr_run( FBW_ISR_SPI, __vector_10 );

  if ( fbw_spi_state < FRAME_LENGTH ) {
    fbw_spi_state++;
    pb_arm( &pb_timer_c, 0, now + FBW_SPI_GAP_CYCLES );
  } else {
    PINB |= _BV( FBW_SPI_SS_PIN );
    fbw_spi_state = 0;
    fbw_spi_nframe++;
    fbw_spi_frame_time += 3 * ( pb_time_t ) papabench_tick_cycles;
    pb_arm( &pb_timer_c, 0, fbw_spi_frame_time );
  }
}


/* ---------------------------------------------------------------- PPM --- */

static unsigned char fbw_gpio_on;


void papabench_irq_gpio( void )
{
  pb_gpio_ack();
  ICR1 = PB_TCNT1( fbw_counters() );
  if ( TIMSK & _BV( TICIE1 ) )
    papabench_isr_run( FBW_ISR_PPM, __vector_5 );
}


static void fbw_gpio_sync( void )
{
  unsigned char on = ( TIMSK & _BV( TICIE1 ) ) != 0;

  if ( on == fbw_gpio_on )
    return;
  fbw_gpio_on = on;
  if ( on )
    ibex_irq_enable( IBEX_IRQ_GPIO );
  else
    ibex_irq_disable( IBEX_IRQ_GPIO );
}


/* The FBW does not receive on the UART: drain it */
void papabench_irq_uart( void )
{
  while ( !( IBEX_REG( IBEX_UART + IBEX_UART_STATUS ) & IBEX_UART_RX_EMPTY ) )
    ( void ) IBEX_REG( IBEX_UART + IBEX_UART_RX );
}


/* --------------------------------------------------------------- init --- */

void papabench_periph_init( void )
{
  unsigned int i;

  pb_timer_a.fn[ 0 ] = fbw_oc1a_event;
  pb_timer_b.fn[ 0 ] = fbw_adc_event;
  pb_timer_c.fn[ 0 ] = fbw_spi_event;
  pb_timer_c.fn[ 2 ] = fbw_uart_event;

  for ( i = 0; i < FBW_4017_CHANNELS; i++ ) {
    IBEX_REG( IBEX_PWM_PERIOD( i ) ) = FBW_SERVO_PERIOD - 1;
    IBEX_REG( IBEX_PWM_WIDTH( i ) ) = 0;
  }

  /* Slave not selected until the first frame */
  PINB |= _BV( FBW_SPI_SS_PIN );
  fbw_spi_frame_time = pb_now() + papabench_tick_cycles;
  pb_arm( &pb_timer_c, 0, fbw_spi_frame_time );

  pb_gpo_write( PB_GPO_MODE_MASK, PB_ENV_FBW << PB_GPO_MODE_SHIFT );
}


void papabench_periph_poll( void )
{
  pb_oc_sync( &fbw_oc, TIMSK & _BV( OCIE1A ), OCR1A );
  fbw_adc_sync();
  fbw_uart_sync();
  fbw_gpio_sync();
  pb_avr_snapshot_done();
}
