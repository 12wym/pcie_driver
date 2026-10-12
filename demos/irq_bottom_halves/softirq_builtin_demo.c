// SPDX-License-Identifier: GPL-2.0
/*
 * Teaching example for a Linux 5.15 non-PREEMPT_RT kernel tree.
 * Requires a new DEMO_SOFTIRQ enum slot and matching softirq_to_name entry.
 * Build into vmlinux (obj-y), not as an external loadable module.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/bottom_half.h>
#include <linux/percpu.h>
#include <linux/atomic.h>

static DEFINE_PER_CPU(atomic_t, demo_pending) = ATOMIC_INIT(0);

static void demo_bottom(struct softirq_action *action)
{
	int events = atomic_xchg(this_cpu_ptr(&demo_pending), 0);

	/* Fixed, short operation; no sleeping and no unbounded drain loop. */
	if (events)
		pr_info("softirq_demo: CPU%d processed %d events\n",
			smp_processor_id(), events);
}

static int __init demo_init(void)
{
	int i;

	open_softirq(DEMO_SOFTIRQ, demo_bottom);

	/* Keep this producer on one CPU and defer the callback until ready. */
	local_bh_disable();
	for (i = 0; i < 10; i++) {
		atomic_inc(this_cpu_ptr(&demo_pending));
		raise_softirq(DEMO_SOFTIRQ);
	}
	local_bh_enable();
	return 0;
}
late_initcall(demo_init);
