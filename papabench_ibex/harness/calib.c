/*
  Compiled with -finstrument-functions like the PapaBench tasks: timing this
  empty function gives the cycles the entry/exit hooks add to every sample.
*/

#include "papabench_harness.h"


void __attribute__( ( noinline ) ) papabench_calib( void )
{
  asm volatile( "" : : : "memory" );
}
