/* -*- linux-c -*-
 *
 *	$Id: sysrq.c,v 1.2 1997/05/31 18:33:11 mj Exp $
 *
 *	Linux Magic System Request Key Hacks
 *
 *	(c) 1997 Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *	based on ideas by Pavel Machek <pavel@atrey.karlin.mff.cuni.cz>
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/reboot.h>
#include <linux/sysrq.h>
#include <linux/kbd_kern.h>
#include <asm/ptrace.h>
#include <asm/smp_lock.h>

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
#endif

extern void wakeup_bdflush(int);
extern void reset_vc(unsigned int);
extern int console_loglevel;
extern struct vfsmount *vfsmntlist;

#ifdef __sparc__
extern void halt_now(void);
#endif

/* Send a signal to all user processes */

static void send_sig_all(int sig, int even_init)
{
	struct task_struct *p;

	for_each_task(p) {
		if (p->pid && p->mm != &init_mm) {	    /* Not swapper nor kernel thread */
			if (p->pid == 1 && even_init)	    /* Ugly hack to kill init */
				p->pid = 0x8000;
			force_sig(sig, p);
		}
	}
}

/*
 * This function is called by the keyboard handler when SysRq is pressed
 * and any other keycode arrives.
 */

void handle_sysrq(int key, struct pt_regs *pt_regs,
		  struct kbd_struct *kbd, struct tty_struct *tty)
{
	int orig_log_level = console_loglevel;

	console_loglevel = 7;
	printk(KERN_INFO "SysRq: ");
	switch (key) {
	case 19:					    /* R -- Reset raw mode */
		kbd->kbdmode = VC_XLATE;
		printk("Keyboard mode set to XLATE\n");
		break;
	case 30:					    /* A -- SAK */
		printk("SAK\n");
		do_SAK(tty);
		reset_vc(fg_console);
		break;
	case 48:					    /* B -- boot immediately */
		printk("Resetting\n");
		machine_restart(NULL);
		break;
#ifdef __sparc__
	case 35:					    /* H -- halt immediately */
		printk("Halting\n");
		halt_now();
		break;
#endif
#ifdef CONFIG_APM
	case 24:					    /* O -- power off */
		printk("Power off\n");
		apm_set_power_state(APM_STATE_OFF);
		break;
#endif
	case 31:					    /* S -- emergency sync */
		printk("Emergency Sync\n");
		emergency_sync_scheduled = EMERG_SYNC;
		wakeup_bdflush(0);
		break;
	case 22:					    /* U -- emergency remount R/O */
		printk("Emergency Remount R/O\n");
		emergency_sync_scheduled = EMERG_REMOUNT;
		wakeup_bdflush(0);
		break;
	case 25:					    /* P -- show PC */
		printk("Show Regs\n");
		if (pt_regs)
			show_regs(pt_regs);
		break;
	case 20:					    /* T -- show task info */
		printk("Show State\n");
		show_state();
		break;
	case 50:					    /* M -- show memory info */
		printk("Show Memory\n");
		show_mem();
		break;
	case 2 ... 11:					    /* 0-9 -- set console logging level */
		key -= 2;
		if (key == 10)
			key = 0;
		orig_log_level = key;
		printk("Log level set to %d\n", key);
		break;
	case 18:					    /* E -- terminate all user processes */
		printk("Terminate All Tasks\n");
		send_sig_all(SIGTERM, 0);
		orig_log_level = 8;			    /* We probably have killed syslogd */
		break;
	case 37:					    /* K -- kill all user processes */
		printk("Kill All Tasks\n");
		send_sig_all(SIGKILL, 0);
		orig_log_level = 8;
		break;
	case 38:					    /* L -- kill all processes including init */
		printk("Kill ALL Tasks (even init)\n");
		send_sig_all(SIGKILL, 1);
		orig_log_level = 8;
		break;
	default:					    /* Unknown: help */
		printk("unRaw sAk Boot "
#ifdef __sparc__
		       "Halt "
#endif
#ifdef CONFIG_APM
		       "Off "
#endif
		       "Sync Unmount showPc showTasks showMem loglevel0-8 tErm Kill killalL\n");
	}

	console_loglevel = orig_log_level;
}

/* Aux routines for the syncer */

static void all_files_read_only(void)	    /* Kill write permissions of all files */
{
	struct file *file;

	for (file = inuse_filps; file; file = file->f_next)
		if (file->f_inode && file->f_count && S_ISREG(file->f_inode->i_mode))
			file->f_mode &= ~2;
}

static int is_local_disk(kdev_t dev)	    /* Guess if the device is a local hard drive */
{
	unsigned int major = MAJOR(dev);

	switch (major) {
	case IDE0_MAJOR:
	case IDE1_MAJOR:
	case IDE2_MAJOR:
	case IDE3_MAJOR:
	case SCSI_DISK_MAJOR:
		return 1;
	default:
		return 0;
	}
}

static void go_sync(kdev_t dev, int remount_flag)
{
	printk(KERN_INFO "%sing device %04x ... ",
	       remount_flag ? "Remount" : "Sync",
	       dev);

	if (remount_flag) {				    /* Remount R/O */
		struct super_block *sb = get_super(dev);
		struct vfsmount *vfsmnt;
		int ret, flags;

		if (!sb) {
			printk("Superblock not found\n");
			return;
		}
		if (sb->s_flags & MS_RDONLY) {
			printk("R/O\n");
			return;
		}
		quota_off(dev, -1);
		fsync_dev(dev);
		flags = MS_RDONLY;
		if (sb->s_op && sb->s_op->remount_fs) {
			ret = sb->s_op->remount_fs(sb, &flags, NULL);
			if (ret)
				printk("error %d\n", ret);
			else {
				sb->s_flags = (sb->s_flags & ~MS_RMT_MASK) | (flags & MS_RMT_MASK);
				if ((vfsmnt = lookup_vfsmnt(sb->s_dev)))
					vfsmnt->mnt_flags = sb->s_flags;
				printk("OK\n");
			}
		} else
			printk("nothing to do\n");
	} else {
		fsync_dev(dev);				    /* Sync only */
		printk("OK\n");
	}
}

/*
 * Emergency Sync or Unmount. We cannot do it directly, so we set a special
 * flag and wake up the bdflush kernel thread which immediately calls this function.
 * We process all mounted hard drives first to recover from crashed experimental
 * block devices and malfunctional network filesystems.
 */

int emergency_sync_scheduled;

void do_emergency_sync(void)
{
	struct vfsmount *mnt;
	int remount_flag;

	lock_kernel();
	remount_flag = (emergency_sync_scheduled == EMERG_REMOUNT);
	emergency_sync_scheduled = 0;

	if (remount_flag)
		all_files_read_only();

	for (mnt = vfsmntlist; mnt; mnt = mnt->mnt_next)
		if (is_local_disk(mnt->mnt_dev))
			go_sync(mnt->mnt_dev, remount_flag);

	for (mnt = vfsmntlist; mnt; mnt = mnt->mnt_next)
		if (!is_local_disk(mnt->mnt_dev) && MAJOR(mnt->mnt_dev))
			go_sync(mnt->mnt_dev, remount_flag);

	unlock_kernel();
	printk(KERN_INFO "Done.\n");
}
