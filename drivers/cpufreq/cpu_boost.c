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
#include <linux/spinlock.h>
#include <linux/input.h>
#include <linux/timekeeping.h>
#include <linux/slab.h>

static struct freq_qos_request boost_max_req[NR_CPUS];
static DECLARE_BITMAP(boost_max_active, NR_CPUS);

static struct freq_qos_request boost_kick_req[NR_CPUS];
static DECLARE_BITMAP(boost_kick_active, NR_CPUS);

static DEFINE_SPINLOCK(pending_lock);
static DECLARE_BITMAP(pending_max_enable, NR_CPUS);
static DECLARE_BITMAP(pending_kick_enable, NR_CPUS);

static atomic_long_t boost_expires = ATOMIC_LONG_INIT(0);

static struct delayed_work boost_disable_work;

static struct notifier_block boost_policy_nb;

#define KICK_INPUT_WINDOW_MS 5000

static atomic64_t last_input_us = ATOMIC64_INIT(0);

static void cpu_boost_input_event(struct input_handle *handle,
				  unsigned int type, unsigned int code, int value)
{
	atomic64_set(&last_input_us, ktime_to_us(ktime_get()));
}

static int cpu_boost_input_connect(struct input_handler *handler,
				   struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "cpu-boost";

	error = input_register_handle(handle);
	if (error)
		goto err_register;

	error = input_open_device(handle);
	if (error)
		goto err_open;
	return 0;
err_open:
	input_unregister_handle(handle);
err_register:
	kfree(handle);
	return error;
}

static void cpu_boost_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id cpu_boost_input_ids[] = {
	{ /* multi-touch touchscreen */
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			    BIT_MASK(ABS_MT_POSITION_X) |
			    BIT_MASK(ABS_MT_POSITION_Y) },
	},
	{ /* touchpad */
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] = BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	},
	{ /* keyboard/keypad */
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ /* mouse */
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_RELBIT,
		.evbit = { BIT_MASK(EV_REL) },
		.relbit = { [BIT_WORD(REL_X)] = BIT_MASK(REL_X) | BIT_MASK(REL_Y) },
	},
	{ },
};

static struct input_handler cpu_boost_input_handler = {
	.event		= cpu_boost_input_event,
	.connect	= cpu_boost_input_connect,
	.disconnect	= cpu_boost_input_disconnect,
	.name		= "cpu-boost",
	.id_table	= cpu_boost_input_ids,
};

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
	unsigned long flags;
	DECLARE_BITMAP(en_max, NR_CPUS);
	DECLARE_BITMAP(en_kick, NR_CPUS);
	int cpu, leader;
	struct cpufreq_policy *policy;
	s32 max_khz, req_khz;

	bitmap_zero(en_max, NR_CPUS);
	bitmap_zero(en_kick, NR_CPUS);

	spin_lock_irqsave(&pending_lock, flags);
	if (!bitmap_empty(pending_max_enable, NR_CPUS))
		bitmap_copy(en_max, pending_max_enable, NR_CPUS);

	if (!bitmap_empty(pending_kick_enable, NR_CPUS))
		bitmap_copy(en_kick, pending_kick_enable, NR_CPUS);

	bitmap_zero(pending_max_enable, NR_CPUS);
	bitmap_zero(pending_kick_enable, NR_CPUS);
	spin_unlock_irqrestore(&pending_lock, flags);

	cpus_read_lock();
	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;

		leader = policy->cpu;
		if (cpu == leader) {
			if (test_bit(leader, en_max)) {
				max_khz = (s32)policy->cpuinfo.max_freq;
				if (!test_bit(leader, boost_max_active)) {
					if (freq_qos_add_request(&policy->constraints,
								 &boost_max_req[leader],
								 FREQ_QOS_MIN, max_khz) < 0) {
					} else {
						__set_bit(leader, boost_max_active);
					}
				} else {
					(void)freq_qos_update_request(&boost_max_req[leader], max_khz);
				}
			}

			if (test_bit(leader, en_kick)) {
				max_khz = (s32)policy->cpuinfo.max_freq;
				req_khz = kick_khz_for_cpu(leader);
				if (req_khz > 0) {
					if (req_khz > max_khz)
						req_khz = max_khz;
					if (!test_bit(leader, boost_kick_active)) {
						if (freq_qos_add_request(&policy->constraints,
									 &boost_kick_req[leader],
									 FREQ_QOS_MIN, req_khz) < 0) {
						} else {
							__set_bit(leader, boost_kick_active);
						}
					} else {
						(void)freq_qos_update_request(&boost_kick_req[leader], req_khz);
					}
				}
			}
		}
		cpufreq_cpu_put(policy);
	}

	now = jiffies;
	exp = atomic_long_read(&boost_expires);
	if (!boost_window_expired(now, exp)) {
		delay = time_after(exp, now) ? exp - now : 0;
		cpus_read_unlock();
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
	cpus_read_unlock();
}

void cpu_boost_max(unsigned int duration_ms)
{
	unsigned long now = jiffies;
	unsigned long new_exp = now + msecs_to_jiffies(duration_ms);
	unsigned long old = atomic_long_read(&boost_expires);
	unsigned long flags;

	for (;;) {
		if (time_after(old, new_exp))
			break;

		if (atomic_long_try_cmpxchg(&boost_expires, &old, new_exp))
			break;
	}

	mod_delayed_work(system_unbound_wq, &boost_disable_work, 0);
	spin_lock_irqsave(&pending_lock, flags);
	bitmap_fill(pending_max_enable, NR_CPUS);
	spin_unlock_irqrestore(&pending_lock, flags);
}
EXPORT_SYMBOL_GPL(cpu_boost_max);

void cpu_boost_kick(unsigned int duration_ms)
{
	{
		u64 now_us = ktime_to_us(ktime_get());
		u64 last_us = atomic64_read(&last_input_us);
		u64 window_us = (u64)KICK_INPUT_WINDOW_MS * 1000ULL;

		if (!last_us || (now_us - last_us) > window_us)
			return;
	}
	unsigned long now = jiffies;
	unsigned long new_exp = now + msecs_to_jiffies(duration_ms);
	unsigned long old = atomic_long_read(&boost_expires);
	unsigned long flags;

	for (;;) {
		if (time_after(old, new_exp))
			break;

		if (atomic_long_try_cmpxchg(&boost_expires, &old, new_exp))
			break;
	}

	mod_delayed_work(system_unbound_wq, &boost_disable_work, 0);
	spin_lock_irqsave(&pending_lock, flags);
	bitmap_fill(pending_kick_enable, NR_CPUS);
	spin_unlock_irqrestore(&pending_lock, flags);
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

		if (leader >= 0) {
			unsigned long flags;
			spin_lock_irqsave(&pending_lock, flags);
			__clear_bit(leader, pending_max_enable);
			__clear_bit(leader, pending_kick_enable);
			spin_unlock_irqrestore(&pending_lock, flags);
		}
	}
	return 0;
}

static int __init cpu_boost_init(void)
{
	int ret;

	INIT_DELAYED_WORK(&boost_disable_work, cpu_boost_worker);
	boost_policy_nb.notifier_call = boost_policy_notifier;
	cpufreq_register_notifier(&boost_policy_nb, CPUFREQ_POLICY_NOTIFIER);

	ret = input_register_handler(&cpu_boost_input_handler);
	if (ret)
		pr_warn("cpu_boost: input handler register failed (%d)\n", ret);

	pr_info("cpu_boost driver initialized\n");
	return 0;
}

late_initcall(cpu_boost_init);
