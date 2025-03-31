// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include "cam_trace.h"

#include "cam_debug_util.h"
/* xiaomi add hw trigger - begin */
#include <linux/gpio.h>
#include "cam_res_mgr_api.h"
#include <cam_soc_util.h>
/* xiaomi add hw trigger - end */

unsigned long long debug_mdl;
module_param(debug_mdl, ullong, 0644);

/* 0x0 - only logs, 0x1 - only trace, 0x2 - logs + trace */
uint debug_type;
module_param(debug_type, uint, 0644);

uint debug_priority;
module_param(debug_priority, uint, 0644);

uint debug_drv;
module_param(debug_drv, uint, 0644);

uint debug_bypass_drivers;
module_param(debug_bypass_drivers, uint, 0644);

struct camera_debug_settings cam_debug;

/* xiaomi add hw trigger - begin */
static int cam_hw_trigger_cnt  = 5;
// cam_hw_trigger[5] = {gpio, value, module_id, gpio_module, delay}
static uint cam_hw_trigger[5] = {0, 0, 0, 0, 1000};
module_param_array(cam_hw_trigger, uint, &cam_hw_trigger_cnt, 0644);

int cam_debug_hw_trigger(unsigned int module_id, bool status)
{
	int rc = 0;
	int restore_value = 0;

	if (!status ||	0 == cam_hw_trigger[0] ||
			0 == cam_hw_trigger[4] ||
			!(cam_hw_trigger[2] & module_id))
		return rc;

	rc = gpio_request_one(cam_hw_trigger[0], cam_hw_trigger[3], "CAM_HW_TRIGGER");
	if (rc) {
		CAM_ERR(CAM_UTIL, "[cam_hw_trigger] GPIO %d:%s request failed, rc = %d",
			cam_hw_trigger[0], "CAM_HW_TRIGGER", rc);
		return rc;
	}

	restore_value = gpio_get_value_cansleep(cam_hw_trigger[0]);
	if (restore_value != 0 && restore_value != 1) {
		CAM_ERR(CAM_UTIL, "[cam_hw_trigger] Failed to get GPIO %d:%s status",
			cam_hw_trigger[0], "CAM_HW_TRIGGER");
		rc = -EIO;
		goto end_free;
	}

	if (restore_value == cam_hw_trigger[1]) {
		CAM_ERR(CAM_UTIL, "[cam_hw_trigger] GPIO %d:%s is already in state %d",
			cam_hw_trigger[0], "CAM_HW_TRIGGER", cam_hw_trigger[1]);
		goto end_free;
	}

	gpio_set_value_cansleep(cam_hw_trigger[0], cam_hw_trigger[1]);
	CAM_DBG(CAM_UTIL, "[cam_hw_trigger] %s success, GPIO:%d value:%d",
		CAM_DBG_MOD_NAME(module_id), cam_hw_trigger[0],
		cam_hw_trigger[1]);

	usleep_range(cam_hw_trigger[4], (cam_hw_trigger[4] + 100));

	gpio_set_value_cansleep(cam_hw_trigger[0], restore_value);
	CAM_DBG(CAM_UTIL, "[cam_hw_trigger] %s restore, GPIO:%d value:%d",
		CAM_DBG_MOD_NAME(module_id), cam_hw_trigger[0], restore_value);

end_free:
	gpio_free(cam_hw_trigger[0]);
	return rc;
}
/* xiaomi add hw trigger - end */


struct dentry *cam_debugfs_root;

void cam_debugfs_init(void)
{
	struct dentry *tmp;

	if (!cam_debugfs_available()) {
		cam_debugfs_root = NULL;
		CAM_DBG(CAM_UTIL, "debugfs not available");
		return;
	}

	if (cam_debugfs_root) {
		CAM_WARN(CAM_UTIL, "already created debugfs root");
		return;
	}

	tmp = debugfs_create_dir("camera", NULL);
	if (IS_ERR_VALUE(tmp)) {
		CAM_ERR(CAM_UTIL, "failed to create debugfs root folder (rc=%d)", PTR_ERR(tmp));
		return;
	}

	cam_debugfs_root = tmp;
	CAM_DBG(CAM_UTIL, "successfully created debugfs root");
}

void cam_debugfs_deinit(void)
{
	if (!cam_debugfs_available())
		return;

	debugfs_remove_recursive(cam_debugfs_root);
	cam_debugfs_root = NULL;
}

int cam_debugfs_create_subdir(const char *name, struct dentry **subdir)
{
	struct dentry *tmp;

	if (!cam_debugfs_root) {
		CAM_WARN(CAM_UTIL, "debugfs root not created");
		*subdir = NULL;
		return -ENODEV;
	}

	if (!subdir) {
		CAM_ERR(CAM_UTIL, "invalid subdir pointer %pK", subdir);
		return -EINVAL;
	}

	tmp = debugfs_create_dir(name, cam_debugfs_root);
	if (IS_ERR_VALUE(tmp)) {
		CAM_ERR(CAM_UTIL, "failed to create debugfs subdir (name=%s, rc=%d)", name,
			PTR_ERR(tmp));
		return PTR_ERR(tmp);
	}

	*subdir = tmp;
	return 0;
}

int cam_debugfs_lookup_subdir(const char *name, struct dentry **subdir)
{
	if (!cam_debugfs_root) {
		CAM_WARN(CAM_UTIL, "debugfs root not created");
		*subdir = NULL;
		return -ENODEV;
	}

	if (!subdir) {
		CAM_ERR(CAM_UTIL, "invalid subdir pointer %pK", subdir);
		return -EINVAL;
	}

	*subdir = debugfs_lookup(name, cam_debugfs_root);
	return (*subdir) ? 0 : -ENOENT;
}

const struct camera_debug_settings *cam_debug_get_settings(void)
{
	return &cam_debug;
}

static int cam_debug_parse_cpas_settings(const char *setting, u64 value)
{
	if (!strcmp(setting, "camnoc_bw")) {
		cam_debug.cpas_settings.camnoc_bw = value;
	} else if (!strcmp(setting, "mnoc_hf_0_ab_bw")) {
		cam_debug.cpas_settings.mnoc_hf_0_ab_bw = value;
	} else if (!strcmp(setting, "mnoc_hf_0_ib_bw")) {
		cam_debug.cpas_settings.mnoc_hf_0_ib_bw = value;
	} else if (!strcmp(setting, "mnoc_hf_1_ab_bw")) {
		cam_debug.cpas_settings.mnoc_hf_1_ab_bw = value;
	} else if (!strcmp(setting, "mnoc_hf_1_ib_bw")) {
		cam_debug.cpas_settings.mnoc_hf_1_ib_bw = value;
	} else if (!strcmp(setting, "mnoc_sf_0_ab_bw")) {
		cam_debug.cpas_settings.mnoc_sf_0_ab_bw = value;
	} else if (!strcmp(setting, "mnoc_sf_0_ib_bw")) {
		cam_debug.cpas_settings.mnoc_sf_0_ib_bw = value;
	} else if (!strcmp(setting, "mnoc_sf_1_ab_bw")) {
		cam_debug.cpas_settings.mnoc_sf_1_ab_bw = value;
	} else if (!strcmp(setting, "mnoc_sf_1_ib_bw")) {
		cam_debug.cpas_settings.mnoc_sf_1_ib_bw = value;
	} else if (!strcmp(setting, "mnoc_sf_icp_ab_bw")) {
		cam_debug.cpas_settings.mnoc_sf_icp_ab_bw = value;
	} else if (!strcmp(setting, "mnoc_sf_icp_ib_bw")) {
		cam_debug.cpas_settings.mnoc_sf_icp_ib_bw = value;
	} else if (!strcmp(setting, "cam_ife_0_drv_ab_high_bw")) {
		cam_debug.cpas_settings.cam_ife_0_drv_ab_high_bw = value;
	} else if (!strcmp(setting, "cam_ife_0_drv_ib_high_bw")) {
		cam_debug.cpas_settings.cam_ife_0_drv_ib_high_bw = value;
	} else if (!strcmp(setting, "cam_ife_1_drv_ab_high_bw")) {
		cam_debug.cpas_settings.cam_ife_1_drv_ab_high_bw = value;
	} else if (!strcmp(setting, "cam_ife_1_drv_ib_high_bw")) {
		cam_debug.cpas_settings.cam_ife_1_drv_ib_high_bw = value;
	} else if (!strcmp(setting, "cam_ife_2_drv_ab_high_bw")) {
		cam_debug.cpas_settings.cam_ife_2_drv_ab_high_bw = value;
	} else if (!strcmp(setting, "cam_ife_2_drv_ib_high_bw")) {
		cam_debug.cpas_settings.cam_ife_2_drv_ib_high_bw = value;
	} else if (!strcmp(setting, "cam_ife_0_drv_ab_low_bw")) {
		cam_debug.cpas_settings.cam_ife_0_drv_ab_low_bw = value;
	} else if (!strcmp(setting, "cam_ife_0_drv_ib_low_bw")) {
		cam_debug.cpas_settings.cam_ife_0_drv_ib_low_bw = value;
	} else if (!strcmp(setting, "cam_ife_1_drv_ab_low_bw")) {
		cam_debug.cpas_settings.cam_ife_1_drv_ab_low_bw = value;
	} else if (!strcmp(setting, "cam_ife_1_drv_ib_low_bw")) {
		cam_debug.cpas_settings.cam_ife_1_drv_ib_low_bw = value;
	} else if (!strcmp(setting, "cam_ife_2_drv_ab_low_bw")) {
		cam_debug.cpas_settings.cam_ife_2_drv_ab_low_bw = value;
	} else if (!strcmp(setting, "cam_ife_2_drv_ib_low_bw")) {
		cam_debug.cpas_settings.cam_ife_2_drv_ib_low_bw = value;
	} else if (!strcmp(setting, "cam_ife_0_drv_low_set_zero")) {
		cam_debug.cpas_settings.cam_ife_0_drv_low_set_zero = value;
	} else if (!strcmp(setting, "cam_ife_1_drv_low_set_zero")) {
		cam_debug.cpas_settings.cam_ife_1_drv_low_set_zero = value;
	} else if (!strcmp(setting, "cam_ife_2_drv_low_set_zero")) {
		cam_debug.cpas_settings.cam_ife_2_drv_low_set_zero = value;
	} else {
		CAM_ERR(CAM_UTIL, "Unsupported cpas sysfs entry");
		return -EINVAL;
	}

	cam_debug.cpas_settings.is_updated = true;
	return 0;
}

ssize_t cam_debug_sysfs_node_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int rc = 0;
	char *local_buf = NULL, *local_buf_temp = NULL;
	char *driver;
	char *setting = NULL;
	char *value_str = NULL;
	u64 value;

	CAM_INFO(CAM_UTIL, "Sysfs debug attr name:[%s] buf:[%s] bytes:[%d]",
		attr->attr.name, buf, count);
	local_buf = kmemdup(buf, (count + sizeof(char)), GFP_KERNEL);
	local_buf_temp = local_buf;
	driver = strsep(&local_buf, "#");
	if (!driver) {
		CAM_ERR(CAM_UTIL,
			"Invalid input driver name buf:[%s], count:%d",
			buf, count);
		goto error;
	}

	setting = strsep(&local_buf, "=");
	if (!setting) {
		CAM_ERR(CAM_UTIL, "Invalid input setting buf:[%s], count:%d",
			buf, count);
		goto error;
	}

	value_str = strsep(&local_buf, "=");
	if (!value_str) {
		CAM_ERR(CAM_UTIL, "Invalid input value buf:[%s], count:%d",
			buf, count);
		goto error;
	}

	rc = kstrtou64(value_str, 0, &value);
	if (rc < 0) {
		CAM_ERR(CAM_UTIL, "Error converting value:[%s], buf:[%s]",
			value_str, buf);
		goto error;
	}

	CAM_INFO(CAM_UTIL,
		"Processing sysfs store for driver:[%s], setting:[%s], value:[%llu]",
		driver, setting, value);

	if (!strcmp(driver, "cpas")) {
		rc = cam_debug_parse_cpas_settings(setting, value);
		if (rc)
			goto error;
	} else {
		CAM_ERR(CAM_UTIL, "Unsupported driver in camera debug node");
		goto error;
	}

	kfree(local_buf_temp);
	return count;

error:
	kfree(local_buf_temp);
	return -EPERM;
}

static inline void __cam_print_to_buffer(char *buf, const size_t buf_size, size_t *len,
	unsigned int tag, enum cam_debug_module_id module_id, const char *fmt, va_list args)
{
	size_t buf_len = *len;

	buf_len += scnprintf(buf + buf_len, (buf_size - buf_len), "\n%-8s: %s:\t",
			CAM_LOG_TAG_NAME(tag), CAM_DBG_MOD_NAME(module_id));
	buf_len += vscnprintf(buf + buf_len, (buf_size - buf_len), fmt, args);
	*len = buf_len;
}

void cam_print_to_buffer(char *buf, const size_t buf_size, size_t *len, unsigned int tag,
	unsigned long long module_id, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	__cam_print_to_buffer(buf, buf_size, len, tag, module_id, fmt, args);
	va_end(args);
}

static void __cam_print_log(int type, const char *fmt, ...)
{
	va_list args1, args2, args;

	va_start(args, fmt);
	va_copy(args1, args);
	va_copy(args2, args1);
	if ((type & CAM_PRINT_LOG) && (debug_type != 1))
		vprintk(fmt, args1);
	if ((type & CAM_PRINT_TRACE) && (debug_type != 0)) {
		/* skip the first character which is used by printk to identify the log level */
		trace_cam_log_debug(fmt + sizeof(KERN_INFO) - 1, &args2);
	}
	va_end(args2);
	va_end(args1);
	va_end(args);
}

void cam_print_log(int type, int module, int tag, const char *func,
	int line, const char *fmt, ...)
{
	char buf[CAM_LOG_BUF_LEN] = {0,};

	struct timespec64 ts;
	uint64_t          ms=0, sec=0, min=0, hrs=0;

	va_list args;

	if (!type)
		return;

	CAM_GET_TIMESTAMP(ts);
	CAM_CONVERT_TIMESTAMP_FORMAT(ts, hrs, min, sec, ms);

	va_start(args, fmt);
	vscnprintf(buf, CAM_LOG_BUF_LEN, fmt, args);
	__cam_print_log(type, __CAM_LOG_FMT,
		hrs, min, sec, ms,
		CAM_LOG_TAG_NAME(tag), CAM_DBG_MOD_NAME(module), func,
		line, buf);
	va_end(args);
}
