#include <linux/earlysuspend.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <linux/notifier.h>
#include <linux/list.h>
#include <linux/pid.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/kernel_stat.h>
#include <asm/cputime.h>
#include <linux/tick.h>

struct fg_pid_struct {
	struct pid *pid;
	struct list_head list;
};

LIST_HEAD(fg_pids_list);
static int fg_pid_nr = 0;
static struct pid *fg_pid = NULL;

extern void oom_adj_register_notify(struct notifier_block *nb);
extern void oom_adj_unregister_notify(struct notifier_block *nb);

static unsigned int freq = 0;
static u32 debug_app_list = 0;
module_param(debug_app_list, uint, 0644);

static unsigned int delay = 10;
module_param(delay, uint, 0644);

static bool io_is_busy = true;

struct cpufreq_interactive_cpuinfo {
	struct timer_list cpu_timer;
	struct timer_list cpu_slack_timer;
	spinlock_t load_lock; /* protects the next 4 fields */
	u64 time_in_idle;
	u64 time_in_idle_timestamp;
	u64 cputime_speedadj;
	u64 cputime_speedadj_timestamp;
	struct cpufreq_policy *policy;
	struct cpufreq_frequency_table *freq_table;
	spinlock_t target_freq_lock; /*protects target freq */
	unsigned int target_freq;
	unsigned int floor_freq;
	unsigned int max_freq;
	unsigned int timer_rate;
	int timer_slack_val;
	unsigned int min_sample_time;
	u64 floor_validate_time;
	u64 hispeed_validate_time;
	struct rw_semaphore enable_sem;
	int governor_enabled;
	int prev_load;
	bool limits_changed;
	unsigned int active_time, idle_time;
};

static DEFINE_PER_CPU(struct cpufreq_interactive_cpuinfo, cpuinfo);

static struct task_cputime prev_app_time = {
	.utime = 0,
	.stime = 0,
	.sum_exec_runtime = 0
};

static bool suspend = false;

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
 *
 * void set_user_nice(struct task_struct *p, long nice)
 *
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
	cputime_t ut, st;

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

		set_user_nice(oldtask, 0);
		printk(KERN_ERR "app_monitor: oldtask\n");
		sig = oldtask->signal;
		cutime_end = sig->cutime;
		cstime_end = sig->cstime;
		utime_end = oldtask->utime;
		stime_end = oldtask->stime;
		printk(KERN_ERR "app_monitor: oldtask %lu %lu %lu %lu\n", utime_end, stime_end, cutime_end, cstime_end);
		printk(KERN_ERR "app_monitor: cputime used utime: %lu, stime: %lu, cutime: %lu, cstime: %lu", utime_end-utime_start, stime_end-stime_start, cutime_end-cutime_start, cstime_end-cstime_start);
		thread_group_times(oldtask, &ut, &st);
		printk(KERN_ERR "app_monitor: oldtask thread_group_times: user:%lu system:%lu \n", ut, st);
		put_task_struct(oldtask);
notfound:
		sig = task->signal;
		cutime_start = sig->cutime;
		cstime_start = sig->cstime;
		utime_start = task->utime;
		stime_start = task->stime;
		printk(KERN_ERR "app_monitor: sighand %lu %lu %lu %lu\n", utime_start, stime_start, cutime_start, cstime_start);

		thread_group_cputime(task, &prev_app_time);

		fg_pid_nr = task->pid;
		fg_pid = el->pid;
		set_user_nice(task, -10);	//TODO loop over threads
		put_task_struct(task);
	}
	return NOTIFY_DONE;
}

static struct notifier_block nb = {
	.notifier_call = &oom_adj_changed,
};

static inline cputime64_t get_cpu_idle_time_jiffy(unsigned int cpu,
						  cputime64_t *wall)
{
	cputime64_t idle_time;
	cputime64_t cur_wall_time;
	cputime64_t busy_time;

	cur_wall_time = jiffies64_to_cputime64(get_jiffies_64());
	busy_time = cputime64_add(kstat_cpu(cpu).cpustat.user,
			kstat_cpu(cpu).cpustat.system);

	busy_time = cputime64_add(busy_time, kstat_cpu(cpu).cpustat.irq);
	busy_time = cputime64_add(busy_time, kstat_cpu(cpu).cpustat.softirq);
	busy_time = cputime64_add(busy_time, kstat_cpu(cpu).cpustat.steal);
	busy_time = cputime64_add(busy_time, kstat_cpu(cpu).cpustat.nice);

	idle_time = cputime64_sub(cur_wall_time, busy_time);
	if (wall)
		*wall = (cputime64_t)jiffies_to_usecs(cur_wall_time);

	return (cputime64_t)jiffies_to_usecs(idle_time);
}

static inline cputime64_t get_cpu_idle_time(unsigned int cpu,
					    cputime64_t *wall)
{
	u64 idle_time = get_cpu_idle_time_us(cpu, wall);

	if (idle_time == -1ULL)
		idle_time = get_cpu_idle_time_jiffy(cpu, wall);
	else if (!io_is_busy)
		idle_time += get_cpu_iowait_time_us(cpu, wall);

	return idle_time;
}

static u64 update_load(void)
{
	struct cpufreq_interactive_cpuinfo *pcpu;
	u64 now;
	u64 now_idle;
	unsigned int delta_idle;
	unsigned int delta_time;
	u64 active_time;
	int cpu;
	for(cpu=0; cpu <= 1; cpu++) {
		pcpu = &per_cpu(cpuinfo, cpu);
		now_idle = get_cpu_idle_time(cpu, &now);
		delta_idle = (unsigned int)(now_idle - pcpu->time_in_idle);
		delta_time = (unsigned int)(now - pcpu->time_in_idle_timestamp);

		if (delta_time <= delta_idle)
			active_time = 0;
		else
			active_time = delta_time - delta_idle;

		//pcpu->cputime_speedadj += active_time * pcpu->policy->cur;
		pcpu->active_time = active_time;
		pcpu->idle_time = delta_idle;
		pcpu->time_in_idle = now_idle;
		pcpu->time_in_idle_timestamp = now;
	}
	return now;
}

extern void thread_group_cputime(struct task_struct *tsk, struct task_cputime *times);

int proc_delay_fn(char *buf, char **start, off_t offset, int len, int *eof, void *data)
{
	unsigned long j0, j1; /* jiffies */
	int cpu;
	struct cpufreq_interactive_cpuinfo *pcpu;
	struct task_cputime app_time;
	struct task_struct * task;
	unsigned long long temp_rtime;

	j0 = jiffies;
	j1 = j0 + delay;

	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout (delay);
	update_load();

	j1 = jiffies;
	len = sprintf(buf, "%9lu %9lu", j0, j1);
	for(cpu=0; cpu <= 1; cpu++) {
		pcpu = &per_cpu(cpuinfo, cpu);
		len += sprintf(buf+len, " cpu%d: active %6u idle %6u", cpu, pcpu->active_time, pcpu->idle_time);
	}
	len += sprintf(buf+len, ", suspend: %d, freq: %4u", suspend, freq);
	if (fg_pid != NULL) {
		task = get_pid_task(fg_pid, PIDTYPE_PID);
		thread_group_cputime(task, &app_time);
		temp_rtime=app_time.sum_exec_runtime-prev_app_time.sum_exec_runtime;
		do_div(temp_rtime, 1000);
		len += sprintf(buf+len, ", app: gid %5d, utime %3lu, stime %3lu, rtime %6llu\n", task->tgid, app_time.utime-prev_app_time.utime, app_time.stime-prev_app_time.stime, temp_rtime);
		prev_app_time = app_time;

		put_task_struct(task);
	} else
		len += sprintf(buf+len, "%45s","\n");
	*start = buf;
	return len;
}

static int cpufreq_callback(struct notifier_block *nfb,
		unsigned long event, void *data)
{
	struct cpufreq_freqs *freqs = data;

	if (event != CPUFREQ_POSTCHANGE || freqs->cpu != 0)
		return 0;

	freq = freqs->new/1000;
	return 0;
}
static struct notifier_block cpufreq_notifier_block = {
	.notifier_call = cpufreq_callback,
};

static void app_monitor_suspend(struct early_suspend *handler)
{
	suspend = true;
}

static void app_monitor_resume(struct early_suspend *handler)
{
	suspend = false;
}

static struct early_suspend app_monitor_early_suspend = {
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB,
	.suspend = app_monitor_suspend,
	.resume = app_monitor_resume,
};

static int __init app_monitor_init(void)
{
	struct task_struct *task;
	struct fg_pid_struct *el;

	oom_adj_register_notify(&nb);
	create_proc_read_entry("app_monitor", 0, NULL, proc_delay_fn, NULL);
	register_early_suspend(&app_monitor_early_suspend);
	cpufreq_register_notifier(&cpufreq_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
	printk(KERN_INFO "Zen foreground app monitor driver registered\n");
	for_each_process(task) {
		//printk(KERN_ERR "app_monitor: checking %s, %d, %d", task->comm, task->cred->euid, task->signal->oom_adj);
		if (task->cred->euid >= 10000 && task->signal->oom_adj == 0) {
			el = kmalloc(sizeof(struct fg_pid_struct), GFP_KERNEL);
			el->pid = get_task_pid(task, PIDTYPE_PID);
			list_add_tail(&(el->list), &fg_pids_list);
		}
	}
	if (!list_empty(&fg_pids_list)) {
		el = list_first_entry(&fg_pids_list, struct fg_pid_struct, list);
		task = get_pid_task(el->pid, PIDTYPE_PID);
		if (task) {
			thread_group_cputime(task, &prev_app_time);
			fg_pid_nr = task->pid;
			fg_pid = el->pid;
			set_user_nice(task, -10);	//TODO loop over threads
			put_task_struct(task);
		}
	}

	debug_app_list = 2;
	check_list(0, 0);
	debug_app_list = 0;

	utime_start = stime_start = cutime_start = cstime_start =  utime_end = stime_end = cutime_end = cstime_end = cputime_zero;
	return 0;
}
 
static void __exit app_monitor_exit(void)
{
	cpufreq_unregister_notifier(&cpufreq_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
	unregister_early_suspend(&app_monitor_early_suspend);
	remove_proc_entry("app_monitor", NULL);
	oom_adj_unregister_notify(&nb);
	printk(KERN_INFO "Zen foreground app monitor driver unregistered\n");
}
 
module_init(app_monitor_init);
module_exit(app_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marcin Kaluza <marcin.kaluza@trioptimum.com>");
MODULE_DESCRIPTION("Test module");
