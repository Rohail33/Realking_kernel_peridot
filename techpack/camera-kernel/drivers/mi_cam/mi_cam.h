// MIUI ADD: Camera_CamSched
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */
#ifndef MICAM_H
#define MICAM_H
#include <linux/types.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/ioctl.h>

#define MI_CAM_DEV_NAME  "micam"
#define MI_CAM_SCHED   "mi_cam_sched"
#define MAXNUM  8
#define MAXCLUSTER 4
#define EXC_UTIL_F_DIVISOR 3


#define MI_CAM_SCHED_ENABLE  _IO(240, 0X0)
#define MI_CAM_SCHED_DISABLE _IO(240, 0X1)
#define MI_CAM_SCHED_WRITE  _IOW(240, 0X2, unsigned int)
#define MI_CAM_SCHED_READ  _IOR(240, 0X3, unsigned int)
#define MI_CAM_SCHED_SET_WDOG _IO(240, 0X4)

#define CYC_EV 0x11 /* 0st event*/
//#define STALL_FRONTEND_EV 0x23 /* 1st event*/
#define STALL_BACKEND_MEM_EV 0x4005 /* 2st event*/
#define HIGH_UTIL_THR 800

static DEFINE_PER_CPU(bool, cpu_is_hp);
struct event_data {
	u32 event_id;
	u64 prev_count;
	u64 cur_delta;
	u64 cached_total_count;
};
static struct event_data **pmu_events;

enum event_idx {
	CYC_EVENT,
	STALL_BACKEND_MEM_EVENT,
	NO_OF_EVENT
};
struct cpu_stats {
	rwlock_t clk_lock;
	u64 be_stall_ratio;
	u64 old_stall_ratio;
	unsigned long high_util;
};

struct cpu_rang {
	int min_index;
	int max_index;
};
static u8 cluster_val[MAXNUM];
static struct cpu_rang cpu_map[MAXCLUSTER];
void map_cluster_cpu(void);
void map_cluster_index(int cpu);
u8 get_first_cpu_index(int cpu);
void micam_exception_process(struct timer_list *timer);

static struct cpu_stats *percpu;
static struct task_struct *read_events_thread;
static struct hrtimer hr_timer;
static u64 sampling_period_ms = 40; // 50ms
static void cam_dyn_hrtimer_init(void);

#ifdef CONFIG_MI_CAM_WALT
typedef void (*cam_set_next_freq_f)(unsigned long util, unsigned long freq,
	unsigned long max, unsigned long *next_freq,
	struct cpufreq_policy *policy, bool *need_freq_update);
extern void register_cam_freq_hook(cam_set_next_freq_f f);
extern void unregister_cam_freq_hook(void);
#endif

struct micam_dev {
	dev_t dev;
	struct mutex  micam_lock;
	unsigned int  enable_feature;
	rwlock_t f_lock;
	struct timer_list  crm_wdog;
};
struct micam_user {
	unsigned long mode;
	unsigned long  cycles_time; //ms
};
#endif
// END Camera_CamSched
