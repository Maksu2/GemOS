#include "scheduler.h"

#include "gdt.h"
#include "idt.h"
#include "process.h"
#include "include/heap.h"
#include "memory/paging.h"
#include "../drivers/pit.h"
#include "../drivers/pic.h"
#include "../drivers/serial.h"

#include <string.h>

static task_t tasks[MAX_TASKS];
static int    current_task = 0;
static int    task_count   = 0;

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

int task_create_kernel(void (*entry)(void)) {
    if (task_count >= MAX_TASKS) {
        serial_print("[SCHED] task_create: max tasks reached\n");
        return -1;
    }

    /* Find a free slot (slot 0 is always task0 / kernel) */
    int slot = scheduler_find_free_slot();
    if (slot < 0) return -1;

    uint8_t *stack = (uint8_t *)kalloc(TASK_STACK_SIZE);
    if (!stack) {
        serial_print("[SCHED] task_create: out of memory\n");
        return -1;
    }

    /*
     * Build the initial interrupt frame on the new task's stack.
     *
     * The restore sequence in scheduler_irq0_stub is:
     *   pop %eax (DS) / mov %ax, seg... / popa / add $8 / iret
     *
     * So we lay out the frame identically to what pusha + the surrounding
     * stub would produce, from high address to low (each *--sp decrements):
     *
     *   highest: EFLAGS, CS, EIP, err_code, int_no,
     *            EAX, ECX, EDX, EBX, ESP_snap, EBP, ESI, EDI, DS  :lowest
     *
     * task->esp will point at DS (the lowest element = what ESP points to
     * after the stub saves the frame).
     *
     * pusha order: EAX first (highest addr), EDI last (lowest addr).
     */
    uint32_t *sp = (uint32_t *)(stack + TASK_STACK_SIZE);

    *--sp = 0x00000202;        /* EFLAGS: IF=1, reserved bit 1 */
    *--sp = GDT_KERNEL_CS;     /* CS: kernel code segment */
    *--sp = (uint32_t)entry;   /* EIP: task entry point */
    *--sp = 0;                 /* err_code */
    *--sp = 32;                /* int_no */
    /* pusha block (high -> low): EAX, ECX, EDX, EBX, ESP_snap, EBP, ESI, EDI */
    *--sp = 0;                 /* EAX */
    *--sp = 0;                 /* ECX */
    *--sp = 0;                 /* EDX */
    *--sp = 0;                 /* EBX */
    *--sp = 0;                 /* ESP snapshot (popa ignores this field) */
    *--sp = 0;                 /* EBP */
    *--sp = 0;                 /* ESI */
    *--sp = 0;                 /* EDI */
    *--sp = GDT_KERNEL_DS;     /* DS: kernel data segment */

    tasks[slot].id = (uint32_t)slot;
    tasks[slot].kind = TASK_KIND_KERNEL;
    tasks[slot].state = TASK_READY;
    tasks[slot].stack = stack;
    tasks[slot].kernel_stack_top = (uint32_t)(uintptr_t)(stack + TASK_STACK_SIZE);
    tasks[slot].esp = (uint32_t)(uintptr_t)sp;
    tasks[slot].ticks_remaining = TASK_QUANTUM;
    tasks[slot].process = NULL;
    task_count++;

    serial_print("[SCHED] Task created: id=");
    serial_print_dec(slot);
    serial_print(" esp=0x");
    serial_print_hex(tasks[slot].esp);
    serial_print("\n");

    return slot;
}

int task_create(void (*entry)(void)) { return task_create_kernel(entry); }

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

uint32_t scheduler_yield_now(uint32_t current_esp) {
    tasks[current_task].esp = current_esp;
    if (tasks[current_task].state == TASK_RUNNING) {
        tasks[current_task].state = TASK_READY;
        if (tasks[current_task].process != NULL) {
            tasks[current_task].process->state = PROC_READY;
        }
    }
    return scheduler_choose_next(current_esp);
}

uint32_t scheduler_switch_now(uint32_t current_esp) {
    int next;

    tasks[current_task].esp = current_esp;
    next = scheduler_find_next_runnable(current_task);
    if (next < 0) {
        next = 0;
    }

    if (next == current_task) {
        return current_esp;
    }

    return scheduler_resume_task(next, current_esp);
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

int scheduler_get_current_pid(void) {
    if (tasks[current_task].process == NULL) {
        return -1;
    }
    return (int)tasks[current_task].process->pid;
}

struct process *scheduler_get_current_process(void) {
    return tasks[current_task].process;
}

const task_t *scheduler_get_current_task(void) { return &tasks[current_task]; }

void scheduler_release_task(uint32_t task_id) {
    if (task_id == 0 || task_id >= MAX_TASKS) {
        return;
    }

    memset(&tasks[task_id], 0, sizeof(tasks[task_id]));
    if (task_count > 1) {
        task_count--;
    }
}

int scheduler_current_task_kind(void) {
    return (int)tasks[current_task].kind;
}
