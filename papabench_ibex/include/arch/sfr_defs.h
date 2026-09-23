/*
  Replacement for bench/parallel/PapaBench/arch/include/avr/arch/sfr_defs.h.

  It is found first on the include path, so every <arch/sfr_defs.h> resolves
  here. Upstream maps AVR I/O registers to raw addresses 0x20-0xFF, where the
  Ibex SoC has no memory; this version maps the same register addresses onto
  offsets inside papabench_sfr[], a RAM array defined by the harness.

  One register bit is not plain memory: the Timer2 overflow flag (TIFR.TOV2)
  that timer_periodic() polls to pace the scheduler. It is set by the Ibex
  timer ISR (harness.c) and reading it through bit_is_set() consumes it
  (test-and-clear), standing in for the AVR write-1-to-clear that
  timer_periodic() relies on. The Makefile passes PAPABENCH_TIFR_ADDR (data
  address of TIFR for the device) and PAPABENCH_TICK_BIT (TOV2).

  Only the macros used by PapaBench are provided. _SFR_ASM_COMPAT and
  __SFR_OFFSET are not supported.
*/

#ifndef _SFR_DEFS_H_
#define _SFR_DEFS_H_

#include <inttypes.h>

#define PAPABENCH_SFR_SIZE 0x100

extern volatile uint8_t papabench_sfr[ PAPABENCH_SFR_SIZE ];

/* AVR data-space address -> harness RAM */
#define _MMIO_BYTE( mem_addr ) \
  ( *( volatile uint8_t * )( &papabench_sfr[ ( mem_addr ) ] ) )
#define _MMIO_WORD( mem_addr ) \
  ( *( volatile uint16_t * )( &papabench_sfr[ ( mem_addr ) ] ) )

#define _SFR_MEM8( mem_addr ) _MMIO_BYTE( mem_addr )
#define _SFR_MEM16( mem_addr ) _MMIO_WORD( mem_addr )
#define _SFR_IO8( io_addr ) _MMIO_BYTE( ( io_addr ) + 0x20 )
#define _SFR_IO16( io_addr ) _MMIO_WORD( ( io_addr ) + 0x20 )

/* Register lvalue -> AVR data-space address (offset inside papabench_sfr) */
#define _SFR_MEM_ADDR( sfr ) \
  ( ( unsigned long )( &( sfr ) ) - ( unsigned long )( papabench_sfr ) )
#define _SFR_IO_ADDR( sfr ) ( _SFR_MEM_ADDR( sfr ) - 0x20 )
#define _SFR_IO_REG_P( sfr ) ( _SFR_MEM_ADDR( sfr ) < 0x60 )
#define _SFR_ADDR( sfr ) _SFR_MEM_ADDR( sfr )

#define _SFR_BYTE( sfr ) _MMIO_BYTE( _SFR_ADDR( sfr ) )
#define _SFR_WORD( sfr ) _MMIO_WORD( _SFR_ADDR( sfr ) )

#define _BV( bit ) ( 1 << ( bit ) )

#ifndef _VECTOR
#define _VECTOR( N ) __vector_ ## N
#endif

#if !defined( PAPABENCH_TIFR_ADDR ) || !defined( PAPABENCH_TICK_BIT )
#error "PAPABENCH_TIFR_ADDR and PAPABENCH_TICK_BIT must be defined"
#endif

/* Defined in harness/runtime.c: returns 1 and clears the flag if a tick is
   pending, 0 otherwise. The address test folds at compile time. */
unsigned char papabench_tick_take( void );

#define PAPABENCH_IS_TICK( sfr, bit ) \
  ( _SFR_ADDR( sfr ) == PAPABENCH_TIFR_ADDR && ( bit ) == PAPABENCH_TICK_BIT )

#define bit_is_set( sfr, bit ) \
  ( PAPABENCH_IS_TICK( sfr, bit ) ? \
    ( papabench_tick_take() ? _BV( bit ) : 0 ) : \
    ( _SFR_BYTE( sfr ) & _BV( bit ) ) )
#define bit_is_clear( sfr, bit ) ( !bit_is_set( sfr, bit ) )
#define loop_until_bit_is_set( sfr, bit ) \
  do { } while ( bit_is_clear( sfr, bit ) )
#define loop_until_bit_is_clear( sfr, bit ) \
  do { } while ( bit_is_set( sfr, bit ) )

#endif /* _SFR_DEFS_H_ */
