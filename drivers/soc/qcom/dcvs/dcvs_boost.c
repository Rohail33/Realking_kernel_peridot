// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) "qcom-dcvs-boost: " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/bitops.h>
#include <linux/bitmap.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/bug.h>

#include <soc/qcom/dcvs.h>
#include <soc/qcom/dcvs_boost.h>

static const enum dcvs_hw_type boost_hw_list[] = {
	DCVS_DDR,
	DCVS_LLCC,
	DCVS_L3,
};

#define BOOSTER_NAME	"dcvs_boost"

static DECLARE_BITMAP(registered_hw, NUM_DCVS_HW_TYPES);
static DECLARE_BITMAP(active_hw, NUM_DCVS_HW_TYPES);
static DECLARE_BITMAP(active_max_hw, NUM_DCVS_HW_TYPES);
static DEFINE_MUTEX(boost_lock);

static DEFINE_SPINLOCK(pending_lock);
static DECLARE_BITMAP(pending_hw, NUM_DCVS_HW_TYPES);
static DECLARE_BITMAP(pending_max_hw, NUM_DCVS_HW_TYPES);

static atomic_long_t boost_expires = ATOMIC_LONG_INIT(0);
static struct delayed_work boost_disable_work;

static inline bool boost_window_expired(unsigned long now, unsigned long exp)
{
	return time_after(now, exp);
}

static inline u32 preset_for_hw(enum dcvs_hw_type hw)
{
	switch (hw) {
	case DCVS_DDR:
		return (u32)CONFIG_QCOM_DCVS_BOOST_KHZ_DDR;
	case DCVS_LLCC:
		return (u32)CONFIG_QCOM_DCVS_BOOST_KHZ_LLCC;
	case DCVS_L3:
		return (u32)CONFIG_QCOM_DCVS_BOOST_KHZ_L3;
	default:
		return 0;
	}
}

static int ensure_voter_registered(enum dcvs_hw_type hw)
{
	int ret;

	if (test_bit(hw, registered_hw))
		return 0;

	ret = qcom_dcvs_register_voter(BOOSTER_NAME, hw, DCVS_SLOW_PATH);
	if (!ret)
		set_bit(hw, registered_hw);

	return ret;
}

static int clamp_to_hw_bounds(enum dcvs_hw_type hw, u32 *khz)
{
	int ret;
	u32 min_khz = 0, max_khz = 0;

	ret = qcom_dcvs_hw_minmax_get(hw, &min_khz, &max_khz);
	if (ret)
		return ret;

	if (*khz < min_khz)
		*khz = min_khz;
	else if (*khz > max_khz)
		*khz = max_khz;

	return 0;
}

static int apply_votes(const unsigned long *mask, bool clear)
{
	struct dcvs_freq votes[NUM_DCVS_HW_TYPES] = { };
	u32 update_mask = 0;
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(boost_hw_list); i++) {
		enum dcvs_hw_type hw = boost_hw_list[i];
		u32 khz = clear ? 0 : preset_for_hw(hw);
		int rc;

		if (!test_bit(hw, mask))
			continue;

		rc = ensure_voter_registered(hw);
		if (rc)
			continue;

		if (!clear && khz) {
			rc = clamp_to_hw_bounds(hw, &khz);
			if (rc)
				continue;
		}

		votes[hw].hw_type = hw;
		votes[hw].ib = khz;
		votes[hw].ab = 0;
		update_mask |= BIT(hw);
	}

	if (!update_mask)
		return 0;

	ret = qcom_dcvs_update_votes(BOOSTER_NAME, votes, update_mask, DCVS_SLOW_PATH);
	return ret;
}

static int apply_votes_max(const unsigned long *mask, bool clear)
{
	struct dcvs_freq votes[NUM_DCVS_HW_TYPES] = { };
	u32 update_mask = 0;
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(boost_hw_list); i++) {
		enum dcvs_hw_type hw = boost_hw_list[i];
		u32 min_khz = 0, max_khz = 0;
		u32 khz;
		int rc;

		if (!test_bit(hw, mask))
			continue;

		rc = ensure_voter_registered(hw);
		if (rc)
			continue;

		if (clear) {
			khz = 0;
		} else {
			rc = qcom_dcvs_hw_minmax_get(hw, &min_khz, &max_khz);
			if (rc)
				continue;

			khz = max_khz;
		}

		votes[hw].hw_type = hw;
		votes[hw].ib = khz;
		votes[hw].ab = 0;
		update_mask |= BIT(hw);
	}

	if (!update_mask)
		return 0;

	ret = qcom_dcvs_update_votes(BOOSTER_NAME, votes, update_mask, DCVS_SLOW_PATH);
	return ret;
}

static void dcvs_boost_worker(struct work_struct *work)
{
	unsigned long flags;
	unsigned long en_mask[BITS_TO_LONGS(NUM_DCVS_HW_TYPES)] = { 0 };
	unsigned long en_max[BITS_TO_LONGS(NUM_DCVS_HW_TYPES)] = { 0 };
	unsigned long now = jiffies;
	unsigned long exp = atomic_long_read(&boost_expires);
	unsigned long delay;

	spin_lock_irqsave(&pending_lock, flags);
	if (!bitmap_empty(pending_hw, NUM_DCVS_HW_TYPES))
		bitmap_copy(en_mask, pending_hw, NUM_DCVS_HW_TYPES);

	if (!bitmap_empty(pending_max_hw, NUM_DCVS_HW_TYPES))
		bitmap_copy(en_max, pending_max_hw, NUM_DCVS_HW_TYPES);

	bitmap_zero(pending_hw, NUM_DCVS_HW_TYPES);
	bitmap_zero(pending_max_hw, NUM_DCVS_HW_TYPES);
	spin_unlock_irqrestore(&pending_lock, flags);

	if (!bitmap_empty(en_mask, NUM_DCVS_HW_TYPES) || !bitmap_empty(en_max, NUM_DCVS_HW_TYPES)) {
		mutex_lock(&boost_lock);
		if (!bitmap_empty(en_mask, NUM_DCVS_HW_TYPES)) {
			bitmap_or(active_hw, active_hw, en_mask, NUM_DCVS_HW_TYPES);
			(void)apply_votes(en_mask, false);
		}
		if (!bitmap_empty(en_max, NUM_DCVS_HW_TYPES)) {
			bitmap_or(active_max_hw, active_max_hw, en_max, NUM_DCVS_HW_TYPES);
			(void)apply_votes_max(en_max, false);
		}
		mutex_unlock(&boost_lock);
	}

	now = jiffies;
	exp = atomic_long_read(&boost_expires);
	if (!boost_window_expired(now, exp)) {
		delay = time_after(exp, now) ? exp - now : 0;
		mod_delayed_work(system_unbound_wq, &boost_disable_work, delay);
		return;
	}

	mutex_lock(&boost_lock);
	if (!bitmap_empty(active_hw, NUM_DCVS_HW_TYPES))
		(void)apply_votes(active_hw, true);

	if (!bitmap_empty(active_max_hw, NUM_DCVS_HW_TYPES))
		(void)apply_votes_max(active_max_hw, true);

	bitmap_zero(active_hw, NUM_DCVS_HW_TYPES);
	bitmap_zero(active_max_hw, NUM_DCVS_HW_TYPES);
	mutex_unlock(&boost_lock);
}

void qcom_dcvs_bus_boost_kick(unsigned int duration_ms)
{
	unsigned long now = jiffies;
	unsigned long new_exp = now + msecs_to_jiffies(duration_ms);
	unsigned long old = atomic_long_read(&boost_expires);
	unsigned long mask[BITS_TO_LONGS(NUM_DCVS_HW_TYPES)] = { 0 };
	int i;

	for (i = 0; i < ARRAY_SIZE(boost_hw_list); i++) {
		enum dcvs_hw_type hw = boost_hw_list[i];
		if (preset_for_hw(hw))
			__set_bit(hw, mask);
	}

	if (bitmap_empty(mask, NUM_DCVS_HW_TYPES))
		return;

	for (;;) {
		if (time_after(old, new_exp))
			break;

		if (atomic_long_try_cmpxchg(&boost_expires, &old, new_exp))
			break;
	}

	mod_delayed_work(system_unbound_wq, &boost_disable_work, 0);

	{
		unsigned long flags;
		spin_lock_irqsave(&pending_lock, flags);
		bitmap_or(pending_hw, pending_hw, mask, NUM_DCVS_HW_TYPES);
		spin_unlock_irqrestore(&pending_lock, flags);
	}
}
EXPORT_SYMBOL_GPL(qcom_dcvs_bus_boost_kick);

void qcom_dcvs_bus_boost_kick_max(unsigned int duration_ms)
{
	unsigned long now = jiffies;
	unsigned long new_exp = now + msecs_to_jiffies(duration_ms);
	unsigned long old = atomic_long_read(&boost_expires);
	unsigned long mask[BITS_TO_LONGS(NUM_DCVS_HW_TYPES)] = { 0 };
	int i;

	for (i = 0; i < ARRAY_SIZE(boost_hw_list); i++)
		__set_bit(boost_hw_list[i], mask);

	for (;;) {
		if (time_after(old, new_exp))
			break;

		if (atomic_long_try_cmpxchg(&boost_expires, &old, new_exp))
			break;
	}

	mod_delayed_work(system_unbound_wq, &boost_disable_work, 0);

	{
		unsigned long flags;
		spin_lock_irqsave(&pending_lock, flags);
		bitmap_or(pending_max_hw, pending_max_hw, mask, NUM_DCVS_HW_TYPES);
		spin_unlock_irqrestore(&pending_lock, flags);
	}
}
EXPORT_SYMBOL_GPL(qcom_dcvs_bus_boost_kick_max);

static int __init dcvs_boost_init(void)
{
	INIT_DELAYED_WORK(&boost_disable_work, dcvs_boost_worker);
	BUILD_BUG_ON(NUM_DCVS_HW_TYPES > 32);
	bitmap_zero(pending_hw, NUM_DCVS_HW_TYPES);
	bitmap_zero(pending_max_hw, NUM_DCVS_HW_TYPES);
	pr_info("initialized\n");
	return 0;
}

static void __exit dcvs_boost_exit(void)
{
	mutex_lock(&boost_lock);
	if (!bitmap_empty(active_hw, NUM_DCVS_HW_TYPES))
		(void)apply_votes(active_hw, true);

	if (!bitmap_empty(active_max_hw, NUM_DCVS_HW_TYPES))
		(void)apply_votes_max(active_max_hw, true);
	bitmap_zero(active_hw, NUM_DCVS_HW_TYPES);
	bitmap_zero(active_max_hw, NUM_DCVS_HW_TYPES);
	mutex_unlock(&boost_lock);
	cancel_delayed_work_sync(&boost_disable_work);
}

module_init(dcvs_boost_init);
module_exit(dcvs_boost_exit);

MODULE_DESCRIPTION("QCOM DCVS timed boost (DDR/LLCC/L3)");
MODULE_LICENSE("GPL");
