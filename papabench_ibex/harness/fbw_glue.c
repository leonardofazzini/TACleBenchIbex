/*
  Fly-By-Wire glue: task table (PapaBench_for_wcet.txt, FBW T1-T5) and the
  device check. Compiled with the PapaBench include paths and flags, without
  instrumentation.

  The upstream main() in fly_by_wire/main.c runs unchanged (renamed with
  -Dmain=papabench_upstream_main): it calls fbw_schedule() on every loop
  iteration and advances _1Hz/_20Hz on every timer tick.
*/

#include <arch/io.h>
#include "papabench_harness.h"

void check_failsafe_task( void );
void check_mega128_values_task( void );
void send_data_to_autopilot_task( void );
void servo_transmit( void );
void test_ppm_task( void );


const char papabench_prog_name[] = "fbw";


const struct papabench_task papabench_tasks[] = {
  PAPABENCH_TASK( "check_failsafe_task", check_failsafe_task, check_failsafe_task ),
  PAPABENCH_TASK( "check_mega128_values_task", check_mega128_values_task, check_mega128_values_task ),
  PAPABENCH_TASK( "send_data_to_autopilot_task", send_data_to_autopilot_task, send_data_to_autopilot_task ),
  PAPABENCH_TASK( "servo_transmit", servo_transmit, servo_transmit ),
  PAPABENCH_TASK( "test_ppm_task", test_ppm_task, test_ppm_task ),
};


const unsigned int papabench_ntasks =
  sizeof( papabench_tasks ) / sizeof( papabench_tasks[ 0 ] );


/* main() enters its loop without waiting for ticks */
const unsigned int papabench_startup_ticks = 0;


int papabench_check( void )
{
  return &TIFR != &papabench_sfr[ PAPABENCH_TIFR_ADDR ] ||
         TOV2 != PAPABENCH_TICK_BIT;
}
