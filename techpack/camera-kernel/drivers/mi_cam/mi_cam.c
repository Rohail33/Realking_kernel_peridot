// MIUI ADD: Camera_CamSched
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/ktime.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/fb.h>
#include <linux/poll.h>
#include <soc/qcom/pmu_lib.h>
#include "mi_cam.h"

static uint cpu_param=0;
module_param(cpu_param, uint, 0644);

static struct micam_dev cam_dev;
static struct class *micam_class;
static unsigned int micam_major;

void map_cluster_cpu(void)
{
	int k;
	for(k = 0; k <MAXCLUSTER; k++)
	{
		if(0==k) {
			cpu_map[k].min_index = 0;
			cpu_map[k].max_index = 1;
		}
		if(1==k) {
			cpu_map[k].min_index = 2;
			cpu_map[k].max_index = 4;
		}
		if(2==k) {
			cpu_map[k].min_index = 5;
			cpu_map[k].max_index = 6;
		}
		if(3==k) {
			cpu_map[k].min_index = 7;
			cpu_map[k].max_index = 7;
		}
	}
}
void map_cluster_index(int cpu)
{
	if(0<=cpu && cpu <=1) {
		cluster_val[cpu] = 0;
	}
	if(2<=cpu && cpu <=4) {
		cluster_val[cpu] = 1;
	}
	if(5<=cpu && cpu <=6) {
		cluster_val[cpu] = 2;
	}
	if(7== cpu) {
		cluster_val[cpu] = 3;
	}
}

u8 get_first_cpu_index(int cpu)
{
	return cpu_map[cluster_val[cpu]].min_index;
}


/******PMU COUNT  START******/
static int set_event(struct event_data *ev, int cpu)
{
	int ret;

	ret = qcom_pmu_event_supported(ev->event_id, cpu);
	if (ret) {
		pr_err("msm_perf: %s failed, eventId:0x%x, cpu:%d, error code:%d\n",
				__func__, ev->event_id, cpu, ret);
		return ret;
	}

	return 0;
}

static void free_pmu_counters(unsigned int cpu)
{
	int i = 0;

	for (i = 0; i < NO_OF_EVENT; i++) {
		pmu_events[i][cpu].prev_count = 0;
		pmu_events[i][cpu].cur_delta = 0;
		pmu_events[i][cpu].cached_total_count = 0;
	}
}

static int init_pmu_counter(void)
{
	int cpu;
	int ret = 0;

	int i = 0, j = 0;
	int no_of_cpus = 0;

	for_each_possible_cpu(cpu)
		no_of_cpus++;

	percpu = kmalloc(sizeof(struct cpu_stats) * MAXNUM, GFP_KERNEL);
	pmu_events = kcalloc(NO_OF_EVENT, sizeof(struct event_data *), GFP_KERNEL);
	if (!pmu_events)
		return -ENOMEM;

	for (i = 0; i < NO_OF_EVENT; i++) {
		pmu_events[i] = kcalloc(no_of_cpus, sizeof(struct event_data), GFP_KERNEL);
		if (!pmu_events[i]) {
			for (j = i; j >= 0; j--) {
				kfree(pmu_events[j]);
				pmu_events[j] = NULL;
			}
			kfree(pmu_events);
			pmu_events = NULL;
			return -ENOMEM;
		}
	}

        map_cluster_cpu();
	/* Create events per CPU */
	for_each_possible_cpu(cpu) {
		percpu[cpu].be_stall_ratio = 0;
		percpu[cpu].old_stall_ratio=0;

		map_cluster_index(cpu);
		rwlock_init(&percpu[cpu].clk_lock);
		/* create cycle event */
		pmu_events[CYC_EVENT][cpu].event_id = CYC_EV;
		ret = set_event(&pmu_events[CYC_EVENT][cpu], cpu);
		if (ret < 0)
			return ret;

        	/* create Back-End Stall event */
		pmu_events[STALL_BACKEND_MEM_EVENT][cpu].event_id = STALL_BACKEND_MEM_EV;
		ret = set_event(&pmu_events[STALL_BACKEND_MEM_EVENT][cpu], cpu);
		if (ret < 0) {
			free_pmu_counters(cpu);
			return ret;
		}
	}
	return 0;
}

static inline void read_event(struct event_data *event, unsigned int cpu)
{
	u64 ev_count = 0;
	int ret;
	u64 total;
	unsigned int local_cpu = smp_processor_id();
	if (!event->event_id) {
		return;
	}
	if (!per_cpu(cpu_is_hp, cpu)) {
		if(local_cpu != cpu)
		{
			ret = qcom_pmu_read(cpu, event->event_id, &total);
		} else {
			ret = qcom_pmu_read_local(event->event_id, &total);
		}
		if (ret) {
			return;
		}
	}
	else
		total = event->cached_total_count;

	ev_count = total - event->prev_count;
	event->prev_count = total;
	event->cur_delta = ev_count;
}
/******PMU COUNT END********/
#ifdef CONFIG_MI_CAM_WALT
static unsigned int map_coeff_freq(unsigned int cpu, unsigned long max_freq, unsigned long util, unsigned long max )
{
	unsigned long target_freq = 0;
	//static unsigned long cached_util[NUM_OF_CLK_DOMIN] = {0};
	u64  new =0 ;
	static u64 old_ratio[MAXNUM];
	unsigned int first_cpu;

	first_cpu = get_first_cpu_index(cpu);
	if( read_trylock(&percpu[first_cpu].clk_lock)) {
		new = percpu[first_cpu].be_stall_ratio;
		old_ratio[first_cpu]   = percpu[first_cpu].old_stall_ratio;
		read_unlock(&percpu[first_cpu].clk_lock);
	}
	else
	{
		new = old_ratio[first_cpu] ;
	}
	if(new < 1024 && new > 0) {
		target_freq = mult_frac(max_freq * util, (1024 - new), 1024 * max);
	}
	if(cpu_param) {
		pr_info("%s: [MICAM_SCHED]  map_coeff_freq[%lu]: target_freq[%lu], util = %lu, max = %lu, new %lu", __func__,cpu, target_freq, util, max, new);
	}
	if (target_freq && (target_freq < max_freq)) {
	    return target_freq;
	} else{
	    return max_freq;
	}
}

static void cam_set_next_freq(unsigned long util, unsigned long freq, unsigned long max,
	unsigned long *next_freq, struct cpufreq_policy *policy, bool *need_freq_update)
{
	unsigned long old_freq;
	unsigned int cpu = policy->cpu;
	unsigned int exc_util = max >> EXC_UTIL_F_DIVISOR;
	struct micam_dev *micam_dev = &cam_dev;
	static unsigned int enable_feature;

	if(read_trylock(&micam_dev->f_lock)) {
		enable_feature = micam_dev->enable_feature;
		read_unlock(&micam_dev->f_lock);
	}
	old_freq = *next_freq;

	if(cpu_param == 2) {
		pr_info("%s:  [MICAM_SCHED] cpu = %d, max = %d, walt_util = %d, walt_freq = %d, exc_util = %d, enable %d",
			__func__, cpu,  max, util, freq, exc_util, enable_feature);
	}
	if(enable_feature) {
		if(cpu !=0 && cpu !=1 && util > exc_util) {
			*next_freq = map_coeff_freq(cpu, policy->cpuinfo.max_freq, util, max);
			if(*next_freq) {
				*next_freq = min(freq, *next_freq);
				if (*next_freq != old_freq)
					*need_freq_update = true;
			}
		}
	}
}
#endif

static enum hrtimer_restart cam_dyn_hrtimer_handler(struct hrtimer *timer)
{
	wake_up_process(read_events_thread);
	hrtimer_forward_now(&hr_timer, ms_to_ktime(sampling_period_ms));
	return HRTIMER_RESTART;
}


static int kthread_get_cpu_pmu_events(void * data)
{
	u64 cycles = 0;
	u64 backend_stall = 0;
	u8   cpu =0 ;
	u8 cluster = 0;
	static u64 old[MAXNUM] ={1024};
	unsigned int first_cpu;

//	unsigned int local_cpu = smp_processor_id();
        /* Read Instruction event */
	while(!kthread_should_stop())
	{
		set_current_state(TASK_UNINTERRUPTIBLE);
		for(cluster=0; cluster < MAXCLUSTER; cluster++) {
			u64 min_tmp= 1024;
			u64 old_tmp =0;
			first_cpu = cpu_map[cluster].min_index;
			old_tmp = old[first_cpu] ;
			for(cpu=cpu_map[cluster].min_index; cpu <= cpu_map[cluster].max_index; cpu++){
				/* Read Cycle event */
				read_event(&pmu_events[CYC_EVENT][cpu], cpu);
				/* Read Back-end stall event */
				read_event(&pmu_events[STALL_BACKEND_MEM_EVENT][cpu], cpu);
				cycles = pmu_events[CYC_EVENT][cpu].cur_delta;
				backend_stall = pmu_events[STALL_BACKEND_MEM_EVENT][cpu].cur_delta;
				if(cycles && backend_stall && (backend_stall  <  cycles)) {
					old[cpu] = mult_frac(1024, backend_stall, cycles);
					if(old[cpu]  < min_tmp)
						min_tmp = old[cpu];
				}
				//pr_info("cuixiaojie cpu:[%d] ev_cycles: %lu ev_backend_stall: %lu, stall_ratio %lu \n", cpu, cycles,backend_stall, old[cpu]);
			}
			old[first_cpu] = min_tmp;
			// pr_info("cpu [%lu] min_ratio %lu , old_ratio %d\n",cpu, old[first_cpu], old_tmp);
			write_lock(&percpu[first_cpu].clk_lock);
			percpu[first_cpu].be_stall_ratio = old[first_cpu];
			percpu[first_cpu].old_stall_ratio = old_tmp;
			write_unlock(&percpu[first_cpu].clk_lock);
		}
		schedule();
	}
	return 0;
}

static void cam_dyn_hrtimer_init(void)
{
	hrtimer_init(&hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hr_timer.function = &cam_dyn_hrtimer_handler;
	hrtimer_start(&hr_timer, ms_to_ktime(sampling_period_ms), HRTIMER_MODE_REL);
}

static void cam_dyn_hrtimer_cancel(void)
{
	hrtimer_cancel(&hr_timer );
}

void micam_exception_process(struct timer_list *timer)
{
#ifdef CONFIG_MI_CAM_WALT
	unregister_cam_freq_hook();
	pr_err("%s:  [MICAM_SCHED] trigger watchdog  to exit micam_sched!", __func__);
#endif
	cam_dyn_hrtimer_cancel();
}

static int micam_open(struct inode *inode, struct file *filp)
{
	cam_dyn_hrtimer_init();
#ifdef CONFIG_MI_CAM_WALT
	register_cam_freq_hook(cam_set_next_freq);
	pr_info("%s: [MICAM_SCHED] open success!", __func__);
#endif
	return 0;
}

static int micam_release(struct inode *inode, struct file *filp)
{
#ifdef CONFIG_MI_CAM_WALT
	unregister_cam_freq_hook();
	pr_info("%s:  [MICAM_SCHED] Close success!", __func__);
#endif
	cam_dyn_hrtimer_cancel();
	return 0;
}

ssize_t micam_read(struct file *file, char __user *buff, size_t length, loff_t *ppos)
{
	pr_info("%s: [MICAM_SCHED] read success!",__func__);
	return 0;
}

ssize_t micam_write(struct file *file, const char __user *buf, size_t length, loff_t *ppos)
{
	pr_info("%s: [MICAM_SCHED] write success!",__func__);
	return 0;
}

static long micam_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret;
	//struct micam_user * casedata=NULL;
	struct micam_dev *micam_dev = &cam_dev;

	switch (cmd) {
		case MI_CAM_SCHED_ENABLE:
			write_lock(&micam_dev->f_lock);
			micam_dev->enable_feature = 1;
			write_unlock(&micam_dev->f_lock);
			mod_timer(&micam_dev->crm_wdog, jiffies +msecs_to_jiffies(5000));  //5s
			pr_info("%s : [MICAM_SCHED] Start adjust walt!", __func__);
			break;
		case MI_CAM_SCHED_DISABLE:
			write_lock(&micam_dev->f_lock);
			micam_dev->enable_feature = 0;
			write_unlock(&micam_dev->f_lock);
			del_timer(&micam_dev->crm_wdog);
			pr_info("%s: [MICAM_SCHED] Stop adjust walt!", __func__);
			break;
		case MI_CAM_SCHED_SET_WDOG:
			mod_timer(&micam_dev->crm_wdog, jiffies +msecs_to_jiffies(5000));  //5s
			break;
		case MI_CAM_SCHED_WRITE:
			break;
		case MI_CAM_SCHED_READ:
			break;
		default:
            		ret = -EINVAL;
            		goto err;
	}
	ret =0;
err:
	return ret;
}

static const struct file_operations micam_fops =
{
	.owner = THIS_MODULE,
	.unlocked_ioctl = micam_ioctl,
	.open = micam_open,
	.release = micam_release,
	//.poll = micam_poll,
	.read = micam_read,
	.write= micam_write,
};

static int __init mi_camera_init(void)
{

	struct device *device;
	struct micam_dev *micam_dev =&cam_dev;

	micam_major =  register_chrdev(0,MI_CAM_DEV_NAME, &micam_fops);
	micam_class = class_create(THIS_MODULE, MI_CAM_DEV_NAME);
	if (IS_ERR(micam_class)) {
		unregister_chrdev(micam_major,  MI_CAM_DEV_NAME);
		pr_warn("Failed to create class.\n");
		return PTR_ERR(micam_class);
	}

	micam_dev->enable_feature =0;
	micam_dev->dev = MKDEV(micam_major, 0);
	device = device_create(micam_class, NULL, micam_dev->dev,
                NULL, MI_CAM_SCHED);
	if (IS_ERR(device)) {
		pr_err("error while trying to create %s\n",MI_CAM_SCHED);
		return -EINVAL;
	}
	mutex_init(&micam_dev->micam_lock);
	rwlock_init(&micam_dev->f_lock);

	init_pmu_counter();
	read_events_thread = kthread_run(kthread_get_cpu_pmu_events,
				NULL, "CAM_SCHED:read_event");

	if(IS_ERR(read_events_thread))
		return PTR_ERR(read_events_thread);
	pr_info("%s: [MICAM_SCHED] read_event PID %d",__func__,read_events_thread->pid);

	timer_setup(&micam_dev->crm_wdog, micam_exception_process,0);
	micam_dev->crm_wdog.expires = jiffies +msecs_to_jiffies(5000);   //5s

	return 0;
}

static  void  __exit mi_cam_exit(void)
{
	struct micam_dev *micam_dev =&cam_dev;

	if(read_events_thread)
		kthread_stop(read_events_thread);
	kfree(pmu_events);
	pmu_events=NULL;
	kfree(*pmu_events);
	*pmu_events=NULL;
	kfree(percpu);
	percpu=NULL;

	device_destroy(micam_class, micam_dev->dev);
	class_destroy(micam_class);
}

module_init(mi_camera_init);
module_exit(mi_cam_exit);
MODULE_DESCRIPTION("mi cam sched driver");
MODULE_LICENSE("GPL v2");
// END Camera_CamSched
