/*
 * This is where we statically allocate and initialize the initial
 * task.
 *
 * Copyright (C) 1999, 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/init_task.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>

static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS(init_signals);
static struct sighand_struct init_sighand = INIT_SIGHAND(init_sighand);
struct mm_struct init_mm = INIT_MM(init_mm);

/*
 * Initial task structure.
 *
 * We need to make sure that this is properly aligned due to the way process stacks are
 * handled. This is done by having a special ".data.init_task" section...
 */
#define init_thread_info	init_thread_union.s.thread_info

union init_thread {
	struct {
		struct task_struct task;
		struct thread_info thread_info;
	} s;
	unsigned long stack[KERNEL_STACK_SIZE/sizeof (unsigned long)];
} init_thread_union __attribute__((section(".data.init_task"))) = {{
	.task =		INIT_TASK(init_thread_union.s.task),
	.thread_info =	INIT_THREAD_INFO(init_thread_union.s.thread_info)
}};

asm (".global init_task; init_task = init_thread_union");
