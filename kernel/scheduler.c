#include "scheduler.h"

#include "gdt.h"
#include "idt.h"
#include "process.h"
#include "memory/paging.h"
#include "../drivers/pit.h"
#include "../drivers/pic.h"
#include "../drivers/serial.h"

#include <string.h>

static task_t tasks[MAX_TASKS];
static int    current_task = 0;
static int    task_count   = 0;
static int    yield_requested = 0;

static uint32_t scheduler_read_esp(void) {
    uint32_t esp;
    __asm__ volatile("mov %%esp, %0" : "=r"(esp));
    return esp;
}

static int scheduler_find_free_slot(void) {
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) {
            return i;
        }
    }
    return -1;
}

static int scheduler_find_next_runnable(int start) {
    int next = start;

    for (int i = 0; i < MAX_TASKS; i++) {
        next = (next + 1) % MAX_TASKS;
        if (tasks[next].state == TASK_READY ||
            tasks[next].state == TASK_RUNNING) {
            return next;
        }
    }

    return -1;
}

static void scheduler_apply_task_context(const task_t *task) {
    if (task == NULL) {
        return;
    }

    gdt_set_kernel_stack(task->kernel_stack_top);
    if (task->kind == TASK_KIND_USER && task->process != NULL) {
        task->process->state = PROC_RUNNING;
        paging_switch_directory(task->process->as.page_directory);
    } else {
        paging_switch_directory(paging_get_directory());
    }
}

static uint32_t scheduler_resume_task(int next, uint32_t fallback_esp) {
    if (next < 0) {
        return fallback_esp;
    }

    current_task = next;
    tasks[current_task].state = TASK_RUNNING;
    tasks[current_task].ticks_remaining = TASK_QUANTUM;
    scheduler_apply_task_context(&tasks[current_task]);

    if (tasks[current_task].esp == 0) {
        return fallback_esp;
    }

    return tasks[current_task].esp;
}

void scheduler_init(void) {
    memset(tasks, 0, sizeof(tasks));

    /* Task 0 = kernel / current execution context (the main event loop).
     * Its ESP will be captured on the first IRQ0 tick — initialise to 0
     * as a sentinel; scheduler_tick guards against returning 0. */
    tasks[0].id = 0;
    tasks[0].kind = TASK_KIND_KERNEL;
    tasks[0].state = TASK_RUNNING;
    tasks[0].stack = NULL;  /* uses the boot-time stack */
    tasks[0].kernel_stack_top = scheduler_read_esp();
    tasks[0].ticks_remaining = TASK_QUANTUM;
    tasks[0].process = NULL;
    current_task = 0;
    task_count   = 1;

    gdt_set_kernel_stack(tasks[0].kernel_stack_top);

    /* Replace the IDT gate for INT 32 (IRQ0) with our scheduler stub.
     * Flags: 0x8E = Present | Ring-0 | 32-bit Interrupt Gate. */
    idt_set_gate(32, (uint32_t)(uintptr_t)scheduler_irq0_stub, GDT_KERNEL_CS,
                 0x8E);

    serial_print("[SCHED] Initialized — INT32 -> scheduler_irq0_stub\n");
}

int task_create_user(struct process *process, uint32_t initial_esp) {
    int slot;

    if (process == NULL || initial_esp == 0) {
        return -1;
    }
    if (task_count >= MAX_TASKS) {
        return -1;
    }

    slot = scheduler_find_free_slot();
    if (slot < 0) {
        return -1;
    }

    memset(&tasks[slot], 0, sizeof(tasks[slot]));
    tasks[slot].id = (uint32_t)slot;
    tasks[slot].kind = TASK_KIND_USER;
    tasks[slot].state = TASK_READY;
    tasks[slot].stack = process->kernel_stack_base;
    tasks[slot].kernel_stack_top = (uint32_t)process->kernel_stack_top;
    tasks[slot].esp = initial_esp;
    tasks[slot].ticks_remaining = TASK_QUANTUM;
    tasks[slot].process = process;
    task_count++;

    return slot;
}

static uint32_t scheduler_choose_next(uint32_t fallback_esp) {
    int next = scheduler_find_next_runnable(current_task);

    if (next < 0) {
        if (tasks[0].state != TASK_UNUSED && tasks[0].state != TASK_ZOMBIE) {
            next = 0;
        } else {
            return fallback_esp;
        }
    }

    if (next == current_task) {
        tasks[current_task].state = TASK_RUNNING;
        if (tasks[current_task].process != NULL) {
            tasks[current_task].process->state = PROC_RUNNING;
        }
        tasks[current_task].ticks_remaining = TASK_QUANTUM;
        scheduler_apply_task_context(&tasks[current_task]);
        return fallback_esp;
    }

    if (tasks[current_task].state == TASK_RUNNING) {
        tasks[current_task].state = TASK_READY;
        if (tasks[current_task].process != NULL) {
            tasks[current_task].process->state = PROC_READY;
        }
    }

    return scheduler_resume_task(next, fallback_esp);
}

/*
 * scheduler_tick — called from scheduler_irq0_stub with interrupts disabled.
 *
 * Receives the ESP of the interrupted task (pointing at DS in the saved
 * frame), performs timer bookkeeping, decides whether to switch tasks, and
 * returns the ESP to resume (same task or next task).
 */
uint32_t scheduler_tick(uint32_t current_esp) {
    /* 1. Timer bookkeeping (global_ticks, EVENT_TIMER_TICK) */
    pit_tick();

    /* 2. Acknowledge IRQ0 at the PIC so further IRQs can be signalled */
    pic_send_eoi(0);

    /* 3. Save current task's stack pointer */
    tasks[current_task].esp = current_esp;

    /* 4. Consume one tick from the current task's quantum */
    if (tasks[current_task].ticks_remaining > 0)
        tasks[current_task].ticks_remaining--;

    if (tasks[current_task].ticks_remaining > 0 &&
        tasks[current_task].state == TASK_RUNNING) {
        /* Still within quantum — stay on the same task */
        return current_esp;
    }

    if (tasks[current_task].state == TASK_RUNNING) {
        tasks[current_task].state = TASK_READY;
        if (tasks[current_task].process != NULL) {
            tasks[current_task].process->state = PROC_READY;
        }
    }
    return scheduler_choose_next(current_esp);
}

void scheduler_request_yield(void) { yield_requested = 1; }

/*
 * scheduler_interrupt_exit — called by isr_handler before it returns to the
 * interrupted code. Keeps running the current task unless it cannot continue
 * (it exited, faulted or was killed) or asked to give up the CPU. Returns the
 * ESP of the task to resume, or 0 to resume the interrupted one.
 */
uint32_t scheduler_interrupt_exit(uint32_t current_esp) {
    task_t *task = &tasks[current_task];

    if (task->state == TASK_RUNNING && !yield_requested) {
        return 0;
    }
    yield_requested = 0;

    task->esp = current_esp;
    if (task->state == TASK_RUNNING) {
        task->state = TASK_READY;
        if (task->process != NULL) {
            task->process->state = PROC_READY;
        }
    }
    return scheduler_choose_next(current_esp);
}

void scheduler_mark_current_zombie(int32_t exit_code) {
    if (tasks[current_task].process != NULL) {
        tasks[current_task].process->state = PROC_ZOMBIE;
        tasks[current_task].process->exit_code = exit_code;
    }
    tasks[current_task].state = TASK_ZOMBIE;
}

void scheduler_mark_current_fault(uint32_t vector, uint32_t error,
                                  uint32_t cr2) {
    if (tasks[current_task].process != NULL) {
        tasks[current_task].process->state = PROC_FAULTED;
        tasks[current_task].process->fault_vector = vector;
        tasks[current_task].process->fault_error = error;
        tasks[current_task].process->fault_cr2 = cr2;
        tasks[current_task].process->exit_code = -1;
    }
    tasks[current_task].state = TASK_ZOMBIE;
}

int scheduler_kill_task(uint32_t task_id) {
    if (task_id == 0 || task_id >= MAX_TASKS) {
        return 0;
    }
    if (task_id == (uint32_t)current_task) {
        return 0;
    }
    if (tasks[task_id].state == TASK_UNUSED || tasks[task_id].state == TASK_ZOMBIE) {
        return 1;
    }

    tasks[task_id].state = TASK_ZOMBIE;
    return 1;
}

int scheduler_get_current_pid(void) {
    if (tasks[current_task].process == NULL) {
        return -1;
    }
    return (int)tasks[current_task].process->pid;
}

struct process *scheduler_get_current_process(void) {
    return tasks[current_task].process;
}

void scheduler_release_task(uint32_t task_id) {
    if (task_id == 0 || task_id >= MAX_TASKS) {
        return;
    }

    memset(&tasks[task_id], 0, sizeof(tasks[task_id]));
    if (task_count > 1) {
        task_count--;
    }
}
