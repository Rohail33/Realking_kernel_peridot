/*
 * Copyright (c) 2023 Xiaomi, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Xiaomi, Inc.
 */
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/stdarg.h>
#if defined(CONFIG_DRM)
#include <linux/soc/qcom/panel_event_notifier.h>
#endif
#include <linux/power_supply.h>
#include <net/sock.h>
#include <net/netlink.h>

#include "xiaomi_touch.h"
static xiaomi_touch_t xiaomi_touch;
static struct task_struct *xiaomi_touch_temp_thread;

#define NETLINK_TEST 24
#define MAX_MSGSIZE 16
int stringlength(char *s);
static int pid = -1;
struct sock *nl_sk;

void sendnlmsg(char message)//char *message
{
	struct sk_buff *skb_1;
	struct nlmsghdr *nlh;
	int len = NLMSG_SPACE(MAX_MSGSIZE);
	int slen = 0;
	int ret = 0;

	if (!nl_sk || !pid) {
		return;
	}

	skb_1 = alloc_skb(len, GFP_KERNEL);

	if (!skb_1) {
		LOG_ERROR("alloc_skb error\n");
		return;
	}

	slen = sizeof(message);
	nlh = nlmsg_put(skb_1, 0, 0, 0, MAX_MSGSIZE, 0);
	NETLINK_CB(skb_1).portid = 0;
	NETLINK_CB(skb_1).dst_group = 0;
//	message[slen] = '\0';
	memcpy(NLMSG_DATA(nlh), &message, slen);
	ret = netlink_unicast(nl_sk, skb_1, pid, MSG_DONTWAIT);

	if (!ret) {
		/*kfree_skb(skb_1); */
		LOG_ERROR("send msg from kernel to usespace failed ret 0x%x\n",
		       ret);
	}
}

void nl_data_ready(struct sk_buff *__skb)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	char str[100] = {'\0'};
	skb = skb_get(__skb);

	if (skb->len >= NLMSG_SPACE(0)) {
		nlh = nlmsg_hdr(skb);
		memcpy(str, NLMSG_DATA(nlh), sizeof(str));
		pid = nlh->nlmsg_pid;
		kfree_skb(skb);
	}
	LOG_INFO("netlink socket, pid =%d, msg = %d\n", pid, str[0]);
}

int netlink_init(void)
{
	struct netlink_kernel_cfg netlink_cfg;
	memset(&netlink_cfg, 0, sizeof(struct netlink_kernel_cfg));
	netlink_cfg.groups = 0;
	netlink_cfg.flags = 0;
	netlink_cfg.input = nl_data_ready;
	netlink_cfg.cb_mutex = NULL;
	nl_sk = netlink_kernel_create(&init_net, NETLINK_TEST, &netlink_cfg);

	if (!nl_sk) {
		LOG_ERROR("create netlink socket error\n");
		return 1;
	}

	return 0;
}

void netlink_exit(void)
{
	if (nl_sk != NULL) {
		netlink_kernel_release(nl_sk);
		nl_sk = NULL;
	}

	LOG_INFO("self module exited\n");
}

void *kzalloc_retry(size_t size, int retry)
{
	void *p = NULL;
	while (retry > 0) {
		retry--;
		p = kzalloc(size, GFP_KERNEL);
		if (p)
			return p;
	}
	return NULL;
}

void kzalloc_free(void *p)
{
	kfree(p);
}

void *kvzalloc_retry(size_t size, int retry)
{
	void *p = NULL;

	while (retry > 0) {
		retry--;
		p = kvzalloc(size, GFP_KERNEL);
		if (p)
			return p;
	}
	return NULL;
}

void kvzalloc_free(void *p)
{
	kvfree(p);
}

xiaomi_touch_data_t *get_xiaomi_touch_data(s8 touch_id)
{
	if (IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_ERROR("touch id %d hasn't select, return!", touch_id);
		return NULL;
	} else if (!(xiaomi_touch.panel_register_mask & (1 << touch_id))) {
		LOG_ERROR("panel in touch id %d hasn't register, return!", touch_id);
		return NULL;
	}
	return &xiaomi_touch.xiaomi_touch_data[touch_id];
}

xiaomi_touch_driver_param_t *get_xiaomi_touch_driver_param(s8 touch_id)
{
	if (IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_ERROR("touch id %d hasn't select, return!", touch_id);
		return NULL;
	} else if (!(xiaomi_touch.panel_register_mask & (1 << touch_id))) {
		LOG_ERROR("panel in touch id %d hasn't register, return!", touch_id);
		return NULL;
	}
	return &xiaomi_touch.xiaomi_touch_driver_param[touch_id];
}

void notify_xiaomi_touch(xiaomi_touch_data_t *xiaomi_touch_data, enum poll_notify_type type)
{
	private_data_t *client_private_data = NULL;
	if (!xiaomi_touch_data)
		return;
	spin_lock(&xiaomi_touch_data->private_data_lock);
	list_for_each_entry_rcu(client_private_data, &xiaomi_touch_data->private_data_list, node) {
		LOG_DEBUG("notify xiaomi-touch data update, client private data is %p", client_private_data);
		wake_up_all(&client_private_data->poll_wait_queue_head);
		if (type == COMMON_DATA_NOTIFY) {
			wake_up_all(&client_private_data->poll_wait_queue_head_for_cmd);
		} else if (type == FRAME_DATA_NOTIFY) {
			wake_up_all(&client_private_data->poll_wait_queue_head_for_frame);
		} else if (type == RAW_DATA_NOTIFY) {
			wake_up_all(&client_private_data->poll_wait_queue_head_for_raw);
		}
	}
	spin_unlock(&xiaomi_touch_data->private_data_lock);
}

void add_common_data_to_buf(s8 touch_id, enum common_data_cmd cmd, enum common_data_mode mode, int length, int *data)
{
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);

	common_data_t *common_data = NULL;

	if (!xiaomi_touch_data)
		return;

	LOG_DEBUG("add touch id %d common data to %d", touch_id, atomic_read(&xiaomi_touch_data->common_data_buf_index));
	mutex_lock(&xiaomi_touch_data->common_data_buf_lock);
	common_data = &xiaomi_touch_data->common_data_buf[atomic_read(&xiaomi_touch_data->common_data_buf_index)];
	common_data->touch_id = touch_id;
	common_data->cmd = cmd;
	common_data->mode = mode;
	common_data->data_len = length;
	memcpy(common_data->data_buf, data, length * sizeof(s32));

	atomic_inc(&xiaomi_touch_data->common_data_buf_index);
	if (atomic_read(&xiaomi_touch_data->common_data_buf_index) >= COMMON_DATA_BUF_SIZE)
		atomic_set(&xiaomi_touch_data->common_data_buf_index, 0);

	mutex_unlock(&xiaomi_touch_data->common_data_buf_lock);
	notify_xiaomi_touch(xiaomi_touch_data, COMMON_DATA_NOTIFY);
}
EXPORT_SYMBOL_GPL(add_common_data_to_buf);


void *get_raw_data_base(s8 touch_id)
{
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
	void *base = NULL;

	if (!xiaomi_touch_data)
		return NULL;

	if (!xiaomi_touch_data->frame_data_mmap_base) {
		LOG_ERROR("touch id %d copy data failed, xiaomi_touch_data %p, base %p",
			touch_id, xiaomi_touch_data, xiaomi_touch_data->frame_data_mmap_base);
		return NULL;
	}

	base = xiaomi_touch_data->frame_data_mmap_base +
		atomic_read(&xiaomi_touch_data->frame_data_buf_index) * xiaomi_touch_data->frame_data_size;
	return base;
}
EXPORT_SYMBOL_GPL(get_raw_data_base);

void notify_raw_data_update(s8 touch_id)
{
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);

	if (!xiaomi_touch_data)
		return;

	atomic_inc(&xiaomi_touch_data->frame_data_buf_index);
	if (atomic_read(&xiaomi_touch_data->frame_data_buf_index) >= xiaomi_touch_data->frame_data_buf_size)
		atomic_set(&xiaomi_touch_data->frame_data_buf_index, 0);


	notify_xiaomi_touch(xiaomi_touch_data, FRAME_DATA_NOTIFY);
}
EXPORT_SYMBOL_GPL(notify_raw_data_update);
int get_bms_temp_common(void)
{
	struct power_supply *battery;
	union power_supply_propval prop;
	int ret;

	battery = power_supply_get_by_name("battery");
	if (!battery) {
		battery = power_supply_get_by_name("bms");
		if (!battery) {
			LOG_INFO("can't find bms and battery");
			return -INVAILD_TEMPERATURE;
		}
	}

	ret = power_supply_get_property(battery, POWER_SUPPLY_PROP_TEMP, &prop);
	if (ret) {
		LOG_INFO("can't read battery temp");
		return -INVAILD_TEMPERATURE;
	}

	return prop.intval;
}
EXPORT_SYMBOL_GPL(get_bms_temp_common);

/*
 * Temperature distribution strategy:
 *   a. Only launch when screen is on
 *   b. Update temperature for firmware at the moment the screen lights up,
 *     and then, update temperature for firmware every 2 degrees of subsequent temperature change
 *
 * Notes:
 *   VIRTUAL-SENSOR, Case temperature fitting, is a parameter that can well reflect the overall
 * temperature change of the mobile phone in the range of 25℃ ~ 53℃. It can be obtained through
 * monitoring node /sys/class/thermal/thermal_message/board_sensor_temp or through program
 * usb_get_property(USB_PROP_BOARD_TEMP, &board_temp).
 *   However, temperature changes in low-temperature scenarios require more attention for goodix.
 * And VIRTUAL-SENSOR cannot guarantee the detection accuracy below 25℃.
 *   Therefore, the monitoring object is changed to battery temperature.
 */
static int xiaomi_touch_temp_thread_func(void *data)
{
	int temp_n = 0;
	static int last_temp = 1000;
	int cur_temp0, cur_temp = 0;
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(0);
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(0);

	LOG_INFO("enter");
	if (!xiaomi_touch_data || !xiaomi_touch_driver_param ||
			!xiaomi_touch_driver_param->hardware_operation.set_thermal_temp)
		return -1;

	while (!kthread_should_stop()) {
		wait_event_interruptible(xiaomi_touch_data->temp_detect_wait_queue,
				atomic_read(&xiaomi_touch_data->temp_detect_ready));
		if(atomic_read(&xiaomi_touch_data->temp_detect_ready)) {
			cur_temp0 = get_bms_temp_common();
			cur_temp = (cur_temp0 + 5) / 10; // Rounding, in degrees Celsius
			if (abs(cur_temp0) < INVAILD_TEMPERATURE &&
					xiaomi_touch_driver_param->hardware_operation.set_thermal_temp &&
					abs(cur_temp - last_temp) >= xiaomi_touch_driver_param->hardware_param.temp_change_value) {
				xiaomi_touch_driver_param->hardware_operation.set_thermal_temp(cur_temp, false);
				add_common_data_to_buf(0, SET_CUR_VALUE, DATA_MODE_137, 1, &cur_temp);
				last_temp = cur_temp;
			}

			/*
			 * The update rules for battery temperature are as follows:
			 * a. updates every 5s when charging; updates every 30s when not charging
			 * b. if the system enters sleep mode when screen-off, the temperature will not be updated until the system is awakened.
			 */
			if (xiaomi_touch.charging_status)
				temp_n = 5;
			else
				temp_n = 10; // 30;

			LOG_DEBUG("cur_temp0:%d, last_temp:%d, sleep %dms", cur_temp0, last_temp, temp_n * 1000);
			msleep(temp_n * 1000);
		}
	}
	LOG_INFO("exit");

	return 0;
}

void enable_temperature_detection_func(bool is_resume)
{
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(0);
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(0);

	LOG_VERBOSE("enter");
	if (!xiaomi_touch_data || !xiaomi_touch_driver_param ||
			!xiaomi_touch_driver_param->hardware_operation.set_thermal_temp)
		return;

	if (is_resume) {
		atomic_set(&xiaomi_touch_data->temp_detect_ready, 1);
		LOG_DEBUG("start detect temperature");
	} else {
		atomic_set(&xiaomi_touch_data->temp_detect_ready, 0);
		LOG_DEBUG("stop detect temperature");
	}

	wake_up_interruptible(&xiaomi_touch_data->temp_detect_wait_queue);
}
EXPORT_SYMBOL_GPL(enable_temperature_detection_func);

int register_touch_panel(struct device *dev, s8 touch_id, hardware_param_t *hardware_param, hardware_operation_t *hardware_operation)
{
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = NULL;
	xiaomi_touch_data_t *xiaomi_touch_data = NULL;
	u32 alloc_size = 0;

	LOG_INFO("enter");
	if (IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_ERROR("touch id error, please check it!");
		return -1;
	}

	if (xiaomi_touch.panel_register_mask & (1 << touch_id)) {
		LOG_ERROR("touch id %d has register, stop register again!", touch_id);
		return -1;
	}

	/* init panel driver param */
	if (hardware_param) {
		xiaomi_touch_driver_param = &xiaomi_touch.xiaomi_touch_driver_param[touch_id];
		xiaomi_touch_driver_param->touch_id = touch_id;
		memcpy(&xiaomi_touch_driver_param->hardware_param, hardware_param, sizeof(hardware_param_t));
	} else {
		LOG_ERROR("param is null");
		return -1;
	}
	if (hardware_operation)
		memcpy(&xiaomi_touch_driver_param->hardware_operation, hardware_operation, sizeof(hardware_operation_t));

	/* create proc node */
	xiaomi_touch_create_proc(xiaomi_touch_driver_param);

	/* init xiaomi touch driver param */
	xiaomi_touch_data = &xiaomi_touch.xiaomi_touch_data[touch_id];
	memset(xiaomi_touch_data, 0, sizeof(xiaomi_touch_data_t));
	xiaomi_touch_data->touch_id = touch_id;
	spin_lock_init(&xiaomi_touch_data->private_data_lock);
	mutex_init(&xiaomi_touch_data->common_data_buf_lock);
	xiaomi_touch_data->frame_data_size = hardware_param->frame_data_page_size * PAGE_SIZE;
	xiaomi_touch_data->frame_data_buf_size = hardware_param->frame_data_buf_size;
	xiaomi_touch_data->raw_data_size = hardware_param->raw_data_page_size * PAGE_SIZE;
	xiaomi_touch_data->raw_data_buf_size = hardware_param->raw_data_buf_size;

	/* The temperature detection function is enabled by default when machine startup */
	atomic_set(&xiaomi_touch_data->temp_detect_ready, 1);

	/* alloc mmap memory */
	xiaomi_touch_data->frame_data_mmap_phy_base = 0;
	alloc_size = xiaomi_touch_data->frame_data_size * xiaomi_touch_data->frame_data_buf_size;
	LOG_DEBUG("alloc size = %d, frame data size %d, frame data page size %d, frame data buf size %d",
		alloc_size, xiaomi_touch_data->frame_data_size,
		hardware_param->frame_data_page_size, hardware_param->frame_data_buf_size);
	if (alloc_size) {
		xiaomi_touch_data->frame_data_mmap_base = kzalloc_retry(alloc_size, 3);
		if (!xiaomi_touch_data->frame_data_mmap_base) {
			LOG_ERROR("touch id %d alloc frame data memory failed!", touch_id);
			return -1;
		}
		LOG_DEBUG("frame data base %p, size %u", xiaomi_touch_data->frame_data_mmap_base, alloc_size);
		xiaomi_touch_data->frame_data_mmap_phy_base = virt_to_phys(xiaomi_touch_data->frame_data_mmap_base);
	}

	xiaomi_touch_data->raw_data_mmap_phy_base = 0;
	alloc_size = xiaomi_touch_data->raw_data_size * xiaomi_touch_data->raw_data_buf_size;

	LOG_DEBUG("alloc size = %d, raw data size %d, raw data page size %d, raw data buf size %d",
		alloc_size, xiaomi_touch_data->raw_data_size,
		hardware_param->raw_data_page_size, hardware_param->raw_data_buf_size);
	if (alloc_size) {
		xiaomi_touch_data->raw_data_mmap_base = kzalloc_retry(alloc_size, 3);
		if (!xiaomi_touch_data->raw_data_mmap_base) {
			LOG_ERROR("touch id %d alloc raw data memory failed!", touch_id);
			return -1;
		}
		LOG_DEBUG("raw data base %p, size %u", xiaomi_touch_data->raw_data_mmap_base, alloc_size);
		xiaomi_touch_data->raw_data_mmap_phy_base = virt_to_phys(xiaomi_touch_data->raw_data_mmap_base);
	}

	xiaomi_touch_data->event_wq = alloc_workqueue("xiaomi-touch-event-queue",
		WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
	if (!xiaomi_touch_data->event_wq) {
		LOG_ERROR("ERROR: Cannot create work thread");
		return -1;
	}

	/* alloc poll data memory */
	xiaomi_touch_data->poll_data = kzalloc_retry(sizeof(htc_ic_polldata_t), 3);
	if (!xiaomi_touch_data->poll_data) {
		LOG_ERROR("alloc poll data memory is failed!");
		return -1;
	}

	/* init list */
	INIT_LIST_HEAD(&xiaomi_touch_data->private_data_list);

	/* print hardware param*/
	if (hardware_operation && hardware_param) {

		if (hardware_operation->ic_get_lockdown_info) {
			hardware_operation->ic_get_lockdown_info(hardware_param->lockdown_info);
		}
		LOG_INFO("x_resolution=%d, y_resolution=%d, rx_num=%d, tx_num=%d, "
			"super_resolution_factor=%d, "
			"frame_data_page_size=%d, frame_data_buf_size=%d, "
			"raw_data_page_size=%d, raw_data_buf_size=%d, "
			"config_file_name=%s, driver_version=%s, fw_version=%s, "
			"lockdown={0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}",
			hardware_param->x_resolution, hardware_param->y_resolution, hardware_param->rx_num, hardware_param->tx_num,
			hardware_param->super_resolution_factor,
			hardware_param->frame_data_page_size, hardware_param->frame_data_buf_size,
			hardware_param->raw_data_page_size, hardware_param->raw_data_buf_size,
			hardware_param->config_file_name, hardware_param->driver_version, hardware_param->fw_version,
			hardware_param->lockdown_info[0], hardware_param->lockdown_info[1], hardware_param->lockdown_info[2], hardware_param->lockdown_info[3],
			hardware_param->lockdown_info[4], hardware_param->lockdown_info[5], hardware_param->lockdown_info[6], hardware_param->lockdown_info[7]);
	}
	/* init touch mode */
	xiaomi_touch_init_touch_mode(touch_id, dev);

	/* complete register */
	xiaomi_touch.panel_register_mask |= (1 << touch_id);
	LOG_INFO("current panel_register_mask is %d", xiaomi_touch.panel_register_mask);

	/* create a thread for temp detect */
	if (hardware_operation && hardware_operation->set_thermal_temp) {
		LOG_INFO("startup temperature detect thread");
		xiaomi_touch_temp_thread = kthread_create(xiaomi_touch_temp_thread_func, NULL, "xiaomi_touch_temp_thread");
		if (IS_ERR(xiaomi_touch_temp_thread)) {
			LOG_ERROR("Failed to create xiaomi_touch_temp_thread");
			goto err_out;
		}
		init_waitqueue_head(&xiaomi_touch_data->temp_detect_wait_queue);
		wake_up_process(xiaomi_touch_temp_thread);
	}

	return 0;

err_out:
	if(xiaomi_touch_temp_thread != NULL) {
		kthread_stop(xiaomi_touch_temp_thread);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(register_touch_panel);

void unregister_touch_panel(s8 touch_id)
{
	xiaomi_touch_data_t *xiaomi_touch_data = NULL;
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = NULL;
	if (!(xiaomi_touch.panel_register_mask & (1 << touch_id))) {
		LOG_ERROR("touch id %d didn't register, break unregister!", touch_id);
		return;
	}

	/* free mmap memory */
	xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
	if (xiaomi_touch_data) {
		kzalloc_free(xiaomi_touch_data->frame_data_mmap_base);
		xiaomi_touch_data->frame_data_mmap_base = NULL;
		xiaomi_touch_data->frame_data_mmap_phy_base = 0;

		kzalloc_free(xiaomi_touch_data->raw_data_mmap_base);
		xiaomi_touch_data->raw_data_mmap_base = NULL;
		xiaomi_touch_data->raw_data_mmap_phy_base = 0;
	}

	/* remove proc node */
	xiaomi_touch_remove_proc(touch_id);

	/* clear driver param */
	xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
	memset(&xiaomi_touch_driver_param->hardware_param, 0, sizeof(hardware_param_t));
	memset(&xiaomi_touch_driver_param->hardware_operation, 0, sizeof(hardware_operation_t));

	/*free poll data memory*/
	if (xiaomi_touch_data) {
		kzalloc_free(xiaomi_touch_data->poll_data);
		xiaomi_touch_data->poll_data = NULL;
	}

	/* complete unregister */
	xiaomi_touch.panel_register_mask &= ~(1 << touch_id);
	LOG_INFO("current panel_register_mask is %d", xiaomi_touch.panel_register_mask);

	/* free temp detect thread */
	if(xiaomi_touch_temp_thread != NULL) {
		atomic_set(&xiaomi_touch_data->temp_detect_ready, 0);
		// kthread_stop(xiaomi_touch_temp_thread); /* Optimize restart time */
		LOG_INFO("stop detect temperature");
	}
}
EXPORT_SYMBOL_GPL(unregister_touch_panel);

#if defined(CONFIG_DRM)
static void xiaomi_touch_resume_work(struct work_struct *work)
{
	xiaomi_touch_data_t *xiaomi_touch_data = container_of(work, xiaomi_touch_data_t, resume_work);
	s8 touch_id = xiaomi_touch_data->touch_id;
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
#ifdef TOUCH_THP_SUPPORT
	int value = 0;
#endif

	LOG_INFO("touch id %d enter", touch_id);
	if (!xiaomi_touch_data || !xiaomi_touch_driver_param)
		return;
	if (!xiaomi_touch_data->is_suspend) {
		LOG_ERROR("touch id %d is resume, stop resume", touch_id);
		return;
	}
	XIAOMI_TOUCH_UTC_PRINT("");
#ifdef TOUCH_SENSORHUB_SUPPORT
	xiaomi_notify_sensorhub_enable(touch_id, false);
#endif
	if (xiaomi_touch_driver_param->hardware_operation.ic_resume_suspend) {
		xiaomi_touch_driver_param->hardware_operation.ic_resume_suspend(true, xiaomi_get_gesture_type(touch_id));
	}
#ifdef TOUCH_THP_SUPPORT
	add_common_data_to_buf(touch_id, SET_CUR_VALUE, DATA_MODE_27, 1, &value);
	add_common_data_to_buf(touch_id, SET_CUR_VALUE, DATA_MODE_54, 1, &xiaomi_touch.charging_status);
#endif
	/*reset charge state*/
	if (xiaomi_touch_driver_param->hardware_operation.ic_set_charge_state) {
		xiaomi_touch_driver_param->hardware_operation.ic_set_charge_state(xiaomi_touch.charging_status);
	}
	xiaomi_touch_data->is_suspend = false;
	/* other resume to do */

}

static void xiaomi_touch_suspend_work(struct work_struct *work)
{
	xiaomi_touch_data_t *xiaomi_touch_data = container_of(work, xiaomi_touch_data_t, suspend_work);
	s8 touch_id = xiaomi_touch_data->touch_id;
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
#ifdef TOUCH_THP_SUPPORT
	int value = 1;
#endif
#ifdef TOUCH_SENSORHUB_SUPPORT
#ifndef CONFIG_TOUCH_FACTORY_BUILD
	int ssh_value = XIAOMI_TOUCH_SENSORHUB_NONUIENABLE;
#endif
#endif

	LOG_INFO("touch id %d enter", touch_id);
	if (!xiaomi_touch_data || !xiaomi_touch_driver_param)
		return;
	if (xiaomi_touch_data->is_suspend) {
		LOG_ERROR("touch id %d is suspend, stop suspend", touch_id);
		return;
	}
	XIAOMI_TOUCH_UTC_PRINT("");
	if (xiaomi_touch_driver_param->hardware_operation.ic_resume_suspend) {
		xiaomi_touch_driver_param->hardware_operation.ic_resume_suspend(false, xiaomi_get_gesture_type(touch_id));
	}
#ifdef TOUCH_THP_SUPPORT
	add_common_data_to_buf(touch_id, SET_CUR_VALUE, DATA_MODE_27, 1, &value);
#ifdef TOUCH_SENSORHUB_SUPPORT
	xiaomi_notify_sensorhub_enable(touch_id, true);
#ifndef CONFIG_TOUCH_FACTORY_BUILD
	if (!xiaomi_touch_data->ssh_status && (driver_get_touch_mode(touch_id, DATA_MODE_10) != 0) && (driver_get_touch_mode(touch_id, DATA_MODE_17) == 2)) {
		LOG_INFO("sleep by nonui mode,need hold wakeup nonui sensor to notify ap reenter gesture");
		add_common_data_to_buf(touch_id, SET_CUR_VALUE, DATA_MODE_27, 1, &ssh_value);
	}
#endif
#endif
#endif
	xiaomi_touch_data->is_suspend = true;
	/* other suspend to do */

}

static void xiaomi_drm_panel_notifier_callback(enum panel_event_notifier_tag tag,
		struct panel_event_notification *notification, void *client_data)
{
	long touch_id = (long)client_data;
	if (!notification || IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_ERROR("Invalid data. notification %p, touch_id %ld", notification, touch_id);
		return;
	}
	if (notification->notif_type < 4)
		LOG_INFO("Notification type:%d, early_trigger:%d", notification->notif_type, notification->notif_data.early_trigger);

	switch (notification->notif_type) {
	case DRM_PANEL_EVENT_UNBLANK:
		if (notification->notif_data.early_trigger == 1) {
			LOG_INFO("FB_BLANK_UNBLAN");
			schedule_resume_suspend_work((s8)touch_id, true);
		}
		break;
	case DRM_PANEL_EVENT_BLANK:
	case DRM_PANEL_EVENT_BLANK_LP:
		if (notification->notif_data.early_trigger == 0) {
			LOG_INFO("FB_BLANK %s", notification->notif_type == DRM_PANEL_EVENT_BLANK_LP ? "POWER DOWN" : "LP");
			schedule_resume_suspend_work((s8)touch_id, false);
		}
		break;
	case DRM_PANEL_EVENT_FPS_CHANGE:
		break;
	default:
		LOG_ERROR("notification serviced :%d", notification->notif_type);
		break;
	}
	if (notification->notif_type < 4)
		LOG_INFO("notification complete");
}

static void xiaomi_register_panel_notifier_work(struct work_struct *work)
{
	xiaomi_touch_data_t *xiaomi_touch_data = container_of(work, xiaomi_touch_data_t, panel_notifier_register_work.work);
	static int check_count = 0;
	struct drm_panel *panel = NULL;
	struct device_node *node;
	int count = 0;
	int i = 0;
	long touch_id = -1;
	char *property_name = "panel";

	LOG_INFO("Start register panel notifier");
	count = of_count_phandle_with_args(xiaomi_touch_data->dev->of_node, property_name, NULL);
	for (i = 0; i < count; i++) {
		node = of_parse_phandle(xiaomi_touch_data->dev->of_node, property_name, i);
		panel = of_drm_find_panel(node);
		if (!IS_ERR(panel)) {
			break;
		} else {
			panel = NULL;
			of_node_put(node);
		}
	}

	if (!panel) {
		LOG_ERROR("Failed to register panel notifier, try again");
		if (check_count++ < 5)
			schedule_delayed_work(&xiaomi_touch_data->panel_notifier_register_work, msecs_to_jiffies(5000));
		else {
			LOG_ERROR("Failed to register panel notifier, not try");
		}
		return;
	}

	for (i = 0; i < MAX_TOUCH_PANEL_COUNT; i++) {
		if (xiaomi_touch_data == &xiaomi_touch.xiaomi_touch_data[i]) {
			touch_id = i;
			break;
		}
	}

	if (touch_id < 0) {
		LOG_ERROR("can't not find this touch id!");
		return;
	}

#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	xiaomi_touch_data->notifier_cookie = panel_event_notifier_register(xiaomi_touch_data->panel_event_notifier_tag,
		xiaomi_touch_data->panel_event_notifier_client, panel,
		&xiaomi_drm_panel_notifier_callback, (void *)touch_id);
#endif
	of_node_put(node);
	if (IS_ERR(xiaomi_touch_data->notifier_cookie)) {
		LOG_ERROR("Failed to register for panel events");
	}
}

#endif

void schedule_resume_suspend_work(s8 touch_id, bool resume_work)
{
#if defined(CONFIG_DRM)
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
	bool queue_work_result = false;
	if (!xiaomi_touch_data) {
		LOG_ERROR("xiaomi_touch_data is NULL!");
		return;
	}

	flush_workqueue(xiaomi_touch_data->event_wq);
	if (resume_work) {
		queue_work_result = queue_work(xiaomi_touch_data->event_wq, &xiaomi_touch_data->resume_work);
	} else {
		queue_work_result = queue_work(xiaomi_touch_data->event_wq, &xiaomi_touch_data->suspend_work);
	}

	if (!queue_work_result) {
		LOG_ERROR("queue %s work failed, retry", resume_work ? "resume" : "suspend");
	}

#endif
}
EXPORT_SYMBOL_GPL(schedule_resume_suspend_work);

/**
 * this function must be called after register_touch_panel()
*/
void xiaomi_register_panel_notifier(struct device *dev, s8 touch_id,
		int panel_event_notifier_tag, int panel_event_notifier_client)
{
#if defined(CONFIG_DRM)
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
	if (xiaomi_touch_data == NULL)
		return;
	xiaomi_touch_data->dev = dev;
	xiaomi_touch_data->panel_event_notifier_tag = panel_event_notifier_tag;
	xiaomi_touch_data->panel_event_notifier_client = panel_event_notifier_client;
	INIT_DELAYED_WORK(&xiaomi_touch_data->panel_notifier_register_work, xiaomi_register_panel_notifier_work);
	INIT_WORK(&xiaomi_touch_data->suspend_work, xiaomi_touch_suspend_work);
	INIT_WORK(&xiaomi_touch_data->resume_work, xiaomi_touch_resume_work);
	schedule_delayed_work(&xiaomi_touch_data->panel_notifier_register_work, msecs_to_jiffies(0));
#endif
}
EXPORT_SYMBOL_GPL(xiaomi_register_panel_notifier);

void xiaomi_unregister_panel_notifier(struct device *dev, s8 touch_id)
{
#if defined(CONFIG_DRM)
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
	if (xiaomi_touch_data == NULL)
		return;
	cancel_delayed_work_sync(&xiaomi_touch_data->panel_notifier_register_work);
	cancel_work_sync(&xiaomi_touch_data->suspend_work);
	cancel_work_sync(&xiaomi_touch_data->resume_work);

#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	if (!IS_ERR(xiaomi_touch_data->notifier_cookie))
		panel_event_notifier_unregister(xiaomi_touch_data->notifier_cookie);
#endif
#endif
}
EXPORT_SYMBOL_GPL(xiaomi_unregister_panel_notifier);

static int xiaomi_get_charging_status(void)
{
	struct power_supply *usb_psy;
	struct power_supply *dc_psy;
	union power_supply_propval val;
	int rc = 0;

	dc_psy = power_supply_get_by_name("wireless");
	if (dc_psy) {
		rc = power_supply_get_property(dc_psy, POWER_SUPPLY_PROP_ONLINE, &val);
		if (rc < 0)
			LOG_ERROR("Couldn't get DC online status, rc=%d\n", rc);
		else if (val.intval == 1)
			return CHARGING | WIRELESS_CHARGING;
	}

	usb_psy = power_supply_get_by_name("usb");
	if (usb_psy) {
		rc = power_supply_get_property(usb_psy, POWER_SUPPLY_PROP_ONLINE, &val);
		if (rc < 0)
			LOG_ERROR("Couldn't get usb online status, rc=%d\n", rc);
		else if (val.intval == 1)
			return CHARGING | WIRED_CHARGING;
	}
	return NOT_CHARGING;
}

static void xiaomi_power_supply_work(struct work_struct *work)
{
	xiaomi_touch_t *xiaomi_touch = container_of(work, xiaomi_touch_t, power_supply_work);
	int charging_status;
	int i = 0;
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param;

	charging_status = xiaomi_get_charging_status();
	if (charging_status != xiaomi_touch->charging_status || xiaomi_touch->charging_status < 0) {
		xiaomi_touch->charging_status = charging_status;
	for (i = 0; i < MAX_TOUCH_PANEL_COUNT; i++) {
		xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(i);
		if (!xiaomi_touch_driver_param)
			break;
#ifdef TOUCH_THP_SUPPORT
		add_common_data_to_buf(i, SET_CUR_VALUE, DATA_MODE_54, 1, &charging_status);
#endif
		if (xiaomi_touch_driver_param->hardware_operation.ic_set_charge_state) {
			xiaomi_touch_driver_param->hardware_operation.ic_set_charge_state(charging_status);
		}
	}
	}
}

static int xiaomi_power_supply_notifier_callback(struct notifier_block *nb, unsigned long event, void *ptr)
{
	schedule_work(&xiaomi_touch.power_supply_work);
	return 0;
}

static void xiaomi_register_power_supply_event(xiaomi_touch_t *xiaomi_touch)
{
	int retval = 0;

	if (xiaomi_touch == NULL)
		return;
	xiaomi_touch->charging_status = -1;
	xiaomi_touch->power_supply_notifier.notifier_call = xiaomi_power_supply_notifier_callback;
	retval = power_supply_reg_notifier(&xiaomi_touch->power_supply_notifier);
	if (retval < 0) {
		LOG_ERROR("error:%d\n", retval);
		return;
	}
	INIT_WORK(&xiaomi_touch->power_supply_work, xiaomi_power_supply_work);
}

static void xiaomi_unregister_power_supply_event(void)
{
	cancel_work_sync(&xiaomi_touch.power_supply_work);
	power_supply_unreg_notifier(&xiaomi_touch.power_supply_notifier);
}

static int xiaomi_touch_probe(struct platform_device *pdev)
{
	LOG_INFO("enter");
#ifdef TOUCH_KNOCK_SUPPORT
	knock_node_init();
#endif
	memset(&xiaomi_touch, 0, sizeof(xiaomi_touch_t));
	xiaomi_touch_sys_init();
	xiaomi_touch_operation_init(&xiaomi_touch);
	xiaomi_register_power_supply_event(&xiaomi_touch);
	netlink_init();
	xiaomi_touch_evdev_init(&xiaomi_touch);
	LOG_INFO("over");

	return 0;
}

static int xiaomi_touch_remove(struct platform_device *pdev)
{
	LOG_INFO("enter");
	xiaomi_touch_evdev_remove();
    netlink_exit();
	xiaomi_unregister_power_supply_event();
	xiaomi_touch_operation_remove();
	xiaomi_touch_sys_remove();
	memset(&xiaomi_touch, 0, sizeof(xiaomi_touch_t));
#ifdef TOUCH_KNOCK_SUPPORT
	knock_node_release();
#endif
	LOG_INFO("over");
	return 0;
}

static const struct of_device_id xiaomi_touch_of_match[] = {
	{ .compatible = "xiaomi-touch", },
	{ },
};
static struct platform_driver xiaomi_touch_device_driver = {
	.probe		= xiaomi_touch_probe,
	.remove		= xiaomi_touch_remove,
	.driver		= {
		.name	= "xiaomi-touch",
		.of_match_table = of_match_ptr(xiaomi_touch_of_match),
	}
};

static int __init xiaomi_touch_init(void)
{
	return platform_driver_register(&xiaomi_touch_device_driver);
}

static void __exit xiaomi_touch_exit(void)
{
	platform_driver_unregister(&xiaomi_touch_device_driver);
}

MODULE_LICENSE("GPL");

module_init(xiaomi_touch_init);
module_exit(xiaomi_touch_exit);
