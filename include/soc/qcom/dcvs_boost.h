/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __SOC_QCOM_DCVS_BOOST_H__
#define __SOC_QCOM_DCVS_BOOST_H__

#if IS_ENABLED(CONFIG_QCOM_DCVS_BOOST)
void qcom_dcvs_bus_boost_kick(unsigned int duration_ms);
#else
static inline void qcom_dcvs_bus_boost_kick(unsigned int duration_ms)
{
}
#endif /* CONFIG_QCOM_DCVS_BOOST */
#endif /* __SOC_QCOM_DCVS_BOOST_H__ */

