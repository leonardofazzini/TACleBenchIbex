/*
  Autopilot (ATmega128) peripheral model on the Ibex SoC (see periph.h).

  AVR source              Ibex resource               upstream ISR
  Timer1 compare A        TimerA channel 0            __vector_12 link_fbw
  SPI master              TimerC channel 0            __vector_17 SPI
  ADC conversion          TimerB channel 0            __vector_21
  UART1 receive (GPS)     UART RX, IRQ 16             __vector_30 GPS
  INT4 (modem clock)      GPIO gp_i[0], IRQ 17        __vector_5  modem

  SPI master: link_fbw_send() and the OCR1A ISR write SPDR to send a byte;
  the transfer (8 bits at the SPCR clock rate) starts at the first
  synchronisation point after the software touched SPDR while the SPI is
  enabled as master (papabench_spdr_accesses). The FBW MCU does not exist
  in this build: a virtual slave answers each frame with the bytes of
  ap_spi_frame() (deterministic scenario: radio OK, mode switch
  MANUAL -> AUTO1 -> AUTO2 within the first 4 frames, full throttle), which
  is what radio_control_task receives.

  GPS UBX bytes and the modem clock come from the simulation environment
  (hw/rtl/papabench_env.sv) through the UART and gp_i[0]; the modem data bit
  (PORTD.6) goes out on gp_o.
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

static struct pb_oc ap_oc = { &pb_timer_a, 0 };


static void ap_oc1a_event( void )
{
  ap_counters();
  papabench_isr_run( AP_ISR_OC1A, __vector_12 );
}


/* ---------------------------------------------------------------- SPI --- */

static unsigned char ap_frame[ FRAME_LENGTH ];
static unsigned char ap_spi_busy, ap_spi_in_frame;
static unsigned int ap_spi_seen, ap_spi_idx, ap_spi_nframe;


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


static void ap_spi_event( void )
{
  ap_spi_busy = 0;
  papabench_spdr_rx = ap_frame[ ap_spi_idx < FRAME_LENGTH ? ap_spi_idx : 0 ];
  ap_spi_idx++;
  ap_counters();
  SPSR |= _BV( SPIF );
  if ( SPCR & _BV( SPIE ) ) {
    papabench_isr_run( AP_ISR_SPI, __vector_17 );
    /* SPIF is cleared by hardware when the vector executes */
    SPSR &= ~_BV( SPIF );
  }
}


static void ap_spi_sync( void )
{
  static const unsigned int div[ 4 ] = { 4, 16, 64, 128 };

  if ( !( SPCR & _BV( SPE ) ) || !( SPCR & _BV( MSTR ) ) ) {
    ap_spi_in_frame = 0;
    return;
  }
  if ( ap_spi_busy || papabench_spdr_accesses == ap_spi_seen )
    return;
  ap_spi_seen = papabench_spdr_accesses;
  if ( !ap_spi_in_frame ) {
    ap_spi_in_frame = 1;
    ap_spi_idx = 0;
    ap_spi_frame( ap_spi_nframe++ );
  }
  ap_spi_busy = 1;
  pb_arm( &pb_timer_c, 0,
          pb_now() + PB_IBEX_OF_AVR( 8 * div[ SPCR & 0x3 ] ) );
}


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

void papabench_irq_uart( void )
{
  while ( !( IBEX_REG( IBEX_UART + IBEX_UART_STATUS ) & IBEX_UART_RX_EMPTY ) ) {
    UDR1 = ( unsigned char ) IBEX_REG( IBEX_UART + IBEX_UART_RX );
    if ( UCSR1B & _BV( RXCIE ) )
      papabench_isr_run( AP_ISR_GPS, __vector_30 );
  }
}


/* -------------------------------------------------------------- modem --- */

static unsigned char ap_gpio_on;


void papabench_irq_gpio( void )
{
  pb_gpio_ack();
  if ( EIMSK & _BV( INT4 ) ) {
    papabench_isr_run( AP_ISR_MODEM, __vector_5 );
    pb_gpo_write( PB_GPO_MODEM_TX,
                  ( PORTD & _BV( AP_MODEM_TX_DATA ) ) ? PB_GPO_MODEM_TX : 0 );
  }
}


/* INT4 enabled by MODEM_CHECK_RUNNING(), disabled by the ISR when the
   buffer is empty */
static void ap_gpio_sync( void )
{
  unsigned char on = ( EIMSK & _BV( INT4 ) ) != 0;

  if ( on == ap_gpio_on )
    return;
  ap_gpio_on = on;
  if ( on )
    ibex_irq_enable( IBEX_IRQ_GPIO );
  else
    ibex_irq_disable( IBEX_IRQ_GPIO );
}


/* --------------------------------------------------------------- init --- */

void papabench_periph_init( void )
{
  pb_timer_a.fn[ 0 ] = ap_oc1a_event;
  pb_timer_b.fn[ 0 ] = ap_adc_event;
  pb_timer_c.fn[ 0 ] = ap_spi_event;
  ap_spi_seen = papabench_spdr_accesses;

  pb_gpo_write( PB_GPO_MODE_MASK | PB_GPO_MODEM_TX,
                ( PB_ENV_AUTOPILOT << PB_GPO_MODE_SHIFT ) | PB_GPO_MODEM_TX );
}


void papabench_periph_poll( void )
{
  pb_oc_sync( &ap_oc, TIMSK & _BV( OCIE1A ), OCR1A );
  ap_spi_sync();
  ap_adc_sync();
  ap_gpio_sync();
  pb_avr_snapshot_done();
}
