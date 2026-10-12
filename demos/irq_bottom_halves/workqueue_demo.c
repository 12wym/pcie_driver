// SPDX-License-Identifier: GPL-2.0
/* Teaching example: Linux 5.15, ordinary threaded workqueue. */
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/sched.h>

static struct workqueue_struct *demo_wq;
static struct work_struct demo_work;
static struct timer_list event_timer;
static atomic_t pending = ATOMIC_INIT(0);
static bool stopping;

static void demo_bottom(struct work_struct *work)
{
	int events = atomic_xchg(&pending, 0);

	if (!events)
		return;

	/* Ordinary worker thread context: sleeping is allowed. */
	msleep(20);
	pr_info("workqueue_demo: %s processed %d events\n",
		current->comm, events);
}

static void demo_emit(struct timer_list *timer)
{
	if (READ_ONCE(stopping))
		return;

	/* Store the events independently of the work's pending bit. */
	atomic_add(10, &pending);
	queue_work(demo_wq, &demo_work);

	if (!READ_ONCE(stopping))
		mod_timer(&event_timer, jiffies + HZ);
}

static int __init demo_init(void)
{
	INIT_WORK(&demo_work, demo_bottom);
	demo_wq = alloc_ordered_workqueue("irq_demo_wq", 0);
	if (!demo_wq)
		return -ENOMEM;

	timer_setup(&event_timer, demo_emit, 0);
	mod_timer(&event_timer, jiffies + HZ);
	return 0;
}

static void __exit demo_exit(void)
{
	WRITE_ONCE(stopping, true);
	del_timer_sync(&event_timer);
	cancel_work_sync(&demo_work);
	destroy_workqueue(demo_wq);
}

module_init(demo_init);
module_exit(demo_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Timer-driven workqueue teaching example");
