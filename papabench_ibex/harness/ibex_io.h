/*
  Ibex SoC registers and CSR helpers for both sides of the harness.

  Free of fixed-width typedefs (see papabench_harness.h): registers are
  'unsigned int', which is 32 bits on RV32 with either typedef set. Addresses
  follow Secure-Ibex reference_system_regs.h, plus what exists only in the
  patched SoC: TimerC (hw/patches/0001-soc-timer-c.patch), TimerD, TimerE
  and the gp_i[1] interrupt (hw/patches/0004-soc-timers-d-e-gpio-irq.patch).

  One peripheral, one function: across FBW and Autopilot no peripheral is
  used for two different things, so the same map holds if both programs
  ever share the SoC.

    Machine timer  scheduler tick (both) and time base of the models
    TimerA         FBW Timer1 compare A: servo pulses
    TimerB         ADC conversions (both)
    TimerC         SPI link between the two MCUs (both ends)
    TimerD         Autopilot Timer1 compare A: link_fbw byte pacing
    TimerE         FBW UART (virtual: timing of the transmitter only)
    UART           Autopilot GPS receiver (UART1 on the ATmega128)
    gp_i[0]        FBW radio PPM (input capture)
    gp_i[1]        Autopilot modem clock (INT4)
    gp_o           bits in periph.h, each with one function
    PWM ch 0-9     FBW servos
*/

#ifndef IBEX_IO_H
#define IBEX_IO_H

#define IBEX_REG( addr ) ( *( volatile unsigned int * )( addr ) )

/* Timers: timer.sv (mtime +1 per cycle; IRQ sticky until mtimecmp written;
   mtimecmp resets to all ones) */
#define IBEX_TIMER    0x80000000u
#define IBEX_TIMER_A  0x80010000u
#define IBEX_TIMER_B  0x80020000u
#define IBEX_TIMER_C  0x80030000u
#define IBEX_TIMER_D  0x80040000u
#define IBEX_TIMER_E  0x80050000u
#define IBEX_MTIME     0x0
#define IBEX_MTIMEH    0x4
#define IBEX_MTIMECMP  0x8
#define IBEX_MTIMECMPH 0xC

/* UART: IRQ = RX FIFO not empty; 115200 baud at 50 MHz */
#define IBEX_UART        0x80001000u
#define IBEX_UART_RX     0x0
#define IBEX_UART_TX     0x4
#define IBEX_UART_STATUS 0x8
#define IBEX_UART_RX_EMPTY 0x1
#define IBEX_UART_TX_FULL  0x2

/* GPIO: gp_o[15:0] out, gp_i[7:0] in; gp_i[0] is fast IRQ 17, gp_i[1] fast
   IRQ 21 (both level-sensitive) */
#define IBEX_GPIO     0x80002000u
#define IBEX_GPIO_OUT 0x0
#define IBEX_GPIO_IN  0x4

/* PWM: channel i pulse width at +8i, period (counter max) at +8i+4; high for
   'width' cycles out of 'max + 1'; 20-bit counter in the patched SoC */
#define IBEX_PWM 0x80003000u
#define IBEX_PWM_WIDTH( i )  ( IBEX_PWM + 8 * ( i ) )
#define IBEX_PWM_PERIOD( i ) ( IBEX_PWM + 8 * ( i ) + 4 )
#define IBEX_PWM_CHANNELS 12

/* mcause / mie bit numbers */
#define IBEX_IRQ_TIMER   7
#define IBEX_IRQ_UART    16
#define IBEX_IRQ_GPIO0   17
#define IBEX_IRQ_TIMER_A 18
#define IBEX_IRQ_TIMER_B 19
#define IBEX_IRQ_TIMER_C 20
#define IBEX_IRQ_GPIO1   21
#define IBEX_IRQ_TIMER_D 22
#define IBEX_IRQ_TIMER_E 23

/* Cycles per second of the simulated SoC */
#define IBEX_HZ 50000000u

static inline unsigned int ibex_mcycle( void )
{
  unsigned int v;

  asm volatile( "csrr %0, mcycle" : "=r"( v ) : : "memory" );
  return v;
}

static inline void ibex_irq_enable( unsigned int irq )
{
  asm volatile( "csrs mie, %0" : : "r"( 1u << irq ) : "memory" );
}

static inline void ibex_irq_disable( unsigned int irq )
{
  asm volatile( "csrc mie, %0" : : "r"( 1u << irq ) : "memory" );
}

/* Clears mstatus.MIE and returns the previous mstatus */
static inline unsigned int ibex_irq_save( void )
{
  unsigned int v;

  asm volatile( "csrrci %0, mstatus, 8" : "=r"( v ) : : "memory" );
  return v;
}

static inline void ibex_irq_restore( unsigned int mstatus )
{
  if ( mstatus & 8 )
    asm volatile( "csrsi mstatus, 8" : : : "memory" );
}

#endif /* IBEX_IO_H */
