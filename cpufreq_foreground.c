/*
 *  drivers/cpufreq/cpufreq_foreground.c
 *
 *  Copyright (C)  2001 Russell King
 *            (C)  2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *                      Jun Nakajima <jun.nakajima@intel.com>
 *            (C)  2009 Alexander Clouter <alex@digriz.org.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/jiffies.h>
#include <linux/kernel_stat.h>
#include <linux/mutex.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/earlysuspend.h>
#include <linux/input.h>
#include <linux/slab.h>

/*
 * dbs is used in this file as a shortform for demandbased switching
 * It helps to keep variable names smaller, simpler
 */

#define DEF_FREQUENCY_UP_THRESHOLD		(80)
#define DEF_DOWN_DIFFERENTIAL		(20)

/*
 * The polling frequency of this governor depends on the capability of
 * the processor. Default polling frequency is 1000 times the transition
 * latency of the processor. The governor will work on any processor with
 * transition latency <= 10mS, using appropriate sampling
 * rate.
 * For CPUs with transition latency > 10mS (mostly drivers with CPUFREQ_ETERNAL)
 * this governor will not work.
 * All times here are in uS.
 */
#define MIN_SAMPLING_RATE_RATIO			(2)

static unsigned int min_sampling_rate;

#define LATENCY_MULTIPLIER			(1000)
#define MIN_LATENCY_MULTIPLIER			(100)
#define MICRO_FREQUENCY_MIN_SAMPLE_RATE		(10000)
#define MICRO_FREQUENCY_UP_THRESHOLD		(90)
#define MICRO_FREQUENCY_DOWN_DIFFERENTIAL		(10)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(10)
#define DEF_SAMPLING_UP_FACTOR		(6)
#define MAX_SAMPLING_UP_FACTOR		(20)
#define TRANSITION_LATENCY_LIMIT		(10 * 1000 * 1000)

static void do_dbs_timer(struct work_struct *work);

struct cpu_dbs_info_s {
	cputime64_t prev_cpu_idle;
	cputime64_t prev_cpu_wall;
	cputime64_t prev_cpu_nice;
	struct cpufreq_policy *cur_policy;
	struct cpufreq_frequency_table *freq_table;
	unsigned int freq_lo;
	struct delayed_work work;
	unsigned int down_skip;
	unsigned int requested_freq;
	unsigned int sampling_up_counter;
	unsigned int standby_counter;
	unsigned int down_threshold;
	int cpu;
	unsigned int enable:1;
	/*
	 * percpu mutex that serializes governor limit change with
	 * do_dbs_timer invocation. We do not want do_dbs_timer to run
	 * when user is changing the governor or limits.
	 */
	struct mutex timer_mutex;
};
static DEFINE_PER_CPU(struct cpu_dbs_info_s, cs_cpu_dbs_info);

static unsigned int dbs_enable;	/* number of CPUs using this policy */

static bool suspend = false;
module_param(suspend, bool, 0644);
static bool standby  = false;
module_param(standby, bool, 0644);

/* input boost */

u64 last_input_time;
#define MIN_INPUT_INTERVAL (50 * USEC_PER_MSEC)

/* input boost end */

/*
 * dbs_mutex protects dbs_enable in governor start/stop.
 */
static DEFINE_MUTEX(dbs_mutex);

static struct dbs_tuners {
	unsigned int sampling_rate;
	unsigned int _sampling_rate;
	unsigned int standby_sampling_rate;
	unsigned int suspend_sampling_rate;
	unsigned int suspend_sampling_up_factor;
	unsigned int standby_sampling_up_factor;
	unsigned int standby_delay_factor;
	unsigned int sampling_down_factor;
	unsigned int up_threshold;
	unsigned int down_differential;
	unsigned int ignore_nice;
	unsigned int freq_step;

	unsigned int input_boost_freq;
	unsigned int input_boost_ms;
	unsigned int suspend_max_freq;

} dbs_tuners_ins = {
	.up_threshold = DEF_FREQUENCY_UP_THRESHOLD,
	.down_differential = DEF_DOWN_DIFFERENTIAL,
	.suspend_sampling_up_factor = DEF_SAMPLING_UP_FACTOR,
	.standby_sampling_up_factor = 2,
	.sampling_down_factor = DEF_SAMPLING_DOWN_FACTOR,

	.ignore_nice = 1,
	.freq_step = 10,

	.input_boost_freq = 0,
	.input_boost_ms = 100,
	.suspend_max_freq = 0,
};

static unsigned int delay;
module_param(delay, uint, 0644);

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

static inline cputime64_t get_cpu_idle_time(unsigned int cpu, cputime64_t *wall)
{
	u64 idle_time = get_cpu_idle_time_us(cpu, NULL);

	if (idle_time == -1ULL)
		return get_cpu_idle_time_jiffy(cpu, wall);
	else
		//TODO add io_is_busy?
		idle_time += get_cpu_iowait_time_us(cpu, wall);

	return idle_time;
}

static inline void recalculate_down_threshold(struct cpu_dbs_info_s *this_dbs_info)
{
	unsigned int temp = (dbs_tuners_ins.up_threshold - dbs_tuners_ins.down_differential) * this_dbs_info->freq_lo / this_dbs_info->cur_policy->cur;
	if (temp < 10 || temp > (dbs_tuners_ins.up_threshold - dbs_tuners_ins.down_differential))
		temp = (dbs_tuners_ins.up_threshold - dbs_tuners_ins.down_differential)/2;
	this_dbs_info->down_threshold = temp;
}

/* keep track of frequency transitions */
static int
dbs_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
		     void *data)
{
	unsigned int idx;
	struct cpufreq_freqs *freq = data;
	struct cpu_dbs_info_s *this_dbs_info = &per_cpu(cs_cpu_dbs_info,
							freq->cpu);

	struct cpufreq_policy *policy;

	if (!this_dbs_info->enable)
		return 0;

	policy = this_dbs_info->cur_policy;

	/*
	 * we only care if our internally tracked freq moves outside
	 * the 'valid' ranges of freqency available to us otherwise
	 * we do not change it
	*/
	if (this_dbs_info->requested_freq > policy->max
			|| this_dbs_info->requested_freq < policy->min)
		this_dbs_info->requested_freq = freq->new;
	//TODO recalculate freq dependend values: freq_lo, down_threshold, ...?
	if (freq->new > policy->min) {
		cpufreq_frequency_table_target(policy, this_dbs_info->freq_table, freq->new - 1, CPUFREQ_RELATION_H, &idx);
		this_dbs_info->freq_lo = this_dbs_info->freq_table[idx].frequency;
		recalculate_down_threshold(this_dbs_info);
	} else 
		 this_dbs_info->freq_lo = policy->min;

	return 0;
}

static struct notifier_block dbs_cpufreq_notifier_block = {
	.notifier_call = dbs_cpufreq_notifier
};

/************************** sysfs interface ************************/
static ssize_t show_sampling_rate_min(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", min_sampling_rate);
}

define_one_global_ro(sampling_rate_min);

/* cpufreq_foreground Governor Tunables */
#define show_one(file_name, object)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)		\
{									\
	return sprintf(buf, "%u\n", dbs_tuners_ins.object);		\
}
show_one(sampling_rate, sampling_rate);
show_one(suspend_sampling_rate, suspend_sampling_rate);
show_one(standby_sampling_rate, standby_sampling_rate);
show_one(suspend_sampling_up_factor, suspend_sampling_up_factor);
show_one(standby_sampling_up_factor, standby_sampling_up_factor);
show_one(sampling_down_factor, sampling_down_factor);
show_one(up_threshold, up_threshold);
show_one(down_differential, down_differential);
show_one(ignore_nice_load, ignore_nice);
show_one(freq_step, freq_step);

show_one(input_boost_freq, input_boost_freq);
show_one(input_boost_ms, input_boost_ms);

show_one(suspend_max_freq, suspend_max_freq);

static ssize_t store_sampling_down_factor(struct kobject *a,
					  struct attribute *b,
					  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_DOWN_FACTOR || input < 1)
		return -EINVAL;

	dbs_tuners_ins.sampling_down_factor = input;
	return count;
}

static ssize_t store_suspend_max_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	//TODO verify
	dbs_tuners_ins.suspend_max_freq = input;

	return count;
}

static ssize_t store_input_boost_freq(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	//TODO verify
	dbs_tuners_ins.input_boost_freq = input;

	return count;
}

static ssize_t store_input_boost_ms(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	//TODO verify
	dbs_tuners_ins.input_boost_ms = input;

	return count;
}

static ssize_t store_suspend_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.suspend_sampling_rate = max(input, min_sampling_rate);
	if (suspend)
		delay = usecs_to_jiffies(dbs_tuners_ins.suspend_sampling_rate);

	return count;
}

static ssize_t store_standby_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.standby_sampling_rate = max(input, min_sampling_rate);
	if (standby)
		delay = usecs_to_jiffies(dbs_tuners_ins.standby_sampling_rate);

	return count;
}

static ssize_t store_sampling_rate(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	dbs_tuners_ins.sampling_rate = max(input, min_sampling_rate);
	dbs_tuners_ins._sampling_rate = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);
	if (!standby && !suspend)
		delay = dbs_tuners_ins._sampling_rate;

	return count;
}

static ssize_t store_standby_sampling_up_factor(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_UP_FACTOR || input < 1)
		return -EINVAL;

	dbs_tuners_ins.standby_sampling_up_factor = input;
	return count;
}

static ssize_t store_suspend_sampling_up_factor(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > MAX_SAMPLING_UP_FACTOR || input < 1)
		return -EINVAL;

	dbs_tuners_ins.suspend_sampling_up_factor = input;
	return count;
}

static ssize_t store_up_threshold(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	//struct cpu_dbs_info_s *this_dbs_info;
	unsigned int cpu;

	ret = sscanf(buf, "%u", &input);

	if (ret != 1 || input > 100 ||
			input <= dbs_tuners_ins.down_differential)
		return -EINVAL;

	dbs_tuners_ins.up_threshold = input;
	for_each_online_cpu(cpu) {
		//this_dbs_info = &per_cpu(cs_cpu_dbs_info, cpu);
		//recalculate_down_threshold(this_dbs_info);
		recalculate_down_threshold(&per_cpu(cs_cpu_dbs_info, cpu));
	}
	return count;
}

static ssize_t store_down_differential(struct kobject *a, struct attribute *b,
				    const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	unsigned int cpu;

	ret = sscanf(buf, "%u", &input);

	//setting it lower that 10 will probably cause jitter anyway...
	if (ret != 1 || input < 10 || input > 100 ||
			input >= dbs_tuners_ins.up_threshold)
		return -EINVAL;

	dbs_tuners_ins.down_differential = input;
	for_each_online_cpu(cpu) {
		//this_dbs_info = &per_cpu(cs_cpu_dbs_info, cpu);
		//recalculate_down_threshold(this_dbs_info);
		recalculate_down_threshold(&per_cpu(cs_cpu_dbs_info, cpu));
	}
	return count;
}

static ssize_t store_ignore_nice_load(struct kobject *a, struct attribute *b,
				      const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	unsigned int j;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	if (input > 1)
		input = 1;

	if (input == dbs_tuners_ins.ignore_nice) /* nothing to do */
		return count;

	dbs_tuners_ins.ignore_nice = input;

	/* we need to re-evaluate prev_cpu_idle */
	for_each_online_cpu(j) {
		struct cpu_dbs_info_s *dbs_info;
		dbs_info = &per_cpu(cs_cpu_dbs_info, j);
		dbs_info->prev_cpu_idle = get_cpu_idle_time(j,
						&dbs_info->prev_cpu_wall);
		if (dbs_tuners_ins.ignore_nice)
			dbs_info->prev_cpu_nice = kstat_cpu(j).cpustat.nice;
	}
	return count;
}

static ssize_t store_freq_step(struct kobject *a, struct attribute *b,
			       const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1)
		return -EINVAL;

	if (input > 100)
		input = 100;

	/* no need to test here if freq_step is zero as the user might actually
	 * want this, they would be crazy though :) */
	dbs_tuners_ins.freq_step = input;
	return count;
}

define_one_global_rw(sampling_rate);
define_one_global_rw(suspend_sampling_rate);
define_one_global_rw(standby_sampling_rate);
define_one_global_rw(suspend_sampling_up_factor);
define_one_global_rw(standby_sampling_up_factor);
define_one_global_rw(sampling_down_factor);
define_one_global_rw(up_threshold);
define_one_global_rw(down_differential);
define_one_global_rw(ignore_nice_load);
define_one_global_rw(freq_step);

define_one_global_rw(suspend_max_freq);
define_one_global_rw(input_boost_freq);
define_one_global_rw(input_boost_ms);

static struct attribute *dbs_attributes[] = {
	&sampling_rate_min.attr,
	&sampling_rate.attr,
	&sampling_down_factor.attr,
	&standby_sampling_rate.attr,
	&standby_sampling_up_factor.attr,
	&suspend_sampling_rate.attr,
	&suspend_sampling_up_factor.attr,
	&up_threshold.attr,
	&down_differential.attr,
	&ignore_nice_load.attr,
	&freq_step.attr,
	&input_boost_freq.attr,
	&input_boost_ms.attr,
	&suspend_max_freq.attr,
	NULL
};

static struct attribute_group dbs_attr_group = {
	.attrs = dbs_attributes,
	.name = "foreground",
};

/************************** sysfs end ************************/

static void dbs_check_cpu(struct cpu_dbs_info_s *this_dbs_info)
{
	unsigned int load = 0;
	unsigned int max_load = 0;
	unsigned int freq_target;

	struct cpufreq_policy *policy;
	unsigned int j;
	bool boosted = ktime_to_us(ktime_get()) < (last_input_time + dbs_tuners_ins.input_boost_ms * 1000);

	policy = this_dbs_info->cur_policy;

	/*
	 * Every sampling_rate, we check, if current idle time is less
	 * than 20% (default), then we try to increase frequency
	 * Every sampling_rate*sampling_down_factor, we check, if current
	 * idle time is more than 80%, then we try to decrease frequency
	 *
	 * Any frequency increase takes it to the maximum frequency.
	 * Frequency reduction happens at minimum steps of
	 * 5% (default) of maximum frequency
	 */

	/* Get Absolute Load */
	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_info_s *j_dbs_info;
		cputime64_t cur_wall_time, cur_idle_time;
		unsigned int idle_time, wall_time;

		j_dbs_info = &per_cpu(cs_cpu_dbs_info, j);

		cur_idle_time = get_cpu_idle_time(j, &cur_wall_time);

		wall_time = (unsigned int) cputime64_sub(cur_wall_time,
				j_dbs_info->prev_cpu_wall);
		j_dbs_info->prev_cpu_wall = cur_wall_time;

		idle_time = (unsigned int) cputime64_sub(cur_idle_time,
				j_dbs_info->prev_cpu_idle);
		j_dbs_info->prev_cpu_idle = cur_idle_time;

		if (dbs_tuners_ins.ignore_nice) {
			cputime64_t cur_nice;
			unsigned long cur_nice_jiffies;

			cur_nice = cputime64_sub(kstat_cpu(j).cpustat.nice,
					 j_dbs_info->prev_cpu_nice);
			/*
			 * Assumption: nice time between sampling periods will
			 * be less than 2^32 jiffies for 32 bit sys
			 */
			cur_nice_jiffies = (unsigned long)
					cputime64_to_jiffies64(cur_nice);

			j_dbs_info->prev_cpu_nice = kstat_cpu(j).cpustat.nice;
			idle_time += jiffies_to_usecs(cur_nice_jiffies);
		}

		if (unlikely(!wall_time || wall_time < idle_time))
			continue;

		load = 100 * (wall_time - idle_time) / wall_time;

		if (load > max_load)
			max_load = load;
	}

	if (boosted) {
		if (policy->cur < dbs_tuners_ins.input_boost_freq) {
			this_dbs_info->requested_freq = dbs_tuners_ins.input_boost_freq;
			__cpufreq_driver_target(policy, dbs_tuners_ins.input_boost_freq, CPUFREQ_RELATION_H);
			return;
		}
	}

	/*
	 * break out if we 'cannot' reduce the speed as the user might
	 * want freq_step to be zero
	 */
	if (dbs_tuners_ins.freq_step == 0)
		return;

	//TODO calculate this only once at param/policy change?
	freq_target = (dbs_tuners_ins.freq_step * policy->max) / 100;

	/* max freq cannot be less than 100. But who knows.... */
	if (unlikely(freq_target == 0))
		freq_target = 5;

	/* Check for frequency increase */
	if (max_load > dbs_tuners_ins.up_threshold) {
		this_dbs_info->down_skip = 0;

		/* if we are already at full speed then break out early */
		if (this_dbs_info->requested_freq == policy->max || (suspend && dbs_tuners_ins.suspend_max_freq && this_dbs_info->requested_freq >= dbs_tuners_ins.suspend_max_freq))
			return;

		this_dbs_info->standby_counter = 0;

		if (suspend) {
			if (++(this_dbs_info->sampling_up_counter) < dbs_tuners_ins.suspend_sampling_up_factor)
				return;
		} else if (standby) {
			if (++(this_dbs_info->sampling_up_counter) < dbs_tuners_ins.standby_sampling_up_factor)
				return;
		} else 
			delay = dbs_tuners_ins._sampling_rate;

		this_dbs_info->sampling_up_counter = 0;

		this_dbs_info->requested_freq += freq_target;
		if (this_dbs_info->requested_freq > policy->max)
			this_dbs_info->requested_freq = policy->max;

		__cpufreq_driver_target(policy, this_dbs_info->requested_freq,
			CPUFREQ_RELATION_H);
		return;
	}

	this_dbs_info->sampling_up_counter = 0;

	/*
	 * if we cannot reduce the frequency anymore, break out early
	 */
	if (policy->cur == policy->min) {
		if (!suspend && !standby) {
			if (++(this_dbs_info->standby_counter) >= dbs_tuners_ins.standby_delay_factor)
				standby = true;

			delay = usecs_to_jiffies(dbs_tuners_ins.standby_sampling_rate);
		}
		return;
	}
	/*
	 * The optimal frequency is the frequency that is the lowest that
	 * can support the current CPU usage without triggering the up
	 * policy.
	 */
	/* Check for frequency decrease */

	if (max_load < this_dbs_info->down_threshold && (!boosted || policy->cur > dbs_tuners_ins.input_boost_freq)) {
		if (!suspend && !standby && ++(this_dbs_info->down_skip) < dbs_tuners_ins.sampling_down_factor)
			return;
		this_dbs_info->down_skip = 0;

		this_dbs_info->requested_freq -= freq_target;
		if (this_dbs_info->requested_freq < policy->min)
			this_dbs_info->requested_freq = policy->min;

		__cpufreq_driver_target(policy, this_dbs_info->requested_freq,
				CPUFREQ_RELATION_L);
		return;
	}
	this_dbs_info->down_skip = 0;
}

static void do_dbs_timer(struct work_struct *work)
{
	struct cpu_dbs_info_s *dbs_info =
		container_of(work, struct cpu_dbs_info_s, work.work);
	unsigned int cpu = dbs_info->cpu;

	mutex_lock(&dbs_info->timer_mutex);

	dbs_check_cpu(dbs_info);

	/* We want all CPUs to do sampling nearly on same jiffy */
	schedule_delayed_work_on(cpu, &dbs_info->work, delay - jiffies % delay);
	mutex_unlock(&dbs_info->timer_mutex);
}

static inline void dbs_timer_init(struct cpu_dbs_info_s *dbs_info)
{
	delay = dbs_tuners_ins._sampling_rate = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);

	dbs_info->enable = 1;
	dbs_info->down_skip = 0;
	INIT_DELAYED_WORK_DEFERRABLE(&dbs_info->work, do_dbs_timer);

	/* We want all CPUs to do sampling nearly on same jiffy */
	schedule_delayed_work_on(dbs_info->cpu, &dbs_info->work, delay - jiffies % delay);
}

static inline void dbs_timer_exit(struct cpu_dbs_info_s *dbs_info)
{
	dbs_info->enable = 0;
	cancel_delayed_work_sync(&dbs_info->work);
}

/* early_suspend */
static void dbs_suspend(struct early_suspend *handler)
{
	suspend = true;
	delay = usecs_to_jiffies(dbs_tuners_ins.suspend_sampling_rate);
}

static void dbs_resume(struct early_suspend *handler)
{
	//unsigned int cpu;
	struct cpu_dbs_info_s *this_dbs_info = &per_cpu(cs_cpu_dbs_info, 0);
	struct cpufreq_policy *policy = this_dbs_info->cur_policy;

	suspend = false;
	standby = false;
	delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);

	//set max freq
	__cpufreq_driver_target(
			policy,
			policy->max, CPUFREQ_RELATION_H);

	/*
	for_each_online_cpu(cpu) {
		this_dbs_info = &per_cpu(cs_cpu_dbs_info, cpu);
		__cpufreq_driver_target(
				policy,
				policy->max, CPUFREQ_RELATION_H);
	}
	*/
}

static struct early_suspend dbs_early_suspend = {
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB,
	.suspend = dbs_suspend,
	.resume = dbs_resume,
};
/* end early suspend */

/* input boost */
static void hotplug_input_event(struct input_handle *handle,
		unsigned int type, unsigned int code, int value)
{
	u64 now;
	struct cpu_dbs_info_s *dbs_info = &per_cpu(cs_cpu_dbs_info, 0);

	standby = false;
	delay = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);

	if (!dbs_tuners_ins.input_boost_freq)
		return;

	now = ktime_to_us(ktime_get());
	if (now - last_input_time < MIN_INPUT_INTERVAL)
		return;

	last_input_time = now;

	flush_delayed_work(&dbs_info->work);
}

static int hotplug_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "cpufreq";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void hotplug_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id hotplug_ids[] = {
	/* multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y) },
	},
	/* touchpad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	},
	/* Keypad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ },
};

static struct input_handler hotplug_input_handler = {
	.event          = hotplug_input_event,
	.connect        = hotplug_input_connect,
	.disconnect     = hotplug_input_disconnect,
	.name           = "cpufreq_foreground",
	.id_table       = hotplug_ids,
};
/* input boost */

static int cpufreq_governor_dbs(struct cpufreq_policy *policy,
				   unsigned int event)
{
	unsigned int cpu = policy->cpu;
	struct cpu_dbs_info_s *this_dbs_info;
	unsigned int j;
	int rc;

	this_dbs_info = &per_cpu(cs_cpu_dbs_info, cpu);

	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) || (!policy->cur))
			return -EINVAL;

		mutex_lock(&dbs_mutex);

		for_each_cpu(j, policy->cpus) {
			struct cpu_dbs_info_s *j_dbs_info;
			j_dbs_info = &per_cpu(cs_cpu_dbs_info, j);
			j_dbs_info->cur_policy = policy;

			j_dbs_info->prev_cpu_idle = get_cpu_idle_time(j,
						&j_dbs_info->prev_cpu_wall);
			if (dbs_tuners_ins.ignore_nice) {
				j_dbs_info->prev_cpu_nice =
						kstat_cpu(j).cpustat.nice;
			}
			recalculate_down_threshold(j_dbs_info);
		}

		this_dbs_info->freq_table = cpufreq_frequency_get_table(cpu);
		this_dbs_info->down_skip = 0;
		this_dbs_info->requested_freq = policy->cur;

		mutex_init(&this_dbs_info->timer_mutex);
		dbs_enable++;
		/*
		 * Start the timerschedule work, when this governor
		 * is used for first time
		 */
		if (dbs_enable == 1) {
			unsigned int latency;
			/* policy latency is in nS. Convert it to uS first */
			latency = policy->cpuinfo.transition_latency / 1000;
			if (latency == 0)
				latency = 1;

			rc = sysfs_create_group(cpufreq_global_kobject,
						&dbs_attr_group);
			if (rc) {
				mutex_unlock(&dbs_mutex);
				return rc;
			}

			/* Bring kernel and HW constraints together */
			min_sampling_rate = max(min_sampling_rate,
					MIN_LATENCY_MULTIPLIER * latency);
			dbs_tuners_ins.sampling_rate =
				max(min_sampling_rate,
				    latency * LATENCY_MULTIPLIER);
			dbs_tuners_ins._sampling_rate = usecs_to_jiffies(dbs_tuners_ins.sampling_rate);
			dbs_tuners_ins.standby_sampling_rate = 2*dbs_tuners_ins.sampling_rate;
			dbs_tuners_ins.suspend_sampling_rate = dbs_tuners_ins.sampling_rate;

			cpufreq_register_notifier(
					&dbs_cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
			register_early_suspend(&dbs_early_suspend);

			rc = input_register_handler(&hotplug_input_handler);
			if (rc)
				pr_err("Cannot register hotplug input handler.\n");
		}
		mutex_unlock(&dbs_mutex);

		dbs_timer_init(this_dbs_info);

		break;

	case CPUFREQ_GOV_STOP:
		dbs_timer_exit(this_dbs_info);

		mutex_lock(&dbs_mutex);
		dbs_enable--;
		mutex_destroy(&this_dbs_info->timer_mutex);

		/*
		 * Stop the timerschedule work, when this governor
		 * is used for first time
		 */
		if (dbs_enable == 0) {
			cpufreq_unregister_notifier(
					&dbs_cpufreq_notifier_block,
					CPUFREQ_TRANSITION_NOTIFIER);
			unregister_early_suspend(&dbs_early_suspend);
			input_unregister_handler(&hotplug_input_handler);
		}


		mutex_unlock(&dbs_mutex);
		if (!dbs_enable)
			sysfs_remove_group(cpufreq_global_kobject,
					   &dbs_attr_group);

		break;

	case CPUFREQ_GOV_LIMITS:
		mutex_lock(&this_dbs_info->timer_mutex);
		if (policy->max < this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(
					this_dbs_info->cur_policy,
					policy->max, CPUFREQ_RELATION_H);
		else if (policy->min > this_dbs_info->cur_policy->cur)
			__cpufreq_driver_target(
					this_dbs_info->cur_policy,
					policy->min, CPUFREQ_RELATION_L);
		mutex_unlock(&this_dbs_info->timer_mutex);

		break;
	}
	return 0;
}

#ifndef CONFIG_CPU_FREQ_DEFAULT_GOV_FOREGROUND
static
#endif
struct cpufreq_governor cpufreq_gov_foreground = {
	.name			= "foreground",
	.governor		= cpufreq_governor_dbs,
	.max_transition_latency	= TRANSITION_LATENCY_LIMIT,
	.owner			= THIS_MODULE,
};

static int __init cpufreq_gov_dbs_init(void)
{
	u64 idle_time;
	int cpu = get_cpu();

	idle_time = get_cpu_idle_time_us(cpu, NULL);
	put_cpu();
	if (idle_time != -1ULL) {
		/* Idle micro accounting is supported. Use finer thresholds */
		dbs_tuners_ins.up_threshold = MICRO_FREQUENCY_UP_THRESHOLD;
		dbs_tuners_ins.down_differential = MICRO_FREQUENCY_DOWN_DIFFERENTIAL;
		/*
		 * In nohz/micro accounting case we set the minimum frequency
		 * not depending on HZ, but fixed (very low). The deferred
		 * timer might skip some samples if idle/sleeping as needed.
		*/
		min_sampling_rate = MICRO_FREQUENCY_MIN_SAMPLE_RATE;
	} else {
		/* For correct statistics, we need 10 ticks for each measure */
		min_sampling_rate =
			MIN_SAMPLING_RATE_RATIO * jiffies_to_usecs(10);
	}

	return cpufreq_register_governor(&cpufreq_gov_foreground);
}

static void __exit cpufreq_gov_dbs_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_foreground);
}


MODULE_AUTHOR("Alexander Clouter <alex@digriz.org.uk>");
MODULE_DESCRIPTION("'cpufreq_foreground' - A dynamic cpufreq governor for "
		"Low Latency Frequency Transition capable processors "
		"optimised for use in a battery environment");
MODULE_LICENSE("GPL");

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_FOREGROUND
fs_initcall(cpufreq_gov_dbs_init);
#else
module_init(cpufreq_gov_dbs_init);
#endif
module_exit(cpufreq_gov_dbs_exit);
