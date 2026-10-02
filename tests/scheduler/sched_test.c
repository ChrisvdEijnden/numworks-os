#include <stdio.h>
#include "scheduler.h"
static int runs[4], wfis = 0;
static void t_idle(void){ runs[0]++; if (!scheduler_ready_above(TASK_PRIO_IDLE)) { wfis++; scheduler_tick(); /* a tick wakes us */ } }
static void t_input(void){ runs[1]++; scheduler_sleep(1); }
static void t_display(void){ runs[2]++; scheduler_sleep(1); }
static void t_app(void){ runs[3]++; scheduler_sleep(1); }
int main(void){
  scheduler_init();
  scheduler_add_task("idle",t_idle,TASK_PRIO_IDLE); scheduler_add_task("input",t_input,TASK_PRIO_NORMAL);
  scheduler_add_task("display",t_display,TASK_PRIO_NORMAL); scheduler_add_task("app",t_app,TASK_PRIO_NORMAL);
  for (int i = 0; i < 4000; i++) scheduler_run_next();
  printf("1000 ticks simulated: idle/WFI=%d input=%d display=%d app=%d\n", wfis, runs[1], runs[2], runs[3]);
  /* each tick: the three tasks run once, then idle finds nothing ready and waits */
  int ok = wfis == 1000 && runs[1] == 1000 && runs[2] == 1000 && runs[3] == 1000;
  printf("%s\n", ok ? "ALL PASSED" : "FAIL: expected 1000 of each");
  return !ok;
}
