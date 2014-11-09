#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/list.h>
#include <linux/pid.h>

struct fg_pid_struct {
	struct pid *pid;
	struct list_head list;
};

LIST_HEAD(fg_pids_list);
static int fg_pid_nr = 0;
static struct pid *fg_pid = NULL;

extern void oom_adj_register_notify(struct notifier_block *nb);
extern void oom_adj_unregister_notify(struct notifier_block *nb);

static u32 debug_app_list = 0;
module_param(debug_app_list, uint, 0644);

static void check_list(int pid, int adj) {
	//go through the list and remove any pids with nonzero oom_adj, empty and system pids
	struct list_head *pos = NULL;
	struct list_head *tmp = NULL;
	struct fg_pid_struct *el = NULL;

	struct task_struct *task;

	if (debug_app_list > 0) printk(KERN_ERR "app_monitor: cleaning list");
	list_for_each_safe(pos, tmp, &fg_pids_list) {
		el = list_entry(pos, struct fg_pid_struct, list);
		task = get_pid_task(el->pid, PIDTYPE_PID);
		if (!task || task->signal->oom_adj != 0) {
			if (debug_app_list > 1) printk(KERN_ERR "app_monitor: removing %s", task->comm);
			put_pid(el->pid);
			list_del(pos);
			kfree(el);
		} else {
			if (debug_app_list > 1) printk(KERN_ERR "app_monitor: leaving %s", task->comm);
		};
	}
	if (debug_app_list > 0) printk(KERN_ERR "app_monitor: cleaning list done");

};

/* random notes:
 * - fs/proc/
 *   - array.c
 *   - stat.c
 * - kernel/posix-cpu-timers.c: thread_group_cputime
 * - include/linux/pid.h
 */
cputime_t utime_start, stime_start, cutime_start, cstime_start;
cputime_t utime_end, stime_end, cutime_end, cstime_end;

static int oom_adj_changed(struct notifier_block *self, unsigned long oom_adj, void *t)
{
	struct task_struct *task = (struct task_struct *)t;
	struct task_struct *oldtask;
	struct fg_pid_struct *el;
	unsigned long flags;
	struct signal_struct *sig;
	struct task_cputime task_time;

	//TODO lock

	if ((oom_adj != 0 && task->pid != fg_pid_nr) || (oom_adj == 0 && task->pid == fg_pid_nr))
		return NOTIFY_DONE;

	if (task->cred->euid < 10000)
		return NOTIFY_DONE;

	//app_changed = (task->pid == fg_pid);
	if (oom_adj == 0) {
		//add it to the end of the list
		el = kmalloc(sizeof(struct fg_pid_struct), GFP_KERNEL);
		el->pid = get_task_pid(task, PIDTYPE_PID);
		list_add_tail(&(el->list), &fg_pids_list);
	}

	check_list(task->pid, oom_adj);
	if (list_empty(&fg_pids_list)) {
		fg_pid_nr = 0;
		fg_pid = NULL;
		printk(KERN_ERR "app_monitor: foreground app list empty");
		//TODO trigger boost?
		return NOTIFY_DONE;
	}

	el = list_first_entry(&fg_pids_list, struct fg_pid_struct, list);
	task = get_pid_task(el->pid, PIDTYPE_PID);

	if (!task) {
		fg_pid_nr = 0;
		fg_pid = NULL;
		printk(KERN_ERR "app_monitor: foreground app unknown");
		//TODO trigger boost?
	} else if (task->pid != fg_pid_nr) {
		printk(KERN_ERR "app_monitor: foreground app changed to %s [pid %d, tgid %d], nice %d, prio %d, is group leader: %d", task->comm, (int)task->pid, (int)task->tgid, task_nice(task), task_prio(task), thread_group_leader(task));
		printk(KERN_ERR "app_monitor: foreground app thread group leader: %s [pid %d, tgid %d], nice %d, prio %d", task->group_leader->comm, (int)task->group_leader->pid, (int)task->group_leader->tgid, task_nice(task->group_leader), task_prio(task->group_leader));
		
		//TODO trigger boost
		if (!fg_pid) {
			printk(KERN_ERR "app_monitor: old task not set - can't calculate cputime used\n");
			goto notfound;
		}
		printk(KERN_ERR "app_monitor: fg_pid\n");
		oldtask = get_pid_task(fg_pid, PIDTYPE_PID);//TGID?
		if (!oldtask) {
			printk(KERN_ERR "app_monitor: old task not found - can't calculate cputime used\n");
			goto notfound;
		}

		printk(KERN_ERR "app_monitor: oldtask\n");
		sig = oldtask->signal;
		cutime_end = sig->cutime;
		cstime_end = sig->cstime;
		utime_end = oldtask->utime;
		stime_end = oldtask->stime;
		printk(KERN_ERR "app_monitor: oldtask %lu %lu %lu %lu\n", utime_end, stime_end, cutime_end, cstime_end);
		printk(KERN_ERR "app_monitor: cputime used utime: %lu, stime: %lu, cutime: %lu, cstime: %lu", utime_end-utime_start, stime_end-stime_start, cutime_end-cutime_start, cstime_end-cstime_start);
		put_task_struct(oldtask);
notfound:
		sig = task->signal;
		cutime_start = sig->cutime;
		cstime_start = sig->cstime;
		utime_start = task->utime;
		stime_start = task->stime;
		printk(KERN_ERR "app_monitor: sighand %lu %lu %lu %lu\n", utime_start, stime_start, cutime_start, cstime_start);

		fg_pid_nr = task->pid;
		fg_pid = el->pid;
		put_task_struct(task);
	}
	return NOTIFY_DONE;
}

static struct notifier_block nb = {
	.notifier_call = &oom_adj_changed,
};

static int __init app_monitor_init(void)
{
	oom_adj_register_notify(&nb);
	printk(KERN_INFO "Zen foreground app monitor driver registered\n");
	utime_start = stime_start = cutime_start = cstime_start =  utime_end = stime_end = cutime_end = cstime_end = cputime_zero;
	return 0;
}
 
static void __exit app_monitor_exit(void)
{
	oom_adj_unregister_notify(&nb);
	printk(KERN_INFO "Zen foreground app monitor driver unregistered\n");
}
 
module_init(app_monitor_init);
module_exit(app_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marcin Kaluza <marcin.kaluza@trioptimum.com>");
MODULE_DESCRIPTION("Test module");
