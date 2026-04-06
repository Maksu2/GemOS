#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

struct process;

#define MAX_TASKS       16
#define TASK_STACK_SIZE (16 * 1024)  /* 16 KB per task */
#define TASK_QUANTUM    10           /* ticks per task, 10ms @ 1000 Hz */
/*
 * NOTE (QEMU/Apple Silicon): x86 is software-emulated on ARM M4 — no VT-x.
 * TASK_QUANTUM is approximate; actual quantum duration depends on emulation
 * speed. At heavy load the effective quantum may be longer than 10ms.
 */

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
    struct process *process;
} task_t;

/* Defined in context_switch.S */
extern void scheduler_irq0_stub(void);

void     scheduler_init(void);
int      task_create_kernel(void (*entry)(void));
int      task_create(void (*entry)(void));
int      task_create_user(struct process *process, uint32_t initial_esp);

/* Called from assembly stub — returns new ESP to switch to */
uint32_t scheduler_tick(uint32_t current_esp);
uint32_t scheduler_yield_now(uint32_t current_esp);
uint32_t scheduler_switch_now(uint32_t current_esp);

void     scheduler_mark_current_zombie(int32_t exit_code);
void     scheduler_mark_current_fault(uint32_t vector, uint32_t error,
                                      uint32_t cr2);
int      scheduler_kill_task(uint32_t task_id);
int      scheduler_get_current_pid(void);
struct process *scheduler_get_current_process(void);
const task_t *scheduler_get_current_task(void);
void     scheduler_release_task(uint32_t task_id);
int      scheduler_current_task_kind(void);

#endif /* SCHEDULER_H */
