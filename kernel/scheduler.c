/*
 * Round-robin scheduler.
 *
 * Concurrency rule: code running in Ring 0 is never preempted. A task
 * switch happens only when
 *   - IRQ0 interrupted Ring 3 and the task's quantum ran out (or a blocked
 *     task was woken up), or
 *   - the running task gives up the CPU itself: a syscall that exits, yields
 *     or blocks, a fault that kills a process, or a kernel task calling
 *     scheduler_yield().
 * Kernel code therefore runs to its next yield point without interference
 * from other tasks; only interrupt handlers can interleave with it, so state
 * shared with an IRQ handler needs irq_save()/irq_restore().
 *
 * Task 0 is the GUI loop in kernel_main. The idle task runs only when no
 * other task is runnable and is the only place that executes hlt.
 */
#include "scheduler.h"

#include "fpu.h"
#include "gdt.h"
#include "idt.h"
#include "include/irq.h"
#include "isr.h"
#include "process.h"
#include "memory/paging.h"
#include "../drivers/pit.h"
#include "../drivers/pic.h"
#include "../drivers/serial.h"

#include <string.h>

/* The idle task sits after the round-robin slots and is never scanned. */
#define IDLE_TASK        MAX_TASKS
#define IDLE_STACK_SIZE  4096

extern void isr129(void);

static task_t tasks[MAX_TASKS + 1];
static fpu_state_t fpu_states[MAX_TASKS + 1];
static int    current_task = 0;
static int    task_count   = 0;
static int    yield_requested = 0;
static int    need_resched = 0;   /* a task was woken since the last switch */
static uint8_t idle_stack[IDLE_STACK_SIZE] __attribute__((aligned(16)));

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

static int scheduler_any_ready(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_READY) {
            return 1;
        }
    }
    return 0;
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

    /* the FPU holds the state of the task being left */
    fpu_save(&fpu_states[current_task]);
    fpu_restore(&fpu_states[next]);

    current_task = next;
    tasks[current_task].state = TASK_RUNNING;
    tasks[current_task].ticks_remaining = TASK_QUANTUM;
    scheduler_apply_task_context(&tasks[current_task]);

    if (tasks[current_task].esp == 0) {
        return fallback_esp;
    }

    return tasks[current_task].esp;
}

/* Frame for a kernel task that has never run, laid out like the frame the
 * interrupt stubs save (registers_t): the first switch to it "returns" into
 * entry() in Ring 0 with interrupts enabled. */
static uint32_t scheduler_build_kernel_frame(uint8_t *stack, size_t size,
                                             void (*entry)(void)) {
    uint32_t *sp = (uint32_t *)(uintptr_t)(stack + size);

    *--sp = 0;                          /* return address of entry() */
    *--sp = 0x00000202U;                /* EFLAGS: IF */
    *--sp = GDT_KERNEL_CS;
    *--sp = (uint32_t)(uintptr_t)entry;
    *--sp = 0;                          /* err_code */
    *--sp = KERNEL_YIELD_VECTOR;        /* int_no */
    for (int i = 0; i < 8; i++) {
        *--sp = 0;                      /* pusha block */
    }
    *--sp = GDT_KERNEL_DS;
    return (uint32_t)(uintptr_t)sp;
}

static void scheduler_idle_main(void) {
    for (;;) {
        __asm__ volatile("cli" : : : "memory");
        if (!scheduler_any_ready()) {
            /* sti takes effect only after the next instruction, so an IRQ
             * that wakes a task after the check still ends the hlt. */
            __asm__ volatile("sti; hlt" : : : "memory");
        } else {
            __asm__ volatile("sti" : : : "memory");
        }
        scheduler_yield();
    }
}

static void scheduler_yield_handler(registers_t *regs) {
    (void)regs;
    scheduler_request_yield();
}

void scheduler_init(void) {
    memset(tasks, 0, sizeof(tasks));

    /* Task 0 = the current execution context (the GUI loop in kernel_main).
     * Its ESP is saved the first time it gives up the CPU. */
    tasks[0].id = 0;
    tasks[0].kind = TASK_KIND_KERNEL;
    tasks[0].state = TASK_RUNNING;
    tasks[0].stack = NULL;  /* uses the boot-time stack */
    tasks[0].kernel_stack_top = scheduler_read_esp();
    tasks[0].ticks_remaining = TASK_QUANTUM;
    tasks[0].process = NULL;
    current_task = 0;
    task_count   = 1;

    tasks[IDLE_TASK].id = IDLE_TASK;
    tasks[IDLE_TASK].kind = TASK_KIND_KERNEL;
    tasks[IDLE_TASK].state = TASK_READY;
    tasks[IDLE_TASK].stack = idle_stack;
    tasks[IDLE_TASK].kernel_stack_top =
        (uint32_t)(uintptr_t)(idle_stack + sizeof(idle_stack));
    tasks[IDLE_TASK].esp = scheduler_build_kernel_frame(
        idle_stack, sizeof(idle_stack), scheduler_idle_main);
    fpu_init_state(&fpu_states[IDLE_TASK]);

    gdt_set_kernel_stack(tasks[0].kernel_stack_top);

    /* Replace the IDT gate for INT 32 (IRQ0) with our scheduler stub.
     * Flags: 0x8E = Present | Ring-0 | 32-bit Interrupt Gate. */
    idt_set_gate(32, (uint32_t)(uintptr_t)scheduler_irq0_stub, GDT_KERNEL_CS,
                 0x8E);
    /* Kernel tasks give up the CPU with int 0x81. Ring-0-only gate: the
     * same instruction from Ring 3 raises #GP. */
    idt_set_gate(KERNEL_YIELD_VECTOR, (uint32_t)(uintptr_t)isr129,
                 GDT_KERNEL_CS, 0x8E);
    register_interrupt_handler(KERNEL_YIELD_VECTOR, scheduler_yield_handler);

    serial_print("[SCHED] Initialized — INT32 -> scheduler_irq0_stub\n");
}

int task_create_user(struct process *process, uint32_t initial_esp) {
    int slot;
    uint32_t flags;

    if (process == NULL || initial_esp == 0) {
        return -1;
    }

    flags = irq_save();
    if (task_count >= MAX_TASKS) {
        irq_restore(flags);
        return -1;
    }

    slot = scheduler_find_free_slot();
    if (slot < 0) {
        irq_restore(flags);
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
    fpu_init_state(&fpu_states[slot]);
    task_count++;
    irq_restore(flags);

    return slot;
}

/* Pick the next task after the current one has been saved: round-robin over
 * the runnable tasks, the idle task if there are none. */
static uint32_t scheduler_choose_next(uint32_t fallback_esp) {
    int next = scheduler_find_next_runnable(current_task);

    need_resched = 0;
    if (next < 0) {
        next = IDLE_TASK;
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

static void scheduler_wake_locked(uint32_t task_id) {
    task_t *task = &tasks[task_id];

    if (task->state != TASK_BLOCKED) {
        return;
    }
    task->state = TASK_READY;
    task->wake_tick = 0;
    if (task->process != NULL) {
        task->process->state = PROC_READY;
    }
    need_resched = 1;
}

static void scheduler_wake_expired(uint64_t now) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_BLOCKED && tasks[i].wake_tick != 0 &&
            now >= tasks[i].wake_tick) {
            scheduler_wake_locked((uint32_t)i);
        }
    }
}

/*
 * scheduler_tick — called from scheduler_irq0_stub with interrupts disabled.
 *
 * Receives the ESP of the interrupted task (pointing at DS in the saved
 * frame), performs timer bookkeeping, decides whether to switch tasks, and
 * returns the ESP to resume (same task or next task).
 */
uint32_t scheduler_tick(uint32_t current_esp) {
    const registers_t *frame = (const registers_t *)(uintptr_t)current_esp;
    task_t *task = &tasks[current_task];

    /* 1. Timer bookkeeping (global_ticks, EVENT_TIMER_TICK) */
    pit_tick();

    /* 2. Acknowledge IRQ0 at the PIC so further IRQs can be signalled */
    pic_send_eoi(0);

    /* 3. Wake tasks whose sleep ran out */
    scheduler_wake_expired(timer_get_ticks());

    /* 4. Consume one tick from the current task's quantum */
    if (task->ticks_remaining > 0) {
        task->ticks_remaining--;
    }

    /* 5. Never preempt Ring 0: kernel code gives up the CPU itself. */
    if ((frame->cs & 0x3U) != 0x3U) {
        return current_esp;
    }
    if (task->ticks_remaining > 0 && !need_resched) {
        return current_esp;
    }

    task->esp = current_esp;
    return scheduler_choose_next(current_esp);
}

void scheduler_request_yield(void) { yield_requested = 1; }

void scheduler_yield(void) {
    __asm__ volatile("int %0" : : "i"(KERNEL_YIELD_VECTOR) : "memory");
}

/*
 * scheduler_interrupt_exit — called by isr_handler before it returns to the
 * interrupted code. Keeps running the current task unless it cannot continue
 * (it exited, faulted, blocked or was killed) or asked to give up the CPU.
 * Returns the ESP of the task to resume, or 0 to resume the interrupted one.
 */
uint32_t scheduler_interrupt_exit(uint32_t current_esp) {
    task_t *task = &tasks[current_task];

    if (task->state == TASK_RUNNING && !yield_requested) {
        return 0;
    }
    yield_requested = 0;

    task->esp = current_esp;
    return scheduler_choose_next(current_esp);
}

void scheduler_block_current(uint64_t wake_tick) {
    uint32_t flags = irq_save();
    task_t *task = &tasks[current_task];

    if (current_task != IDLE_TASK && task->state == TASK_RUNNING) {
        task->state = TASK_BLOCKED;
        task->wake_tick = wake_tick;
        if (task->process != NULL) {
            task->process->state = PROC_BLOCKED;
        }
    }
    irq_restore(flags);
}

void scheduler_wake(uint32_t task_id) {
    uint32_t flags;

    if (task_id >= MAX_TASKS) {
        return;
    }
    flags = irq_save();
    scheduler_wake_locked(task_id);
    irq_restore(flags);
}

void scheduler_mark_current_zombie(int32_t exit_code) {
    if (tasks[current_task].process != NULL) {
        tasks[current_task].process->state = PROC_ZOMBIE;
        tasks[current_task].process->exit_code = exit_code;
    }
    tasks[current_task].state = TASK_ZOMBIE;
    scheduler_wake(TASK_GUI);  /* the GUI task reaps processes */
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
    scheduler_wake(TASK_GUI);
}

int scheduler_kill_task(uint32_t task_id) {
    uint32_t flags;
    int result = 1;

    if (task_id == 0 || task_id >= MAX_TASKS) {
        return 0;
    }

    flags = irq_save();
    if (task_id == (uint32_t)current_task) {
        result = 0;
    } else if (tasks[task_id].state != TASK_UNUSED &&
               tasks[task_id].state != TASK_ZOMBIE) {
        tasks[task_id].state = TASK_ZOMBIE;
        tasks[task_id].wake_tick = 0;
    }
    irq_restore(flags);
    return result;
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
    uint32_t flags;

    if (task_id == 0 || task_id >= MAX_TASKS) {
        return;
    }

    flags = irq_save();
    memset(&tasks[task_id], 0, sizeof(tasks[task_id]));
    if (task_count > 1) {
        task_count--;
    }
    irq_restore(flags);
}
