// Smoke test of the PapaBench SoC patches (hw/patches/), run by 'make hwtest'.
//
// TimerD (0x80040000, fast IRQ 22), TimerE (0x80050000, IRQ 23), one after
// the other: mtime counts, 4 periodic interrupts on the timer's own line,
// each taken after its compare time, and no other line raised (TimerA/B
// 18/19, SPI master/slave 20/21, the other new timer, the GPIO banks 17/24).
// GPIO banks (patch 0003, hw/rtl/papabench_gpio.sv): the two OUT registers
// are independent; bank 1 (IRQ 24): with the Autopilot environment enabled
// (bank 1 gp_o[0]) every falling edge of the modem clock (gp_i[0]) raises
// one interrupt, acknowledged by writing 1 to IRQ_STATUS, while bank 0
// (IRQ 17, radio PPM not enabled) stays quiet; bank 0: with IRQ_EN clear a
// PPM edge sets IRQ_STATUS but raises no interrupt, the write-1-to-clear
// clears it, and with IRQ_EN set the next edge raises IRQ 17.
// SPI loopback kept by patch 0005, at the settings of the PapaBench models
// (SCK 1 MHz = DIV 24, mode 0, CS by software): the master exchanges three
// bytes with the slave (slave bytes queued before), each side receives the
// other's through its RX interrupt (IRQ 20 / 21), and releasing CS sets the
// slave's FRAME_DONE.
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

// GPIO banks (harness/ibex_io.h); gp_o[0] of a bank enables its program's
// stimuli, gp_i[0] is its input line (harness/periph.h)
#define GPIO0 0x80002000u
#define GPIO1 0x80060000u
#define GPIO_OUT 0x00
#define GPIO_IRQ_EN 0x0C
#define GPIO_IRQ_STATUS 0x10
#define GPIO_IRQ_FALL 0x14
#define GPO_ENV 0x01u
#define GPI_LINE 0x01u

// Radio PPM: first edge 1000 us after the enable, then one every channel
// (at least 1000 us, hw/rtl/papabench_env.sv)
#define PPM_FIRST_WAIT ( 1000u * 50u + 2000u )
#define PPM_NEXT_WAIT ( 2200u * 50u )

// PWM channel 0: 100000-cycle period (> 2^16), 75000-cycle pulse
#define PWM_PERIOD 100000u
#define PWM_PULSE 75000u

struct hw_timer {
  const char *name;
  uint32_t base;
  uint32_t irq;
};

static const struct hw_timer hw_timers[] = {
  { "TimerD", 0x80040000, 22 },
  { "TimerE", 0x80050000, 23 },
};

#define NTIMERS ( sizeof( hw_timers ) / sizeof( hw_timers[ 0 ] ) )

// Lines that must stay quiet while one timer is tested
static const uint32_t hw_quiet_irqs[] = { 17, 18, 19, 20, 21, 22, 23, 24 };

#define NQUIET ( sizeof( hw_quiet_irqs ) / sizeof( hw_quiet_irqs[ 0 ] ) )

static const struct hw_timer *hw_cur;
static volatile uint32_t hw_timer_count;
static volatile uint32_t hw_timer_late;  // taken before its compare time
static volatile uint32_t hw_timer_latency_max;
static volatile uint32_t hw_other_count;
static volatile uint32_t hw_modem_count;
static volatile uint32_t hw_ppm_count;
static uint32_t hw_timer_next;

static uint32_t hw_mtime( void ) {
  return DEV_READ( hw_cur->base + TIMER_MTIME );
}

static void hw_timecmp( uint32_t t ) {
  DEV_WRITE( hw_cur->base + TIMER_MTIMECMP, -1 );
  DEV_WRITE( hw_cur->base + TIMER_MTIMECMPH, 0 );
  DEV_WRITE( hw_cur->base + TIMER_MTIMECMP, t );
}

static void hw_wait( uint32_t cycles ) {
  uint32_t start = hw_mtime();
  while ( hw_mtime() - start < cycles )
    ;
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
  DEV_WRITE( GPIO1 + GPIO_IRQ_STATUS, GPI_LINE );
  hw_modem_count++;
}

void hw_ppm_isr( void ) __attribute__( ( interrupt ) );
void hw_ppm_isr( void ) {
  DEV_WRITE( GPIO0 + GPIO_IRQ_STATUS, GPI_LINE );
  hw_ppm_count++;
}

// SPI (harness/ibex_io.h)
#define SPI_M 0x80004000u
#define SPI_S 0x80005000u
#define SPI_CTRL 0x00
#define SPI_DIV 0x04
#define SPI_TXDATA 0x08
#define SPI_RXDATA 0x0C
#define SPI_STATUS 0x10
#define SPI_CS 0x14
#define SPI_IE 0x18
#define SPI_RX_EMPTY 0x08u
#define SPI_FRAME_DONE 0x80u
#define SPI_BYTES 3u

static const uint8_t hw_spi_m_tx[ SPI_BYTES ] = { 0x11, 0x22, 0x33 };
static const uint8_t hw_spi_s_tx[ SPI_BYTES ] = { 0xA5, 0x5A, 0x3C };
static volatile uint8_t hw_spi_m_rx[ SPI_BYTES ], hw_spi_s_rx[ SPI_BYTES ];
static volatile uint32_t hw_spi_m_n, hw_spi_s_n;

static void hw_spi_pop( uint32_t base, volatile uint8_t *buf, volatile uint32_t *n ) {
  while ( !( DEV_READ( base + SPI_STATUS ) & SPI_RX_EMPTY ) ) {
    uint8_t b = DEV_READ( base + SPI_RXDATA );
    if ( *n < SPI_BYTES )
      buf[ *n ] = b;
    ( *n )++;
  }
}

void hw_spi_m_isr( void ) __attribute__( ( interrupt ) );
void hw_spi_m_isr( void ) {
  hw_spi_pop( SPI_M, hw_spi_m_rx, &hw_spi_m_n );
}

void hw_spi_s_isr( void ) __attribute__( ( interrupt ) );
void hw_spi_s_isr( void ) {
  hw_spi_pop( SPI_S, hw_spi_s_rx, &hw_spi_s_n );
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

  hw_cur = &hw_timers[ 0 ];

  // GPIO banks: independent OUT registers
  DEV_WRITE( GPIO1 + GPIO_OUT, 0x80 );
  fails += hw_check( "GPIO banks: independent OUT registers",
                     DEV_READ( GPIO0 + GPIO_OUT ) == 0 && DEV_READ( GPIO1 + GPIO_OUT ) == 0x80 );
  DEV_WRITE( GPIO1 + GPIO_OUT, 0 );

  // Bank 1 on IRQ 24: modem clock edges; bank 0 (IRQ 17) stays quiet
  hw_other_count = 0;
  DEV_WRITE( GPIO0 + GPIO_IRQ_FALL, GPI_LINE );
  DEV_WRITE( GPIO0 + GPIO_IRQ_EN, GPI_LINE );
  DEV_WRITE( GPIO1 + GPIO_IRQ_FALL, GPI_LINE );
  DEV_WRITE( GPIO1 + GPIO_IRQ_EN, GPI_LINE );
  install_exception_handler( 24, &hw_modem_isr );
  install_exception_handler( 17, &hw_other_isr );
  enable_interrupts( ( 1u << 24 ) | ( 1u << 17 ) );
  DEV_WRITE( GPIO1 + GPIO_OUT, GPO_ENV );
  set_global_interrupt_enable( 1 );
  hw_wait( MODEM_WAIT );
  set_global_interrupt_enable( 0 );
  disable_interrupts( ( 1u << 24 ) | ( 1u << 17 ) );
  DEV_WRITE( GPIO1 + GPIO_OUT, 0 );
  DEV_WRITE( GPIO1 + GPIO_IRQ_EN, 0 );
  fails += hw_check( "GPIO bank 1 IRQ 24: one interrupt per modem clock edge",
                     hw_modem_count >= MODEM_EDGES && hw_modem_count <= MODEM_EDGES + 1 );
  fails += hw_check( "GPIO bank 0 IRQ 17 not raised", hw_other_count == 0 );

  // Bank 0: a PPM edge with IRQ_EN clear only sets IRQ_STATUS
  install_exception_handler( 17, &hw_ppm_isr );
  DEV_WRITE( GPIO0 + GPIO_IRQ_EN, 0 );
  enable_interrupts( 1u << 17 );
  set_global_interrupt_enable( 1 );
  DEV_WRITE( GPIO0 + GPIO_OUT, GPO_ENV );
  hw_wait( PPM_FIRST_WAIT );
  fails += hw_check( "GPIO bank 0: masked PPM edge latched in IRQ_STATUS, no IRQ 17",
                     DEV_READ( GPIO0 + GPIO_IRQ_STATUS ) == GPI_LINE && hw_ppm_count == 0 );
  DEV_WRITE( GPIO0 + GPIO_IRQ_STATUS, GPI_LINE );
  fails += hw_check( "GPIO bank 0: IRQ_STATUS write 1 to clear",
                     DEV_READ( GPIO0 + GPIO_IRQ_STATUS ) == 0 );
  DEV_WRITE( GPIO0 + GPIO_IRQ_EN, GPI_LINE );
  hw_wait( PPM_NEXT_WAIT );
  set_global_interrupt_enable( 0 );
  disable_interrupts( 1u << 17 );
  DEV_WRITE( GPIO0 + GPIO_OUT, 0 );
  DEV_WRITE( GPIO0 + GPIO_IRQ_EN, 0 );
  fails += hw_check( "GPIO bank 0 IRQ 17: next PPM edge raises it",
                     hw_ppm_count >= 1 && hw_ppm_count <= 2 );

  // SPI loopback: master -> slave, 3 bytes each way
  install_exception_handler( 20, &hw_spi_m_isr );
  install_exception_handler( 21, &hw_spi_s_isr );
  DEV_WRITE( SPI_S + SPI_CTRL, 1 );
  DEV_WRITE( SPI_S + SPI_IE, 1 );
  for ( i = 0; i < SPI_BYTES; i++ )
    DEV_WRITE( SPI_S + SPI_TXDATA, hw_spi_s_tx[ i ] );
  DEV_WRITE( SPI_M + SPI_DIV, 24 );
  DEV_WRITE( SPI_M + SPI_CTRL, 1 );
  DEV_WRITE( SPI_M + SPI_IE, 1 );
  DEV_WRITE( SPI_M + SPI_CS, 1 );
  enable_interrupts( ( 1u << 20 ) | ( 1u << 21 ) );
  set_global_interrupt_enable( 1 );
  for ( i = 0; i < SPI_BYTES; i++ )
    DEV_WRITE( SPI_M + SPI_TXDATA, hw_spi_m_tx[ i ] );
  start = hw_mtime();
  // 3 bytes of 17 * 25 cycles each
  while ( hw_mtime() - start < SPI_BYTES * 17 * 25 + 500 )
    ;
  DEV_WRITE( SPI_M + SPI_CS, 0 );
  start = hw_mtime();
  while ( hw_mtime() - start < 100 )
    ;
  set_global_interrupt_enable( 0 );
  disable_interrupts( ( 1u << 20 ) | ( 1u << 21 ) );
  {
    int m_ok = hw_spi_m_n == SPI_BYTES, s_ok = hw_spi_s_n == SPI_BYTES;
    for ( i = 0; i < SPI_BYTES; i++ ) {
      m_ok &= hw_spi_m_rx[ i ] == hw_spi_s_tx[ i ];
      s_ok &= hw_spi_s_rx[ i ] == hw_spi_m_tx[ i ];
    }
    fails += hw_check( "SPI master IRQ 20: received the slave's 3 bytes", m_ok );
    fails += hw_check( "SPI slave IRQ 21: received the master's 3 bytes", s_ok );
  }
  fails += hw_check( "SPI slave FRAME_DONE after CS release",
                     ( DEV_READ( SPI_S + SPI_STATUS ) & SPI_FRAME_DONE ) != 0 );

  // Keep running for two PWM periods so the waveform shows them
  start = hw_mtime();
  while ( hw_mtime() - start < 2 * PWM_PERIOD + 1000 )
    ;

  puts( fails ? "hw_smoke: FAIL\n" : "hw_smoke: PASS\n" );
  sim_halt();
  return 0;
}
