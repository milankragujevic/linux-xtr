/*
 *	linux/kernel/softirq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *
 * Fixed a disable_bh()/enable_bh() race (was causing a console lockup)
 * due bh_mask_count not atomic handling. Copyright (C) 1998  Andrea Arcangeli
 *
 * Rewritten. Old one was good in 2.2, but in 2.3 it was immoral. --ANK (990903)
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/tqueue.h>

/*
   - No shared variables, all the data are CPU local.
   - If a softirq needs serialization, let it serialize itself
     by its own spinlocks.
   - Even if softirq is serialized, only local cpu is marked for
     execution. Hence, we get something sort of weak cpu binding.
     Though it is still not clear, will it result in better locality
     or will not.
   - These softirqs are not masked by global cli() and start_bh_atomic()
     (by clear reasons). Hence, old parts of code still using global locks
     MUST NOT use softirqs, but insert interfacing routines acquiring
     global locks. F.e. look at BHs implementation.

   Examples:
   - NET RX softirq. It is multithreaded and does not require
     any global serialization.
   - NET TX softirq. It kicks software netdevice queues, hence
     it is logically serialized per device, but this serialization
     is invisible to common code.
   - Tasklets: serialized wrt itself.
   - Bottom halves: globally serialized, grr...
 */

/* No separate irq_stat for s390, it is part of PSA */
#if !defined(CONFIG_ARCH_S390)
irq_cpustat_t irq_stat[NR_CPUS];
#endif	/* CONFIG_ARCH_S390 */

static struct softirq_action softirq_vec[32] __cacheline_aligned;

asmlinkage void do_softirq()
{
	int cpu = smp_processor_id();
	__u32 pending;

	if (in_interrupt())
		return;

	local_irq_disable();

	pending = softirq_pending(cpu);

	if (pending) {
		struct softirq_action *h;

		local_bh_disable();
restart:
		/* Reset the pending bitmask before enabling irqs */
		softirq_pending(cpu) = 0;

		local_irq_enable();

		h = softirq_vec;

		do {
			if (pending & 1)
				h->action(h);
			h++;
			pending >>= 1;
		} while (pending);

		local_irq_disable();

		pending = softirq_pending(cpu);
		if (pending)
			goto restart;
		__local_bh_enable();
	}

	local_irq_enable();
}


void open_softirq(int nr, void (*action)(struct softirq_action*), void *data)
{
	softirq_vec[nr].data = data;
	softirq_vec[nr].action = action;
}


/* Tasklets */

struct tasklet_head tasklet_vec[NR_CPUS] __cacheline_aligned;

void tasklet_schedule(struct tasklet_struct *t)
{
	unsigned long flags;
	int cpu;

	cpu = smp_processor_id();
	local_irq_save(flags);
	/*
	 * If nobody is running it then add it to this CPU's
	 * tasklet queue.
	 */
	if (!test_and_set_bit(TASKLET_STATE_SCHED, &t->state) &&
						tasklet_trylock(t)) {
		t->next = tasklet_vec[cpu].list;
		tasklet_vec[cpu].list = t;
		__cpu_raise_softirq(cpu, TASKLET_SOFTIRQ);
		tasklet_unlock(t);
	}
	local_irq_restore(flags);
}

void tasklet_hi_schedule(struct tasklet_struct *t)
{
	unsigned long flags;
	int cpu;

	cpu = smp_processor_id();
	local_irq_save(flags);

	if (!test_and_set_bit(TASKLET_STATE_SCHED, &t->state) &&
						tasklet_trylock(t)) {
		t->next = tasklet_hi_vec[cpu].list;
		tasklet_hi_vec[cpu].list = t;
		__cpu_raise_softirq(cpu, HI_SOFTIRQ);
		tasklet_unlock(t);
	}
	local_irq_restore(flags);
}

static void tasklet_action(struct softirq_action *a)
{
	int cpu = smp_processor_id();
	struct tasklet_struct *list;

	local_irq_disable();
	list = tasklet_vec[cpu].list;
	tasklet_vec[cpu].list = NULL;

	while (list) {
		struct tasklet_struct *t = list;

		list = list->next;

		/*
		 * A tasklet is only added to the queue while it's
		 * locked, so no other CPU can have this tasklet
		 * pending:
		 */
		if (!tasklet_trylock(t))
			BUG();
repeat:
		if (!test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
			BUG();
		if (!atomic_read(&t->count)) {
			local_irq_enable();
			t->func(t->data);
			local_irq_disable();
			/*
			 * One more run if the tasklet got reactivated:
			 */
			if (test_bit(TASKLET_STATE_SCHED, &t->state))
				goto repeat;
		}
		tasklet_unlock(t);
		if (test_bit(TASKLET_STATE_SCHED, &t->state))
			tasklet_schedule(t);
	}
	local_irq_enable();
}



struct tasklet_head tasklet_hi_vec[NR_CPUS] __cacheline_aligned;

static void tasklet_hi_action(struct softirq_action *a)
{
	int cpu = smp_processor_id();
	struct tasklet_struct *list;

	local_irq_disable();
	list = tasklet_hi_vec[cpu].list;
	tasklet_hi_vec[cpu].list = NULL;

	while (list) {
		struct tasklet_struct *t = list;

		list = list->next;

		if (!tasklet_trylock(t))
			BUG();
repeat:
		if (!test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
			BUG();
		if (!atomic_read(&t->count)) {
			local_irq_enable();
			t->func(t->data);
			local_irq_disable();
			if (test_bit(TASKLET_STATE_SCHED, &t->state))
				goto repeat;
		}
		tasklet_unlock(t);
		if (test_bit(TASKLET_STATE_SCHED, &t->state))
			tasklet_hi_schedule(t);
	}
	local_irq_enable();
}


void tasklet_init(struct tasklet_struct *t,
		  void (*func)(unsigned long), unsigned long data)
{
	t->next = NULL;
	t->state = 0;
	atomic_set(&t->count, 0);
	t->func = func;
	t->data = data;
}

void tasklet_kill(struct tasklet_struct *t)
{
	if (in_interrupt())
		printk("Attempt to kill tasklet from interrupt\n");

	while (test_and_set_bit(TASKLET_STATE_SCHED, &t->state)) {
		current->state = TASK_RUNNING;
		do {
			current->policy |= SCHED_YIELD;
			schedule();
		} while (test_bit(TASKLET_STATE_SCHED, &t->state));
	}
	tasklet_unlock_wait(t);
	clear_bit(TASKLET_STATE_SCHED, &t->state);
}



/* Old style BHs */

static void (*bh_base[32])(void);
struct tasklet_struct bh_task_vec[32];

/* BHs are serialized by spinlock global_bh_lock.

   It is still possible to make synchronize_bh() as
   spin_unlock_wait(&global_bh_lock). This operation is not used
   by kernel now, so that this lock is not made private only
   due to wait_on_irq().

   It can be removed only after auditing all the BHs.
 */
spinlock_t global_bh_lock = SPIN_LOCK_UNLOCKED;

static void bh_action(unsigned long nr)
{
	int cpu = smp_processor_id();

	if (!spin_trylock(&global_bh_lock))
		goto resched;

	if (!hardirq_trylock(cpu))
		goto resched_unlock;

	if (bh_base[nr])
		bh_base[nr]();

	hardirq_endlock(cpu);
	spin_unlock(&global_bh_lock);
	return;

resched_unlock:
	spin_unlock(&global_bh_lock);
resched:
	mark_bh(nr);
}

void init_bh(int nr, void (*routine)(void))
{
	bh_base[nr] = routine;
	mb();
}

void remove_bh(int nr)
{
	tasklet_kill(bh_task_vec+nr);
	bh_base[nr] = NULL;
}

void __init softirq_init()
{
	int i;

	for (i=0; i<32; i++)
		tasklet_init(bh_task_vec+i, bh_action, i);

	open_softirq(TASKLET_SOFTIRQ, tasklet_action, NULL);
	open_softirq(HI_SOFTIRQ, tasklet_hi_action, NULL);
}

void __run_task_queue(task_queue *list)
{
	struct list_head head, *next;
	unsigned long flags;

	spin_lock_irqsave(&tqueue_lock, flags);
	list_add(&head, list);
	list_del_init(list);
	spin_unlock_irqrestore(&tqueue_lock, flags);

	next = head.next;
	while (next != &head) {
		void (*f) (void *);
		struct tq_struct *p;
		void *data;

		p = list_entry(next, struct tq_struct, list);
		next = next->next;
		f = p->routine;
		data = p->data;
		wmb();
		p->sync = 0;
		if (f)
			f(data);
	}
}
