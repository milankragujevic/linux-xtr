/**
 * @file init.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/kernel.h>
#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/errno.h>
 
extern int perfmon_init(struct oprofile_operations * ops);
extern void perfmon_exit(void);
extern void ia64_backtrace(struct pt_regs * const regs, unsigned int depth);

void __init oprofile_arch_init(struct oprofile_operations * ops)
{
#ifdef CONFIG_PERFMON
	/* perfmon_init() can fail, but we have no way to report it */
	perfmon_init(ops);
#endif
	ops->backtrace = ia64_backtrace;
}


void oprofile_arch_exit(void)
{
#ifdef CONFIG_PERFMON
	perfmon_exit();
#endif
}
