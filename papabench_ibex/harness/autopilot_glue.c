/*
  Autopilot glue: task table (PapaBench_for_wcet.txt, Autopilot T5-T12) and
  the device check. Compiled with the PapaBench include paths and flags,
  without instrumentation.

  The upstream main() in autopilot/mainloop.c runs unchanged (renamed with
  -Dmain=papabench_upstream_main): init, a 30-tick wait, then periodic_task()
  on every timer tick.

  In this TACLeBench version navigation_task, reporting_task and
  receive_gps_data_task are not functions: their bodies are sequences of
  calls inlined in periodic_task() (main.c) and main() (mainloop.c). They are
  timed from the entry of the first call to the exit of the last one.
*/

#include <arch/io.h>
#include "papabench_harness.h"

void altitude_control_task( void );
void climb_control_task( void );
void link_fbw_send( void );
void radio_control_task( void );
void stabilisation_task( void );

/* navigation_task: estimator_propagate_state, navigation_update,
   send_nav_values, course_run */
void estimator_propagate_state( void );
void course_run( void );
/* reporting_task: send_boot ... send_nav_ref */
void send_boot( void );
void send_nav_ref( void );
/* receive_gps_data_task: parse_gps_msg, send_gps_pos, send_radIR,
   send_takeOff */
void parse_gps_msg( void );
void send_takeOff( void );


const char papabench_prog_name[] = "autopilot";


const struct papabench_task papabench_tasks[] = {
  PAPABENCH_TASK( "altitude_control_task", altitude_control_task, altitude_control_task ),
  PAPABENCH_TASK( "climb_control_task", climb_control_task, climb_control_task ),
  PAPABENCH_TASK( "link_fbw_send", link_fbw_send, link_fbw_send ),
  PAPABENCH_TASK( "navigation_task", estimator_propagate_state, course_run ),
  PAPABENCH_TASK( "radio_control_task", radio_control_task, radio_control_task ),
  PAPABENCH_TASK( "receive_gps_data_task", parse_gps_msg, send_takeOff ),
  PAPABENCH_TASK( "reporting_task", send_boot, send_nav_ref ),
  PAPABENCH_TASK( "stabilisation_task", stabilisation_task, stabilisation_task ),
};


const unsigned int papabench_ntasks =
  sizeof( papabench_tasks ) / sizeof( papabench_tasks[ 0 ] );


/* mainloop.c: init_cpt = 30; while ( init_cpt ) if ( timer_periodic() ) ... */
const unsigned int papabench_startup_ticks = 30;


int papabench_check( void )
{
  return &TIFR != &papabench_sfr[ PAPABENCH_TIFR_ADDR ] ||
         TOV2 != PAPABENCH_TICK_BIT;
}
