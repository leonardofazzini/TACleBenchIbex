/*
  Ibex SoC registers and CSR helpers for both sides of the harness.

  Free of fixed-width typedefs (see papabench_harness.h): registers are
  'unsigned int', which is 32 bits on RV32 with either typedef set. Addresses
  follow Secure-Ibex reference_system_regs.h (SPI master/slave included),
  plus what exists only in the patched SoC: TimerD, TimerE
  (hw/patches/0002-soc-timers-d-e.patch) and the two GPIO banks with edge
  interrupts (hw/patches/0003-soc-gpio-banks.patch).

  One peripheral, one function: across FBW and Autopilot no peripheral is
  used for two different things, so the same map holds on the SoC of either
  MCU, in a single-MCU run (other MCU virtual) or in the joint run (two
  SoCs, hw/rtl/papabench_dual.sv).

    Machine timer  scheduler tick (both) and time base of the models
    TimerA         FBW Timer1 compare A: servo pulses
    TimerB         ADC conversions (both)
    TimerD         Autopilot Timer1 compare A: link_fbw byte pacing (also
                   the virtual Autopilot's, in a single FBW run)
    TimerE         FBW UART (virtual: timing of the transmitter only)
    SPI master     Autopilot end of the inter-MCU link (also the virtual
                   Autopilot's, in a single FBW run)
    SPI slave      FBW end of the inter-MCU link (also the virtual FBW's, in
                   a single Autopilot run)
    UART           Autopilot GPS receiver (UART1 on the ATmega128)
    GPIO bank 0    FBW: radio PPM (input capture), 4017 clock/reset, stimuli
                   enable, run over (bits in periph.h)
    GPIO bank 1    Autopilot: modem clock (INT4), modem TX data, stimuli
                   enable, run over (bits in periph.h)
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

/* GPIO banks (hw/rtl/papabench_gpio.sv): gp_o[15:0] out, gp_i[7:0] in, the
   gpio.sv map plus edge interrupts. IRQ_STATUS latches the edges selected
   by IRQ_FALL/IRQ_RISE (write 1 to clear); the bank's interrupt line (bank
   0: IRQ 17, bank 1: IRQ 24) is high while IRQ_STATUS & IRQ_EN != 0. */
#define IBEX_GPIO0 0x80002000u
#define IBEX_GPIO1 0x80060000u
#define IBEX_GPIO_OUT        0x00
#define IBEX_GPIO_IN         0x04
#define IBEX_GPIO_IRQ_EN     0x0C
#define IBEX_GPIO_IRQ_STATUS 0x10
#define IBEX_GPIO_IRQ_FALL   0x14
#define IBEX_GPIO_IRQ_RISE   0x18

/* PWM: channel i pulse width at +8i, period (counter max) at +8i+4; high for
   'width' cycles out of 'max + 1'; 20-bit counter in the patched SoC */
#define IBEX_PWM 0x80003000u
#define IBEX_PWM_WIDTH( i )  ( IBEX_PWM + 8 * ( i ) )
#define IBEX_PWM_PERIOD( i ) ( IBEX_PWM + 8 * ( i ) + 4 )
#define IBEX_PWM_CHANNELS 12

/* SPI master and slave (spi_master.sv, spi_slave.sv): 8-bit frames, MSB
   first, TX and RX FIFOs of 16 bytes. TXDATA pushes a byte, RXDATA pops one
   (0 when empty). Master: SCK = 50 MHz / ( 2 * ( DIV + 1 ) ), chip select
   driven by CS. Slave: shifts while its CS input is low, sends 0x00 when its
   TX FIFO is empty; with CPHA = 0 the byte must be queued before the master
   starts it. On the single-MCU SoC the master drives the slave (loopback);
   in the joint run the Autopilot's master drives the FBW's slave. */
#define IBEX_SPI_MASTER 0x80004000u
#define IBEX_SPI_SLAVE  0x80005000u
#define IBEX_SPI_CTRL   0x00
#define IBEX_SPI_DIV    0x04
#define IBEX_SPI_TXDATA 0x08
#define IBEX_SPI_RXDATA 0x0C
#define IBEX_SPI_STATUS 0x10
#define IBEX_SPI_CS     0x14
#define IBEX_SPI_IE     0x18
/* CTRL */
#define IBEX_SPI_EN   0x1u
#define IBEX_SPI_CPOL 0x2u
#define IBEX_SPI_CPHA 0x4u
/* STATUS (FRAME_DONE and the error flags are write-1-to-clear) */
#define IBEX_SPI_TX_FULL    0x01u
#define IBEX_SPI_TX_EMPTY   0x02u
#define IBEX_SPI_RX_EMPTY   0x08u
#define IBEX_SPI_BUSY       0x10u  /* master: shifting; slave: CS asserted */
#define IBEX_SPI_TX_UNDERRUN 0x40u /* slave: sent 0x00, TX FIFO empty */
#define IBEX_SPI_FRAME_DONE 0x80u  /* slave: CS released */
/* IE: RX FIFO not empty; master: idle, slave: FRAME_DONE */
#define IBEX_SPI_IE_RX   0x1u
#define IBEX_SPI_IE_DONE 0x2u

/* mcause / mie bit numbers */
#define IBEX_IRQ_TIMER   7
#define IBEX_IRQ_UART    16
#define IBEX_IRQ_GPIO0   17
#define IBEX_IRQ_TIMER_A 18
#define IBEX_IRQ_TIMER_B 19
#define IBEX_IRQ_SPI_M   20
#define IBEX_IRQ_SPI_S   21
#define IBEX_IRQ_TIMER_D 22
#define IBEX_IRQ_TIMER_E 23
#define IBEX_IRQ_GPIO1   24

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
