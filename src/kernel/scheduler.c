// scheduler.c
#include <meow/scheduler.h>
#include <meow/io.h> // for printf

// Two tasks for now: kernel idle task + one user task (toybox)
static task_t kernel_task;
static task_t user_task;

task_t* current_task = NULL;

extern uint8_t kernel_stack_top[];

void scheduler_init(void) {
    // Initialize kernel idle task
    kernel_task.rsp = 0; // will be set on first switch
    kernel_task.rip = 0;
    kernel_task.cr3 = 0; // kernel page tables
    kernel_task.stack_top = (uint64_t)kernel_stack_top;
    kernel_task.next = &user_task;

    // User task will be filled later by scheduler_add_task()
    user_task.next = &kernel_task;

    current_task = &kernel_task;

    printf("[scheduler] Initialized (round-robin)\n");
}

void scheduler_add_task(uint64_t entry, uint64_t user_stack_top) {
    user_task.rip = entry;
    user_task.stack_top = user_stack_top; // user stack (not kernel stack)
    user_task.cr3 = 0;                    // for now we use same page tables

    printf("[scheduler] Added user task: entry=0x%llx, stack=0x%llx\n", entry, user_stack_top);
}

void schedule(void) {
    // Very simple round-robin between kernel and user task
    if (current_task == &kernel_task) {
        current_task = &user_task;
    } else {
        current_task = &kernel_task;
    }
}

void scheduler_tick(void) {
    // Called from timer interrupt
    schedule();
}