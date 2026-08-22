/*
 * Copyright (c) 2023 Xiaomi, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Xiaomi, Inc.
 */

#include <linux/device.h>
#include "xiaomi_touch.h"

#define SENSITIVE_EVENT_BUF_SIZE    (10)
#define TOUCH_ID    0
#define MAX_BUF_SIZE (sizeof(common_data_t))

enum MI_TP_LOG_LEVEL current_log_level = MI_TP_LOG_INFO;
EXPORT_SYMBOL_GPL(current_log_level);

typedef struct abnormal_event {
	u16 type;
	u16 code;
	u16 value;
} abnormal_event_t;

struct {
	int top;
	int bottom;
	bool full_flag;
	abnormal_event_t abnormal_event[SENSITIVE_EVENT_BUF_SIZE];
} abnormal_event_buf;

static DEFINE_MUTEX(gesture_single_tap_mutex);
static DEFINE_MUTEX(gesture_double_tap_mutex);
static DEFINE_MUTEX(fod_press_status_mutex);
static DEFINE_MUTEX(abnormal_event_mutex);
static DEFINE_MUTEX(palm_mutex);

static DEFINE_MUTEX(thp_ic_mutex);
static DEFINE_MUTEX(thp_ic_read_data_mutex);
static DEFINE_MUTEX(thp_ic_write_data_mutex);

static struct device *xiaomi_touch_dev = NULL;
static struct class *xiaomi_touch_class = NULL;
static struct attribute_group xiaomi_touch_attrs;

static int fod_press_status_value = 0;

static int doze_analysis_result;
static int is_enable_touchraw = 1;
static int palm_value;
static u64 ic_buffer_addr;
static int touch_finger_status = 0;

static common_data_t thp_ic_cmd_data_common_data;
static int vendor_input = 1;

#define CREATE_ATTR(name, show_func, store_func) \
	static ssize_t show_##name(struct device *dev, struct device_attribute *attr, char *buf)show_func \
	static ssize_t store_##name(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)store_func \
	static DEVICE_ATTR(name, (S_IRUGO | S_IWUSR | S_IWGRP), show_##name, store_##name);

static int analy_poll_data(char* poll_data_buf, char* buf)
{
	int count = 0;
	xiaomi_touch_data_t* xiaomi_touch_data = get_xiaomi_touch_data(TOUCH_ID);

	if (!poll_data_buf || !buf)
		return -1;
	memset(buf, 0x00, PAGE_SIZE);
	memcpy((char* )xiaomi_touch_data->poll_data, poll_data_buf, sizeof(struct htc_ic_polldata));

	count += snprintf(buf, PAGE_SIZE, "protocol_version:    %hu\n", xiaomi_touch_data->poll_data->protocol_version);
	count += snprintf(buf + count, PAGE_SIZE, "frame_len:    %hu\n", xiaomi_touch_data->poll_data->frame_len);
	count += snprintf(buf + count, PAGE_SIZE, "ic_fw_v:    %hu\n", xiaomi_touch_data->poll_data->ic_fw_v);
	count += snprintf(buf + count, PAGE_SIZE, "ic_name:    %hu\n", xiaomi_touch_data->poll_data->ic_name);
	count += snprintf(buf + count, PAGE_SIZE, "ic_project_name:    %hu\n", xiaomi_touch_data->poll_data->ic_project_name);
	count += snprintf(buf + count, PAGE_SIZE, "ic_supplier_name:    %hu\n", xiaomi_touch_data->poll_data->ic_supplier_name);
	count += snprintf(buf + count, PAGE_SIZE, "pitch_size_y:    %d\n", xiaomi_touch_data->poll_data->pitch_size_y);
	count += snprintf(buf + count, PAGE_SIZE, "pitch_size_x:    %d\n", xiaomi_touch_data->poll_data->pitch_size_x);
	count += snprintf(buf + count, PAGE_SIZE, "numCol:    %d\n", xiaomi_touch_data->poll_data->numCol);
	count += snprintf(buf + count, PAGE_SIZE, "numRow:    %d\n", xiaomi_touch_data->poll_data->numRow);
	count += snprintf(buf + count, PAGE_SIZE, "packaging_factory:    %hu\n", xiaomi_touch_data->poll_data->packaging_factory);
	count += snprintf(buf + count, PAGE_SIZE, "wafe_factory:    %hu\n", xiaomi_touch_data->poll_data->wafe_factory);
	count += snprintf(buf + count, PAGE_SIZE, "x_resolution:    %hu\n", xiaomi_touch_data->poll_data->x_resolution);
	count += snprintf(buf + count, PAGE_SIZE, "y_resolution:    %hu\n", xiaomi_touch_data->poll_data->y_resolution);
	count += snprintf(buf + count, PAGE_SIZE, "lockdown_info:    %llu\n", xiaomi_touch_data->poll_data->lockdown_info);
	count += snprintf(buf + count, PAGE_SIZE, "frame_data_type:    %d\n", xiaomi_touch_data->poll_data->frame_data_type);
	count += snprintf(buf + count, PAGE_SIZE, "mutual_len:    %hu\n", xiaomi_touch_data->poll_data->mutual_len);
	count += snprintf(buf + count, PAGE_SIZE, "slef1_len:    %hu\n", xiaomi_touch_data->poll_data->slef1_len);
	count += snprintf(buf + count, PAGE_SIZE, "slef2_len:    %hu\n", xiaomi_touch_data->poll_data->slef2_len);

	return count;
}

int notify_gesture_double_tap(void)
{
	mutex_lock(&gesture_double_tap_mutex);
	sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "gesture_double_tap_state");
	mutex_unlock(&gesture_double_tap_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(notify_gesture_double_tap);

int notify_gesture_single_tap(void)
{
	mutex_lock(&gesture_single_tap_mutex);
	sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "gesture_single_tap_state");
	mutex_unlock(&gesture_single_tap_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(notify_gesture_single_tap);

int update_fod_press_status(int value)
{
	mutex_lock(&fod_press_status_mutex);

	if (value != fod_press_status_value) {
		LOG_INFO("value:%d", value);
		fod_press_status_value = value;
		sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "fod_press_status");
	}

	mutex_unlock(&fod_press_status_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(update_fod_press_status);

int update_palm_sensor_value(int value)
{
	mutex_lock(&palm_mutex);
	if (value != palm_value) {
		LOG_ERROR("value:%d", value);
		palm_value = value;
		sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "palm_sensor");
	}

	mutex_unlock(&palm_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(update_palm_sensor_value);

struct class *get_xiaomi_touch_class(void)
{
	return xiaomi_touch_class;
}
EXPORT_SYMBOL_GPL(get_xiaomi_touch_class);

CREATE_ATTR(gesture_single_tap_enabled, {
		return snprintf(buf, PAGE_SIZE, "%d\n", driver_get_touch_mode(TOUCH_ID, DATA_MODE_11));
	},
	{
		int input;

		if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;

		int new_modes[DATA_MODE_33] = {0};
		new_modes[DATA_MODE_11] = input;
		driver_update_touch_mode(TOUCH_ID, new_modes, (1L << DATA_MODE_11));

		return count;
	});
CREATE_ATTR(gesture_single_tap_state, {
		return snprintf(buf, PAGE_SIZE, "%d\n", 1);
	},
	{
		return count;
	});

CREATE_ATTR(gesture_double_tap_enabled, {
		return snprintf(buf, PAGE_SIZE, "%d\n", driver_get_touch_mode(TOUCH_ID, DATA_MODE_14));
	},
	{
		int input;

		if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;

		int new_modes[DATA_MODE_33] = {0};
		new_modes[DATA_MODE_14] = input;
		driver_update_touch_mode(TOUCH_ID, new_modes, (1L << DATA_MODE_14));

		return count;
	});
CREATE_ATTR(gesture_double_tap_state, {
		return snprintf(buf, PAGE_SIZE, "%d\n", 1);
	},
	{
		return count;
	});

CREATE_ATTR(fod_press_status, {
		return snprintf(buf, PAGE_SIZE, "%d\n", fod_press_status_value);
	},
	{
		return count;
	});
CREATE_ATTR(fod_longpress_gesture_enabled, {
		return snprintf(buf, PAGE_SIZE, "%d\n", driver_get_touch_mode(TOUCH_ID, DATA_MODE_10));
	},
	{
		int input;

		if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;

		int new_modes[DATA_MODE_33] = {0};
		new_modes[DATA_MODE_10] = input;
		driver_update_touch_mode(TOUCH_ID, new_modes, (1L << DATA_MODE_10));

		/* Manually set finger up to clear touch event */
		if (!input) {
			xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
			if (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.ic_set_fod_value) {
				s32 info[FOD_VALUE_LEN];
				info[0] = 0x81; // GESTURE_TOUCH_AND_HOLD_UP_EVENT
				xiaomi_touch_driver_param->hardware_operation.ic_set_fod_value(info, FOD_VALUE_LEN);
			}
		}

		return count;
	});

CREATE_ATTR(panel_vendor, {
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		return (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.panel_vendor_read) ?
			snprintf(buf, PAGE_SIZE, "%c", xiaomi_touch_driver_param->hardware_operation.panel_vendor_read()) : 0;
	},
	{
		return count;
	});

CREATE_ATTR(panel_color, {
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		return (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.panel_color_read) ?
			snprintf(buf, PAGE_SIZE, "%c", xiaomi_touch_driver_param->hardware_operation.panel_color_read()) : 0;
	},
	{
		return count;
	});

CREATE_ATTR(panel_display, {
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		return (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.panel_display_read) ?
			snprintf(buf, PAGE_SIZE, "%c", xiaomi_touch_driver_param->hardware_operation.panel_display_read()) : 0;
	},
	{
		return count;
	});

CREATE_ATTR(touch_vendor, {
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		return (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.touch_vendor_read) ?
			snprintf(buf, PAGE_SIZE, "%c", xiaomi_touch_driver_param->hardware_operation.touch_vendor_read()) : 0;
	},
	{
		return count;
	});


CREATE_ATTR(touch_doze_analysis, {
		return snprintf(buf, PAGE_SIZE, "%d\n", doze_analysis_result);
	},
	{
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = NULL;
		int input;
		s8 touch_id = 0;
		if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;

		for (touch_id = 0; touch_id < MAX_TOUCH_PANEL_COUNT; touch_id++) {
			xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
			if (xiaomi_touch_driver_param == NULL)
				continue;
			if (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.touch_doze_analysis) {
                		doze_analysis_result = xiaomi_touch_driver_param->hardware_operation.touch_doze_analysis(input);
                	}
		}
		
		LOG_INFO("value:%d", input);
		return count;
	});

CREATE_ATTR(touch_ic_buffer, {
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		u8 *tmp_buf = NULL;
		int n = 0;
		LOG_INFO("get ic buffer from addr: 0x%08llX", ic_buffer_addr);
		if (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.get_touch_ic_buffer) {
			tmp_buf = kzalloc_retry(PAGE_SIZE, 3);
			xiaomi_touch_driver_param->hardware_operation.get_touch_ic_buffer(ic_buffer_addr, tmp_buf);
			n = snprintf(buf, PAGE_SIZE, "%s", tmp_buf);
			kzalloc_free(tmp_buf);
		}
		return n;
	},
	{
		if (sscanf(buf, "0x%llX", &ic_buffer_addr) < 0) {
			LOG_INFO("write addr error format. current addr is 0x%08llX", ic_buffer_addr);
			return -EINVAL;
		}
		LOG_INFO("write addr: 0x%08llX", ic_buffer_addr);
		return count;
	});

CREATE_ATTR(resolution_factor, {
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		int factor = 1;
		if (xiaomi_touch_driver_param)
			factor = xiaomi_touch_driver_param->hardware_param.super_resolution_factor;

		return snprintf(buf, PAGE_SIZE, "%d", factor);
	},
	{
		return count;
	});

CREATE_ATTR(abnormal_event, {
		int struct_abnormal_event_size = sizeof(abnormal_event_t);
		mutex_lock(&abnormal_event_mutex);
		if (abnormal_event_buf.bottom == abnormal_event_buf.top && !abnormal_event_buf.full_flag) {
			LOG_ERROR("buf is empty");
			mutex_unlock(&abnormal_event_mutex);
			return -1;
		}
		memcpy(buf, &abnormal_event_buf.abnormal_event[abnormal_event_buf.bottom], struct_abnormal_event_size);
		abnormal_event_buf.bottom++;
		if (abnormal_event_buf.bottom >= SENSITIVE_EVENT_BUF_SIZE) {
			abnormal_event_buf.full_flag = false;
			abnormal_event_buf.bottom = 0;
		}
		if (abnormal_event_buf.top > abnormal_event_buf.bottom || abnormal_event_buf.full_flag)
			sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "abnormal_event");
		mutex_unlock(&abnormal_event_mutex);

		return struct_abnormal_event_size;
	},
	{
		abnormal_event_t *temp_event = (abnormal_event_t *)buf;
		int struct_abnormal_event_size = sizeof(abnormal_event_t);
		if (count != struct_abnormal_event_size) {
			LOG_ERROR("fail! size = %zu, %d", count, struct_abnormal_event_size);
			return -ENODEV;
		}

		mutex_lock(&abnormal_event_mutex);
		memcpy(&abnormal_event_buf.abnormal_event[abnormal_event_buf.top], temp_event, struct_abnormal_event_size);
		abnormal_event_buf.top++;
		if (abnormal_event_buf.top >= SENSITIVE_EVENT_BUF_SIZE) {
			abnormal_event_buf.full_flag = true;
			abnormal_event_buf.top = 0;
		}
		mutex_unlock(&abnormal_event_mutex);
		sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "abnormal_event");
		return count;
	});

CREATE_ATTR(enable_touch_raw, {
		return snprintf(buf, PAGE_SIZE, "%d\n", is_enable_touchraw);
	},
	{
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(0);
		int input;

		if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;
		LOG_ERROR("enable touch raw %d", input);
		if (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.enable_touch_raw)
			xiaomi_touch_driver_param->hardware_operation.enable_touch_raw(input);

		is_enable_touchraw = input;
		return count;
	});

CREATE_ATTR(palm_sensor, {
		return snprintf(buf, PAGE_SIZE, "%d\n", palm_value);
	},
	{
		int i;
		int input;
		if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;

		for (i = 0;i < MAX_TOUCH_PANEL_COUNT;i++) {
			xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(i);
			if (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.palm_sensor_write)
				xiaomi_touch_driver_param->hardware_operation.palm_sensor_write(!!input);
		}

		add_common_data_to_buf(TOUCH_ID, SET_CUR_VALUE, DATA_MODE_26, 1, &input);
		LOG_INFO("value:%d", input);
		return count;
	});


CREATE_ATTR(touch_thp_ic_cmd, {
		return 0;
	},
	{
		s32 input[CMD_DATA_BUF_SIZE];
		int i = 0;
		int para_cnt = 0;
		int retval;
		u8 *databuf = (u8 *)&thp_ic_cmd_data_common_data.data_buf[1];
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		char *token;
		char *p;
		char *strbuf;

		mutex_lock(&thp_ic_mutex);

		memset(input, 0x00, sizeof(int) * CMD_DATA_BUF_SIZE);
		strbuf = (unsigned char *)kzalloc(count + 1, GFP_KERNEL);
		memcpy(strbuf, buf, count);
		strbuf[count] = '\0';

		p = strbuf;
		token = strbuf;
		while(token != NULL){
			token = strsep(&p, ", ");
			if (token != NULL) {
				retval = kstrtoint(token, 0, &input[i]);
				if (retval || (i == CMD_DATA_BUF_SIZE - 1)) {
					LOG_ERROR("input[%d] value format error, retval = %d; or value count %d overflow, please check", i, retval, i);
					break;
				}
				++i;
			}
		}
		para_cnt = i;
		for (i = 0; i < para_cnt; ++i) {
			LOG_DEBUG("input[%d] = 0x%x", i, input[i]);
		}

		LOG_INFO("user_cmd:%d, mode:%d, addr:%d, data_len:%d", input[0], input[1], input[2], input[para_cnt - 1]);

		if (input[0] != SET_THP_IC_CUR_VALUE && input[0] != GET_THP_IC_CUR_VALUE) {
			LOG_ERROR("unsupport cmd!!");
			mutex_unlock(&thp_ic_mutex);
			return -1;
		}

		if (para_cnt < 4 || para_cnt > CMD_DATA_BUF_SIZE) {
			LOG_ERROR("input format is error!!");
			mutex_unlock(&thp_ic_mutex);
			return -1;
		}

		memset(&thp_ic_cmd_data_common_data, 0x00, sizeof(common_data_t));
		thp_ic_cmd_data_common_data.cmd = (u8)input[0];
		thp_ic_cmd_data_common_data.mode = (u16)input[1];
		thp_ic_cmd_data_common_data.data_buf[0] = input[2];
		
		if (thp_ic_cmd_data_common_data.mode == IC_MODE_44) {
			/* transport mode, databuf for send to driver is [s32 addr][u8 data0][u8 data1]... */
			for (i = 3; i < para_cnt - 1; ++i) {
				*databuf = (u8)input[i];
				databuf++;
			}
			if (thp_ic_cmd_data_common_data.cmd == SET_THP_IC_CUR_VALUE) 
				thp_ic_cmd_data_common_data.data_len = ((para_cnt - 3) <=  (input[para_cnt - 1])) ? (para_cnt - 3) : input[para_cnt - 1];
			else if (thp_ic_cmd_data_common_data.cmd == GET_THP_IC_CUR_VALUE)
				thp_ic_cmd_data_common_data.data_len = input[para_cnt - 1];
		} else {
			/* non-transport mode, databuf for send to driver is [s32 value0][s32 value1]...*/
			for (i = 2; i < para_cnt - 1; ++i) {
				thp_ic_cmd_data_common_data.data_buf[i-2] = input[i];
			}
			thp_ic_cmd_data_common_data.data_len = input[para_cnt - 1];
		}

		LOG_INFO("user_cmd:%d, mode:%d, addr:%d, data_len:%d", thp_ic_cmd_data_common_data.cmd,
							thp_ic_cmd_data_common_data.mode,
							thp_ic_cmd_data_common_data.data_buf[0],
							thp_ic_cmd_data_common_data.data_len);

		if (thp_ic_cmd_data_common_data.cmd == SET_THP_IC_CUR_VALUE) {
			if (xiaomi_touch_driver_param->hardware_operation.htc_ic_setModeValue) {
				xiaomi_touch_driver_param->hardware_operation.htc_ic_setModeValue(&thp_ic_cmd_data_common_data);
			}
		} else if (thp_ic_cmd_data_common_data.cmd == GET_THP_IC_CUR_VALUE) {
			if (xiaomi_touch_driver_param->hardware_operation.htc_ic_setModeValue) {
				xiaomi_touch_driver_param->hardware_operation.htc_ic_getModeValue(&thp_ic_cmd_data_common_data);
			}
		}

		kfree(strbuf);
		mutex_unlock(&thp_ic_mutex);
		return count;
	});

void update_get_ic_current_value(common_data_t *common_data) {
	memcpy(&thp_ic_cmd_data_common_data, common_data, sizeof(common_data_t));
}

CREATE_ATTR(touch_thp_ic_cmd_data, {
		int i = 0;
		int count = 0;
		int mode = thp_ic_cmd_data_common_data.mode;
		char *data_buf = (char *)&thp_ic_cmd_data_common_data.data_buf[1];
		u16 data_len = thp_ic_cmd_data_common_data.data_len;

		LOG_INFO("mode:%d, cmd:%d\n", mode, thp_ic_cmd_data_common_data.cmd);

		mutex_lock(&thp_ic_read_data_mutex);
		if (thp_ic_cmd_data_common_data.cmd != GET_THP_IC_CUR_VALUE) {
			LOG_ERROR("error, need to read data for ic!");
			mutex_unlock(&thp_ic_read_data_mutex);
			return count;
		}

		for (i = 0; i < data_len; i++) {
			LOG_INFO("buf[%d]:%x", i, data_buf[i]);
		}
		if (mode > THP_IC_CMD_BASE) {
			if (mode != IC_MODE_49) {
				for (i = 0; i < data_len; ++i) {
					count += snprintf(buf + count, PAGE_SIZE - count, "%x", data_buf[i]);
				}
				count += snprintf(buf + count, PAGE_SIZE - count, "\n");
			} else {
				count = analy_poll_data(data_buf, buf);
			}
		} else {
			memcpy(buf, data_buf, data_len);
			count = data_len;
		}

		mutex_unlock(&thp_ic_read_data_mutex);
		return count;
	},
	{
		if (count > MAX_BUF_SIZE) {
			LOG_ERROR("%s memory out of range:%d\n", __func__, (int)count);
			return count;
		}

		mutex_lock(&thp_ic_write_data_mutex);
		memcpy((char* )&thp_ic_cmd_data_common_data, buf, count);
		mutex_unlock(&thp_ic_write_data_mutex);

		return count;
	});


CREATE_ATTR(touch_finger_status, {
		return snprintf(buf, PAGE_SIZE, "%d\n", touch_finger_status);
	},
	{
		int input;

		if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;
		if (touch_finger_status == input)
			return count;
		touch_finger_status = input;
		sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "touch_finger_status");

		return count;
	});

CREATE_ATTR(fod_test, {
		return 0;
	},
	{
		int value;
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);

		if (sscanf(buf, "%d", &value) < 0)
			return -EINVAL;

		if (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.xiaomi_touch_fod_test)
			xiaomi_touch_driver_param->hardware_operation.xiaomi_touch_fod_test(value);

		return count;
	});

CREATE_ATTR(touch_log_level, {
		return snprintf(buf, PAGE_SIZE,
				"echo [value0] [value1] > touch_log_level\n"
				"[value0]: control xiaomi_touch log level, 0:Always,1:Error,2:Warning,3:Info,4:Debug,5:Verbose\n"
				"[value1]: control vendor driver log level, 0:Always,1:Error,2:Warning,3:Info,4:Debug,5:Verbose\n"
				"current xiaomi_touch log_level = %d, vendor log_level = %d\n",
				current_log_level, vendor_input);
	},
	{
		int input;
		s8 touch_id = 0;
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = NULL;

		if (sscanf(buf, "%d %d", &input, &vendor_input) < 0)
			return -EINVAL;
		current_log_level = input;
		for (touch_id = 0; touch_id < MAX_TOUCH_PANEL_COUNT; touch_id++) {
			xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
			if (xiaomi_touch_driver_param == NULL)
				continue;
			if (xiaomi_touch_driver_param->hardware_operation.touch_log_level_control)
				xiaomi_touch_driver_param->hardware_operation.touch_log_level_control(vendor_input >= MI_TP_LOG_DEBUG ? true : false);
			else if (xiaomi_touch_driver_param->hardware_operation.touch_log_level_control_v2)
				xiaomi_touch_driver_param->hardware_operation.touch_log_level_control_v2(vendor_input);
		}
		return count;
	});

static struct attribute *touch_attr_group[] = {
	&dev_attr_fod_press_status.attr,
	&dev_attr_fod_longpress_gesture_enabled.attr,
	&dev_attr_fod_test.attr,
	&dev_attr_gesture_single_tap_enabled.attr,
	&dev_attr_gesture_single_tap_state.attr,
	&dev_attr_gesture_double_tap_enabled.attr,
	&dev_attr_gesture_double_tap_state.attr,
	&dev_attr_panel_vendor.attr,
	&dev_attr_panel_color.attr,
	&dev_attr_panel_display.attr,
	&dev_attr_touch_vendor.attr,
	&dev_attr_touch_doze_analysis.attr,
	&dev_attr_touch_ic_buffer.attr,
	&dev_attr_resolution_factor.attr,
	&dev_attr_abnormal_event.attr,
	&dev_attr_enable_touch_raw.attr,
	&dev_attr_palm_sensor.attr,
	&dev_attr_touch_thp_ic_cmd_data.attr,
	&dev_attr_touch_thp_ic_cmd.attr,
	&dev_attr_touch_finger_status.attr,
	&dev_attr_touch_log_level.attr,
	NULL,
};

int xiaomi_touch_sys_init(void)
{
	int ret = 0;
	LOG_ERROR("enter");
	xiaomi_touch_class = class_create(THIS_MODULE, "touch");

	if (!xiaomi_touch_class) {
		LOG_ERROR("create device class err");
		return -1;
	}

	xiaomi_touch_dev = device_create(xiaomi_touch_class, NULL, 'T', NULL, "touch_dev");
	if (!xiaomi_touch_dev) {
		LOG_ERROR("create device dev err");
		return -1;
	}

	xiaomi_touch_attrs.attrs = touch_attr_group;
	ret = sysfs_create_group(&xiaomi_touch_dev->kobj, &xiaomi_touch_attrs);
	if (ret)
		LOG_ERROR("ERROR: Cannot create sysfs structure!:%d", ret);

	return ret;
}

int xiaomi_touch_sys_remove(void)
{

	if (xiaomi_touch_dev) {
		sysfs_remove_group(&xiaomi_touch_dev->kobj, &xiaomi_touch_attrs);
		device_destroy(xiaomi_touch_class, 'T');
		xiaomi_touch_dev = NULL;
	}

	if (xiaomi_touch_class) {
		class_destroy(xiaomi_touch_class);
		xiaomi_touch_class = NULL;
	}

	return 0;
}
