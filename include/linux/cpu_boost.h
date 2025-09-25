#ifndef _CPU_BOOST_H
#define _CPU_BOOST_H

#if IS_ENABLED(CONFIG_CPU_BOOSTING)
void cpu_boost_max(unsigned int duration_ms);
void cpu_boost_kick(unsigned int duration_ms);
#else
static inline void cpu_boost_max(unsigned int duration_ms)
{
}
static inline void cpu_boost_kick(unsigned int duration_ms)
{
}
#endif /* CONFIG_CPU_BOOST_MAX */
#endif /* _CPU_BOOST_H */
