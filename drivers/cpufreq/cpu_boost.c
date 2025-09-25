// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/pm_qos.h>
#include <linux/workqueue.h>
#include <linux/threads.h>
#include <linux/bitops.h>
#include <linux/bitmap.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>
#include <linux/minmax.h>
#include <linux/cpu_boost.h>

static struct freq_qos_request boost_max_req[NR_CPUS];
static DECLARE_BITMAP(boost_max_active, NR_CPUS);

static struct freq_qos_request boost_kick_req[NR_CPUS];
static DECLARE_BITMAP(boost_kick_active, NR_CPUS);

static atomic_long_t boost_expires = ATOMIC_LONG_INIT(0);

static struct delayed_work boost_disable_work;

static struct notifier_block boost_policy_nb;

static inline bool boost_window_expired(unsigned long now, unsigned long exp)
{
	return time_after(now, exp);
}

static inline s32 kick_khz_for_cpu(int cpu)
{
	if (cpu <= 1)
		return (s32)CONFIG_CPU_BOOST_KICK_KHZ_LITTLE;
	else if (cpu <= 4)
		return (s32)CONFIG_CPU_BOOST_KICK_KHZ_MID;
	else if (cpu <= 6)
		return (s32)CONFIG_CPU_BOOST_KICK_KHZ_BIG;
	else
		return (s32)CONFIG_CPU_BOOST_KICK_KHZ_PRIME;
}

static void cpu_boost_worker(struct work_struct *work)
{
	unsigned long now = jiffies;
	unsigned long exp = atomic_long_read(&boost_expires);
	unsigned long delay;
	int cpu, leader;
	struct cpufreq_policy *policy;

	if (!boost_window_expired(now, exp)) {
		delay = time_after(exp, now) ? exp - now : 0;
		mod_delayed_work(system_unbound_wq, &boost_disable_work, delay);
		return;
	}

	now = jiffies;
	exp = atomic_long_read(&boost_expires);
	if (!boost_window_expired(now, exp)) {
		delay = time_after(exp, now) ? exp - now : 0;
		mod_delayed_work(system_unbound_wq, &boost_disable_work, delay);
		return;
	}

	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;

		leader = policy->cpu;
		if (cpu == leader) {
			if (test_and_clear_bit(leader, boost_max_active))
				freq_qos_remove_request(&boost_max_req[leader]);
			if (test_and_clear_bit(leader, boost_kick_active))
				freq_qos_remove_request(&boost_kick_req[leader]);
		}
		cpufreq_cpu_put(policy);
	}
}

void cpu_boost_max(unsigned int duration_ms)
{
	unsigned long now = jiffies;
	unsigned long new_exp = now + msecs_to_jiffies(duration_ms);
	unsigned long old = atomic_long_read(&boost_expires);
	int cpu, leader, ret;
	struct cpufreq_policy *policy;
	s32 max_khz;

	for (;;) {
		if (time_after(old, new_exp))
			break;
		if (atomic_long_try_cmpxchg(&boost_expires, &old, new_exp))
			break;
	}

	{
		unsigned long exp = atomic_long_read(&boost_expires);
		unsigned long delay = time_after(exp, now) ? exp - now : 0;
		mod_delayed_work(system_unbound_wq, &boost_disable_work, delay);
	}

	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;

		leader = policy->cpu;
		if (cpu == leader) {
			max_khz = (s32)policy->cpuinfo.max_freq;
			if (!test_and_set_bit(leader, boost_max_active)) {
				ret = freq_qos_add_request(&policy->constraints,
							   &boost_max_req[leader],
							   FREQ_QOS_MIN, max_khz);
				if (ret < 0)
					clear_bit(leader, boost_max_active);
			} else {
				ret = freq_qos_update_request(&boost_max_req[leader],
							      max_khz);
				(void)ret;
			}
		}
		cpufreq_cpu_put(policy);
	}
}
EXPORT_SYMBOL_GPL(cpu_boost_max);

void cpu_boost_kick(unsigned int duration_ms)
{
	unsigned long now = jiffies;
	unsigned long new_exp = now + msecs_to_jiffies(duration_ms);
	unsigned long old = atomic_long_read(&boost_expires);
	int cpu, leader, ret;
	struct cpufreq_policy *policy;
	s32 req_khz, max_khz;

	for (;;) {
		if (time_after(old, new_exp))
			break;

		if (atomic_long_try_cmpxchg(&boost_expires, &old, new_exp))
			break;
	}

	{
		unsigned long exp = atomic_long_read(&boost_expires);
		unsigned long delay = time_after(exp, now) ? exp - now : 0;
		mod_delayed_work(system_unbound_wq, &boost_disable_work, delay);
	}

	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;

		leader = policy->cpu;
		if (cpu == leader) {
			max_khz = (s32)policy->cpuinfo.max_freq;
			req_khz = kick_khz_for_cpu(leader);

			if (req_khz > 0) {
				if (req_khz > max_khz)
					req_khz = max_khz;

				if (!test_and_set_bit(leader, boost_kick_active)) {
					ret = freq_qos_add_request(&policy->constraints,
								   &boost_kick_req[leader],
								   FREQ_QOS_MIN, req_khz);
					if (ret < 0)
						clear_bit(leader, boost_kick_active);
				} else {
					ret = freq_qos_update_request(&boost_kick_req[leader],
								      req_khz);
					(void)ret;
				}
			}
		}

		cpufreq_cpu_put(policy);
	}
}
EXPORT_SYMBOL_GPL(cpu_boost_kick);

static int boost_policy_notifier(struct notifier_block *nb,
				 unsigned long val, void *data)
{
	struct cpufreq_policy *policy = data;
	int leader = policy ? policy->cpu : -1;

	if (val == CPUFREQ_REMOVE_POLICY) {
		if (leader >= 0 && test_and_clear_bit(leader, boost_max_active))
			freq_qos_remove_request(&boost_max_req[leader]);

		if (leader >= 0 && test_and_clear_bit(leader, boost_kick_active))
			freq_qos_remove_request(&boost_kick_req[leader]);
	}
	return 0;
}

static int __init cpu_boost_init(void)
{
	INIT_DELAYED_WORK(&boost_disable_work, cpu_boost_worker);
	boost_policy_nb.notifier_call = boost_policy_notifier;
	cpufreq_register_notifier(&boost_policy_nb, CPUFREQ_POLICY_NOTIFIER);
	pr_info("cpu_boost driver initialized\n");
	return 0;
}

late_initcall(cpu_boost_init);
