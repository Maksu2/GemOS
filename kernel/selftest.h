#ifndef SELFTEST_H
#define SELFTEST_H

/* Kernel self-test, built only into the self-test image (make selftest,
 * -DGEMOS_SELFTEST). See kernel/selftest.c. */

struct process;

/* Start the self-test task (kernel_main, before the GUI loop). */
void selftest_start(void);

/* Called by the reaper for every process that ends. */
void selftest_process_exited(const struct process *process);

#endif /* SELFTEST_H */
