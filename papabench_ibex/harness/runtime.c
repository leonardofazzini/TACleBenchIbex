/*
  Minimal runtime for PapaBench on Ibex: the RAM array behind the AVR
  registers, the Timer2-overflow tick flag, and the mem* functions GCC emits
  for struct copies (no libc).
  Built with -fno-builtin -fno-tree-loop-distribute-patterns so that the
  loops below are not turned back into calls to themselves.
*/

#include <stddef.h>

/* Declared in include/arch/sfr_defs.h */
volatile unsigned char papabench_sfr[ 0x100 ] __attribute__( ( aligned( 4 ) ) );

/* Set by the timer ISR (harness.c), consumed by timer_periodic() */
volatile unsigned char papabench_tick_pending;


unsigned char papabench_tick_take( void )
{
  /* A tick arriving between the test and the clear is merged with the one
     being consumed, as on the AVR flag */
  if ( !papabench_tick_pending )
    return 0;
  papabench_tick_pending = 0;
  return 1;
}


void *memcpy( void *dst, const void *src, size_t n )
{
  unsigned char *d = dst;
  const unsigned char *s = src;

  while ( n-- )
    *d++ = *s++;
  return dst;
}


void *memset( void *dst, int c, size_t n )
{
  unsigned char *d = dst;

  while ( n-- )
    *d++ = ( unsigned char ) c;
  return dst;
}
