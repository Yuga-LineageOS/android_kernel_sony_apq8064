/*
 * Copyright (C) 2015-2017 Tom G. <roboter972@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "apq_hotplug: " fmt

#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/notifier.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>

#define SUSPEND_DELAY	(CONFIG_HZ * 2)

static struct notifier_block fb_notif;

static struct delayed_work offline_all_work;
static struct work_struct online_all_work;

static struct kobject *apq_hotplug_kobj;

unsigned int apq_hp_max_online_cpus;

static inline void offline_all_fn(struct work_struct *work)
{
	unsigned int cpu;

	for_each_online_cpu(cpu) {
		if (cpu != 0) {
			cpu_down(cpu);

			pr_debug("CPU%u down.\nCPU(s) running: %u\n",
						cpu, num_online_cpus());
		}
	}
}

static inline void __cpuinit online_all_fn(struct work_struct *work)
{
	unsigned int cpu;

	for_each_cpu_not_adj(cpu, cpu_online_mask) {
		if (cpu == 0)
			continue;
		cpu_up(cpu);

		pr_debug("CPU%u up.\nCPU(s) running: %u\n",
					cpu, num_online_cpus());
	}
}

static void apq_hotplug_suspend(void)
{
	if (work_pending(&online_all_work))
		cancel_work_sync(&online_all_work);

	schedule_delayed_work(&offline_all_work,
					msecs_to_jiffies(SUSPEND_DELAY));
}

static void apq_hotplug_resume(void)
{
	if (delayed_work_pending(&offline_all_work))
		cancel_delayed_work_sync(&offline_all_work);

	schedule_work(&online_all_work);
}

static int fb_notifier_callback(struct notifier_block *self,
					unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;

	if (evdata && evdata->data && event == FB_EVENT_BLANK) {
		blank = evdata->data;
		switch (*blank) {
			case FB_BLANK_UNBLANK:
				apq_hotplug_resume();
				break;
			default:
				apq_hotplug_suspend();
				break;
		}
	}

	return 0;
}

static ssize_t max_online_cpus_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n", apq_hp_max_online_cpus);
}

static ssize_t max_online_cpus_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int val;

	if (sscanf(buf, "%u", &val) != 1 || val > CONFIG_NR_CPUS)
		return -EINVAL;

	apq_hp_max_online_cpus = val;

	return count;
}

static struct kobj_attribute max_online_cpus_attribute =
	__ATTR(max_online_cpus, S_IRUGO | S_IWUSR, max_online_cpus_show,
						max_online_cpus_store);

static struct attribute *apq_hotplug_attrs[] = {
	&max_online_cpus_attribute.attr,
	NULL
};

static struct attribute_group apq_hotplug_attr_group = {
	.attrs = apq_hotplug_attrs,
};

static int apq_hotplug_sysfs_init(void)
{
	return sysfs_create_group(apq_hotplug_kobj, &apq_hotplug_attr_group);
}

static int __init apq_hotplug_init(void)
{
	INIT_DELAYED_WORK(&offline_all_work, offline_all_fn);
	INIT_WORK(&online_all_work, online_all_fn);

	apq_hotplug_kobj = kobject_create_and_add("apq_hotplug", kernel_kobj);
	if (!apq_hotplug_kobj) {
		pr_err("Failed to create apq_hotplug kobject!\n");
		return -ENOMEM;
	}

	apq_hotplug_sysfs_init();

	fb_notif.notifier_call = fb_notifier_callback;
	fb_register_client(&fb_notif);

	apq_hp_max_online_cpus = CONFIG_NR_CPUS;

	pr_info("Initialized!\n");

	return 0;
}

late_initcall(apq_hotplug_init);
