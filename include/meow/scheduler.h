// scheduler.h
#pragma once

#include <stdint.h>

typedef struct task {
    uint64_t rsp;           // saved kernel stack pointer for this task
    uint64_t rip;           // instruction pointer (entry point)
    uint64_t cr3;           // page table base (for future multi-process)
    uint64_t stack_top;     // top of kernel stack for this task
    struct task *next;
} task_t;

// Global current task
extern task_t *current_task;

// Initialize scheduler
void scheduler_init(void);

// Add a new task (for now we only support one user task)
void scheduler_add_task(uint64_t entry, uint64_t user_stack_top);

// Called from timer interrupt (or syscall yield)
void schedule(void);

// Simple round-robin scheduler
void scheduler_tick(void);
