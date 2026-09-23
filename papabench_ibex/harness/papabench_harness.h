/*
  Interface between the PapaBench side (fbw_glue.c / autopilot_glue.c, which
  include PapaBench headers) and the Ibex side (harness.c, runtime.c, which
  include Secure-Ibex headers). It must stay free of any fixed-width typedef,
  because the two sides define uint32_t differently (unsigned long vs
  unsigned int).
*/

#ifndef PAPABENCH_HARNESS_H
#define PAPABENCH_HARNESS_H

typedef void ( *papabench_fn_t )( void );

/*
  A task is timed from the entry of 'first' to the exit of 'last'. For a task
  that is one function, first == last. For a task whose body is a sequence of
  calls inlined in the scheduler (e.g. navigation_task), first/last are the
  first and last call of the sequence.

  The Makefile parses the PAPABENCH_TASK( ... ) lines of the glue file to
  decide which functions get -finstrument-functions hooks: keep one entry per
  line, in exactly this form.
*/
struct papabench_task {
  const char *name;
  papabench_fn_t first;
  papabench_fn_t last;
};

#define PAPABENCH_TASK( name, first, last ) { name, first, last }

/* Provided by the glue file of the program being built */
extern const char papabench_prog_name[];
extern const struct papabench_task papabench_tasks[];
extern const unsigned int papabench_ntasks;
/* Ticks consumed by the upstream init before the scheduler loop starts */
extern const unsigned int papabench_startup_ticks;
/* 0 if the build-time TIFR/TOV2 assumptions hold for this device */
int papabench_check( void );

/* Upstream main(), renamed at compile time (-Dmain=papabench_upstream_main) */
int papabench_upstream_main( void );

/* Instrumented empty function (calib.c), used to measure hook overhead */
void papabench_calib( void );

/* Tick flag, defined in runtime.c */
extern volatile unsigned char papabench_tick_pending;

/* Scheduler tick period in cycles (harness.c, PAPABENCH_TICK_CYCLES) */
extern const unsigned int papabench_tick_cycles;

/*
  Peripheral models (harness/periph.c + harness/<prog>_periph.c, see
  periph.h). The program's model lists the upstream ISRs it calls; each call
  goes through papabench_isr_run(), which times it under that index.
*/
struct papabench_isr {
  const char *name;
};

extern const struct papabench_isr papabench_isrs[];
extern const unsigned int papabench_nisrs;

/* harness.c: runs fn (an upstream __vector_N) and records its duration */
void papabench_isr_run( unsigned int id, papabench_fn_t fn );

/* Program model: set up the real peripherals, before the upstream main() */
void papabench_periph_init( void );
/* Program model: synchronisation point, called with interrupts disabled */
void papabench_periph_poll( void );

/* Interrupt dispatch, called by the harness wrappers of the Ibex IRQs */
void papabench_irq_timer_a( void );
void papabench_irq_timer_b( void );
void papabench_irq_timer_c( void );
void papabench_irq_uart( void );
void papabench_irq_gpio( void );

#endif /* PAPABENCH_HARNESS_H */
