/* kernel/power/powersuspend.c
 *
 * Copyright (C) 2005-2008 Google, Inc.
 * Copyright (C) 2013 Paul Reioux
 *
 * Modified by Lonelyoneskatter <threesixoh.skater@yahoo.com>
 * Modified by Jean-Pierre Rasquin <yank555.lu@gmail.com>
 *
 *  v2.0 - Many fixes, unofficial bump to 2.0.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/powersuspend.h>

#define MAJOR_VERSION	2
#define MINOR_VERSION	0

struct workqueue_struct *power_suspend_work_queue;

static DEFINE_MUTEX(power_suspend_lock);
static LIST_HEAD(power_suspend_handlers);
static void power_suspend(struct work_struct *work);
static void power_resume(struct work_struct *work);
static DECLARE_WORK(power_suspend_work, power_suspend);
static DECLARE_WORK(power_resume_work, power_resume);
static DEFINE_SPINLOCK(state_lock);

static int state;
static int mode;
bool power_suspended;

void register_power_suspend(struct power_suspend *handler)
{
	struct list_head *pos;

	mutex_lock(&power_suspend_lock);
	list_for_each(pos, &power_suspend_handlers) {
		struct power_suspend *p;
		p = list_entry(pos, struct power_suspend, link);
	}
	list_add_tail(&handler->link, pos);
	mutex_unlock(&power_suspend_lock);
}
EXPORT_SYMBOL(register_power_suspend);

void unregister_power_suspend(struct power_suspend *handler)
{
	mutex_lock(&power_suspend_lock);
	list_del(&handler->link);
	mutex_unlock(&power_suspend_lock);
}
EXPORT_SYMBOL(unregister_power_suspend);

static void power_suspend(struct work_struct *work)
{
	struct power_suspend *pos;
	unsigned long irqflags;

	mutex_lock(&power_suspend_lock);
	spin_lock_irqsave(&state_lock, irqflags);
	if (state != POWER_SUSPEND_ACTIVE) {
		state = POWER_SUSPEND_ACTIVE;
		power_suspended = true;
	}
	spin_unlock_irqrestore(&state_lock, irqflags);
	mutex_unlock(&power_suspend_lock);

	list_for_each_entry(pos, &power_suspend_handlers, link) {
		if (pos->suspend != NULL) {
			pos->suspend(pos);
		}
	}
}

static void power_resume(struct work_struct *work)
{
	struct power_suspend *pos;
	unsigned long irqflags;

	mutex_lock(&power_suspend_lock);
	spin_lock_irqsave(&state_lock, irqflags);
	if (state != POWER_SUSPEND_INACTIVE) {
		state = POWER_SUSPEND_INACTIVE;
		power_suspended = false;
	}
	spin_unlock_irqrestore(&state_lock, irqflags);
	mutex_unlock(&power_suspend_lock);

	list_for_each_entry_reverse(pos, &power_suspend_handlers, link) {
		if (pos->resume != NULL) {
			pos->resume(pos);
		}
	}
}

void set_power_suspend_state(int new_state)
{
	unsigned long irqflags;

	spin_lock_irqsave(&state_lock, irqflags);
	if (state == POWER_SUSPEND_INACTIVE && new_state == POWER_SUSPEND_ACTIVE) {
		state = new_state;
		power_suspended = true;
		queue_work(power_suspend_work_queue, &power_suspend_work);
	} else if (state == POWER_SUSPEND_ACTIVE && new_state == POWER_SUSPEND_INACTIVE) {
		state = new_state;
		power_suspended = true;
		queue_work(power_suspend_work_queue, &power_resume_work);
	}
	spin_unlock_irqrestore(&state_lock, irqflags);
}

void set_power_suspend_state_autosleep_hook(int new_state)
{
	if (mode == POWER_SUSPEND_AUTOSLEEP || mode == POWER_SUSPEND_HYBRID)
		set_power_suspend_state(new_state);
}
EXPORT_SYMBOL(set_power_suspend_state_autosleep_hook);

void set_power_suspend_state_panel_hook(int new_state)
{
	if (mode == POWER_SUSPEND_PANEL || mode == POWER_SUSPEND_HYBRID)
		set_power_suspend_state(new_state);
}
EXPORT_SYMBOL(set_power_suspend_state_panel_hook);

// ------------------------------------------ sysfs interface ------------------------------------------

static ssize_t power_suspend_state_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", state);
}

static ssize_t power_suspend_state_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int new_state = 0;

	if (mode != POWER_SUSPEND_USERSPACE)
		return -EINVAL;

	sscanf(buf, "%d\n", &new_state);

	if(new_state == POWER_SUSPEND_ACTIVE || new_state == POWER_SUSPEND_INACTIVE)
		set_power_suspend_state(new_state);

	return count;
}

static struct kobj_attribute power_suspend_state_attribute =
	__ATTR(power_suspend_state, S_IRUGO|S_IWUSR,
		power_suspend_state_show,
		power_suspend_state_store);

static ssize_t power_suspend_mode_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", mode);
}

static ssize_t power_suspend_mode_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val = 0;

	sscanf(buf, "%d", &val);

	switch (val) {
		case 0:
			mode = POWER_SUSPEND_AUTOSLEEP;
			break;
		case 1:
			mode = POWER_SUSPEND_USERSPACE;
			break;
		case 2:
			mode = POWER_SUSPEND_PANEL;
			break;
		case 3:
			mode = POWER_SUSPEND_HYBRID;
			break;
		default:
			break;
	}
	return count;
}

static struct kobj_attribute power_suspend_mode_attribute =
	__ATTR(power_suspend_mode, S_IRUGO|S_IWUSR,
		power_suspend_mode_show,
		power_suspend_mode_store);

static ssize_t power_suspend_version_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "version: %d.%d\n", MAJOR_VERSION, MINOR_VERSION);
}

static struct kobj_attribute power_suspend_version_attribute =
	__ATTR(power_suspend_version, 0444,
		power_suspend_version_show,
		NULL);

static struct attribute *power_suspend_attrs[] =
{
	&power_suspend_state_attribute.attr,
	&power_suspend_mode_attribute.attr,
	&power_suspend_version_attribute.attr,
	NULL,
};

static struct attribute_group power_suspend_attr_group =
{
	.attrs = power_suspend_attrs,
};

static struct kobject *power_suspend_kobj;

// ------------------ sysfs interface -----------------------
static int __init power_suspend_init(void)
{

	int sysfs_result;

	power_suspend_kobj = kobject_create_and_add("power_suspend",
				kernel_kobj);
	if (!power_suspend_kobj) {
		pr_err("%s kobject create failed!\n", __FUNCTION__);
		return -ENOMEM;
	}

	sysfs_result = sysfs_create_group(power_suspend_kobj,
			&power_suspend_attr_group);

	if (sysfs_result) {
		pr_info("%s group create failed!\n", __FUNCTION__);
		kobject_put(power_suspend_kobj);
		return -ENOMEM;
	}

	power_suspend_work_queue =
		alloc_workqueue("power_suspend",
			WQ_HIGHPRI | WQ_UNBOUND | WQ_MEM_RECLAIM, 0);

	if (power_suspend_work_queue == NULL) {
		return -ENOMEM;
	}

	/* Set mode on boot. */
	mode = POWER_SUSPEND_HYBRID;

	return 0;
}

static void __exit power_suspend_exit(void)
{
	if (power_suspend_kobj != NULL)
		kobject_put(power_suspend_kobj);

	destroy_workqueue(power_suspend_work_queue);
} 

core_initcall(power_suspend_init);
module_exit(power_suspend_exit);

MODULE_AUTHOR("Paul Reioux <reioux@gmail.com> / Jean-Pierre Rasquin <yank555.lu@gmail.com>");
MODULE_DESCRIPTION("power_suspend - A replacement kernel PM driver for"
		   "Android's deprecated early_suspend/late_resume PM driver!");
MODULE_LICENSE("GPL v2");
