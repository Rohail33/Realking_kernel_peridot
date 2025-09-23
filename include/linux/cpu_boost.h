#ifndef _CPU_BOOST_H
#define _CPU_BOOST_H

#if IS_ENABLED(CONFIG_CPU_BOOST_MAX)
void cpu_boost_all(unsigned int duration_ms);
#else
static inline void cpu_boost_all(unsigned int duration_ms)
{
}
#endif /* CONFIG_CPU_BOOST_MAX */
#endif /* _CPU_BOOST_H */
