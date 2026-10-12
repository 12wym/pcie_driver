// SPDX-License-Identifier: GPL-2.0
/* Teaching example: Linux 5.15, non-PREEMPT_RT. */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>

static struct tasklet_struct demo_tasklet;
static struct timer_list event_timer;
static atomic_t pending = ATOMIC_INIT(0);
static bool stopping;

static void demo_bottom(struct tasklet_struct *tasklet)
{
	int events = atomic_xchg(&pending, 0);

	/* Softirq context: no msleep(), mutex_lock(), or GFP_KERNEL. */
	if (events)
		pr_info("tasklet_demo: processed %d events\n", events);
}

static void demo_emit(struct timer_list *timer)
{
	if (READ_ONCE(stopping))
		return;

	/* Timer callback is TIMER_SOFTIRQ context, not a PCIe hard IRQ. */
	atomic_add(10, &pending);
	tasklet_schedule(&demo_tasklet);

	if (!READ_ONCE(stopping))
		mod_timer(&event_timer, jiffies + HZ);
}

static int __init demo_init(void)
{
	tasklet_setup(&demo_tasklet, demo_bottom);
	timer_setup(&event_timer, demo_emit, 0);
	mod_timer(&event_timer, jiffies + HZ);
	return 0;
}

static void __exit demo_exit(void)
{
	/* Stop the producer first, then wait for the bottom half. */
	WRITE_ONCE(stopping, true);
	del_timer_sync(&event_timer);
	tasklet_kill(&demo_tasklet);
}

module_init(demo_init);
module_exit(demo_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Timer-driven tasklet teaching example");
