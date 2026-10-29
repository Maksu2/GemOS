#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

struct process;

#define MAX_TASKS       16
#define TASK_STACK_SIZE (16 * 1024)  /* 16 KB per task */
#define TASK_QUANTUM    10           /* ticks per task, 10ms @ 1000 Hz */
#define TASK_GUI        0            /* task 0: the GUI loop in kernel_main */

/* Software interrupt a kernel task uses to give up the CPU. */
#define KERNEL_YIELD_VECTOR 0x81

typedef enum {
    TASK_UNUSED  = 0,
    TASK_READY   = 1,
    TASK_RUNNING = 2,
    TASK_BLOCKED = 3,
    TASK_ZOMBIE  = 4,
} task_state_t;

typedef enum {
    TASK_KIND_KERNEL = 0,
    TASK_KIND_USER   = 1,
} task_kind_t;

typedef struct {
    uint32_t      id;
    task_kind_t   kind;
    task_state_t  state;
    uint32_t      esp;             /* Saved ESP — points to DS field in saved frame */
    uint8_t      *stack;           /* Stack base (kalloc'd), NULL for task0 */
    uint32_t      kernel_stack_top;
    uint32_t      ticks_remaining; /* Per-task quantum countdown (decremented only when RUNNING) */
    uint64_t      wake_tick;       /* BLOCKED: wake at this tick, 0 = only scheduler_wake() */
    uint32_t      runs;            /* times the task was given the CPU */
    uint8_t       kill_pending;    /* killed inside a syscall: ends when it returns */
    struct process *process;
} task_t;

/* scheduler_kill_task() */
#define SCHEDULER_KILL_FAILED   0
#define SCHEDULER_KILLED        1  /* the task never runs again */
#define SCHEDULER_KILL_DEFERRED 2  /* it is inside a syscall and ends on its
                                      way back to Ring 3 */

/* Defined in context_switch.S */
extern void scheduler_irq0_stub(void);

void     scheduler_init(void);
int      task_create_user(struct process *process, uint32_t initial_esp);
#ifdef GEMOS_SELFTEST
/* A kernel task that starts in entry() on the given stack (self-test). */
int      task_create_kernel(void (*entry)(void), uintptr_t stack_top);
#endif

/* Called from assembly stub — returns new ESP to switch to */
uint32_t scheduler_tick(uint32_t current_esp);

/* Ask to give up the CPU when the current interrupt returns. */
void     scheduler_request_yield(void);
/* Kernel code (a kernel task or a syscall): give up the CPU now
 * (int 0x81). Returns when the task is scheduled again. */
void     scheduler_yield(void);
/* scheduler_yield(), but run task_id next if it is ready to run. */
void     scheduler_yield_to(uint32_t task_id);
/* Long kernel work (a file syscall) calls this between two steps, where no
 * shared state is half changed: it gives up the CPU when the scheduler
 * would have preempted the task in Ring 3 (its quantum is used up, or a
 * blocked task was woken). The GUI task keeps the CPU: it gives it up only
 * between two iterations of its loop. */
void     scheduler_preempt_point(void);
uint32_t scheduler_current_task(void);
/* Mark the current task blocked until scheduler_wake() or until the tick
 * count reaches wake_tick (0 = no timeout). The caller must then give up
 * the CPU: scheduler_yield(), or return from a syscall that runs again
 * when the task wakes (SYS_console_wait_event). */
void     scheduler_block_current(uint64_t wake_tick);
/* Make a blocked task runnable. Safe from IRQ handlers. */
void     scheduler_wake(uint32_t task_id);
/* Called when an exception, a syscall, a kernel yield or an IRQ that
 * interrupted Ring 3 returns (IRQ0 has its own stub, and an IRQ that
 * interrupted kernel code always returns to it): returns the ESP of the
 * task to resume, or 0 to resume the interrupted one. */
uint32_t scheduler_interrupt_exit(uint32_t current_esp);

void     scheduler_mark_current_zombie(int32_t exit_code);
void     scheduler_mark_current_fault(uint32_t vector, uint32_t error,
                                      uint32_t cr2);
/* SCHEDULER_KILLED, SCHEDULER_KILL_DEFERRED for a process stopped inside a
 * syscall (it may hold GemFS, or be half way through a change), or
 * SCHEDULER_KILL_FAILED for the current task. */
int      scheduler_kill_task(uint32_t task_id);
/* The current task was killed while it was inside a syscall: it should
 * finish the syscall as soon as it can, which then ends it. */
int      scheduler_kill_pending(void);
/* The task is a process stopped inside a syscall, not in Ring 3. */
int      scheduler_task_in_kernel(uint32_t task_id);
uint32_t scheduler_task_runs(uint32_t task_id);
int      scheduler_get_current_pid(void);
struct process *scheduler_get_current_process(void);
void     scheduler_release_task(uint32_t task_id);

#endif /* SCHEDULER_H */
