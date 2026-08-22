/*
 * Goodix Touchscreen Driver
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <uapi/linux/sched/types.h>
#include <linux/backlight.h>
#include <linux/soc/qcom/panel_event_notifier.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 38)
#include <linux/input/mt.h>
#define INPUT_TYPE_B_PROTOCOL
#endif

#include "goodix_ts_core.h"

#define GOODIX_DEFAULT_CFG_NAME 	"goodix_cfg_group.cfg"
#define GOOIDX_INPUT_PHYS			"goodix_ts/input0"
#define PINCTRL_STATE_ACTIVE		"pmx_ts_active"
#define PINCTRL_STATE_SUSPEND		"pmx_ts_suspend"
#define PINCTRL_STATE_BOOT			"pmx_ts_boot"

#define HTC_PROJECT_CFG_NAME		"peridot_goodix_thp_config.ini"

#ifdef CONFIG_TOUCH_BOOST
#define EVENT_INPUT 0x1
extern void lpm_disable_for_dev(bool on, char event_dev);
#endif

#ifdef TOUCH_TRUSTED_SUPPORT
const char *qts_touch_type = NULL;
struct qts_vendor_data qts_vendor_data;
#endif // TOUCH_TRUSTED_SUPPORT
extern struct device_node *gf_spi_dp;
struct goodix_module goodix_modules;
int core_module_prob_sate = CORE_MODULE_UNPROBED;
int ID_Flag = 1;

#if IS_ENABLED(CONFIG_OF)
int goodix_check_ts_id_gpio(struct device *dev)
{
	int ret;
	int gpio1, gpio2;
	int gpio_det1, gpio_det2;

	gpio1 = of_get_named_gpio(dev->of_node, "goodix,ts-id1-gpio", 0);
	if (gpio1 < 0)
		return 0;

	ret = devm_gpio_request_one(dev, gpio1, GPIOF_IN, "LCD_ID_DET1");
	if (ret < 0)
		return -EINVAL;

	gpio_det1 = gpio_get_value(gpio1);

	gpio2 = of_get_named_gpio(dev->of_node, "goodix,ts-id2-gpio", 0);
	if (gpio2 < 0)
		return 0;

	ret = devm_gpio_request_one(dev, gpio2, GPIOF_IN, "LCD_ID_DET2");
	if (ret < 0)
		return -EINVAL;

	gpio_det2 = gpio_get_value(gpio2);

	ts_info("gpio_det1 = %d, gpio_det2 = %d\n", gpio_det1, gpio_det2);

	if (!gpio_det1 && !gpio_det2) {
		ID_Flag = 0;
		ts_info("goodix touchscreen detected with ID_Flag = %d", ID_Flag);
		return 0;
	}

	if (gpio_det1 && !gpio_det2) {
		ID_Flag = 2;
		ts_info("goodix touchscreen detected with ID_Flag = %d", ID_Flag);
		return 0;
	}

	ts_err("goodix touchscreen not detected");
	return -ENODEV;
}
#endif
struct goodix_ts_core *goodix_core_data;
static int goodix_send_ic_config(struct goodix_ts_core *cd, int type);
static int goodix_resume_suspend(bool is_resume, u8 gesture_type);
static int goodix_update_cfg(struct goodix_ts_core *cd, bool enable);
int goodix_ts_get_lockdown_info(struct goodix_ts_core *cd);
int goodix_ts_power_off(struct goodix_ts_core *cd);
int goodix_ts_power_on(struct goodix_ts_core *cd);
int goodix_set_thermal_temp(int temp, bool force);

/**
 * __do_register_ext_module - register external module
 * to register into touch core modules structure
 * return 0 on success, otherwise return < 0
 */
static int __do_register_ext_module(struct goodix_ext_module *module)
{
	struct goodix_ext_module *ext_module = NULL;
	struct goodix_ext_module *next = NULL;
	struct list_head *insert_point = &goodix_modules.head;

	/* prority level *must* be set */
	if (module->priority == EXTMOD_PRIO_RESERVED) {
		ts_err("Priority of module [%s] needs to be set",
			module->name);
		return -EINVAL;
	}
	mutex_lock(&goodix_modules.mutex);
	/* find insert point for the specified priority */
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (ext_module == module) {
				ts_info("Module [%s] already exists",
					module->name);
				mutex_unlock(&goodix_modules.mutex);
				return 0;
			}
		}

		/* smaller priority value with higher priority level */
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (ext_module->priority >= module->priority) {
				insert_point = &ext_module->list;
				break;
			}
		}
	}

	if (module->funcs && module->funcs->init) {
		if (module->funcs->init(goodix_modules.core_data,
					module) < 0) {
			ts_err("Module [%s] init error",
				module->name ? module->name : " ");
			mutex_unlock(&goodix_modules.mutex);
			return -EFAULT;
		}
	}

	list_add(&module->list, insert_point->prev);
	mutex_unlock(&goodix_modules.mutex);

	ts_info("Module [%s] registered,priority:%u", module->name,
		module->priority);
	return 0;
}

static void goodix_register_ext_module_work(struct work_struct *work) {
	struct goodix_ext_module *module =
			container_of(work, struct goodix_ext_module, work);

	ts_info("module register work IN");

	/* driver probe failed */
	if (core_module_prob_sate != CORE_MODULE_PROB_SUCCESS) {
		ts_err("Can't register ext_module core error");
		return;
	}

	if (__do_register_ext_module(module))
		ts_err("failed register module: %s", module->name);
	else
		ts_info("success register module: %s", module->name);
}

static void goodix_core_module_init(void)
{
	if (goodix_modules.initilized)
		return;
	goodix_modules.initilized = true;
	INIT_LIST_HEAD(&goodix_modules.head);
	mutex_init(&goodix_modules.mutex);
}

/**
 * goodix_register_ext_module - interface for register external module
 * to the core. This will create a workqueue to finish the real register
 * work and return immediately. The user need to check the final result
 * to make sure registe is success or fail.
 *
 * @module: pointer to external module to be register
 * return: 0 ok, <0 failed
 */
int goodix_register_ext_module(struct goodix_ext_module *module)
{
	if (!module)
		return -EINVAL;

	ts_info("goodix_register_ext_module IN");

	goodix_core_module_init();
	INIT_WORK(&module->work, goodix_register_ext_module_work);
	schedule_work(&module->work);

	ts_info("goodix_register_ext_module OUT");
	return 0;
}

/**
 * goodix_register_ext_module_no_wait
 * return: 0 ok, <0 failed
 */
int goodix_register_ext_module_no_wait(struct goodix_ext_module *module)
{
	if (!module)
		return -EINVAL;
	ts_info("goodix_register_ext_module_no_wait IN");
	goodix_core_module_init();
	/* driver probe failed */
	if (core_module_prob_sate != CORE_MODULE_PROB_SUCCESS) {
		ts_err("Can't register ext_module core error");
		return -EINVAL;
	}
	return __do_register_ext_module(module);
}

/**
 * goodix_unregister_ext_module - interface for external module
 * to unregister external modules
 *
 * @module: pointer to external module
 * return: 0 ok, <0 failed
 */
int goodix_unregister_ext_module(struct goodix_ext_module *module)
{
	struct goodix_ext_module *ext_module = NULL;
	struct goodix_ext_module *next = NULL;
	bool found = false;

	if (!module)
		return -EINVAL;

	if (!goodix_modules.initilized)
		return -EINVAL;

	if (!goodix_modules.core_data)
		return -ENODEV;

	mutex_lock(&goodix_modules.mutex);
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (ext_module == module) {
				found = true;
				break;
			}
		}
	} else {
		mutex_unlock(&goodix_modules.mutex);
		return 0;
	}

	if (!found) {
		ts_debug("Module [%s] never registed",
				module->name);
		mutex_unlock(&goodix_modules.mutex);
		return 0;
	}

	list_del(&module->list);
	mutex_unlock(&goodix_modules.mutex);

	if (module->funcs && module->funcs->exit)
		module->funcs->exit(goodix_modules.core_data, module);

	ts_info("Moudle [%s] unregistered",
		module->name ? module->name : " ");
	return 0;
}

static void goodix_ext_sysfs_release(struct kobject *kobj)
{
	ts_info("Kobject released!");
}

#define to_ext_module(kobj)	container_of(kobj,\
				struct goodix_ext_module, kobj)
#define to_ext_attr(attr)	container_of(attr,\
				struct goodix_ext_attribute, attr)

static ssize_t goodix_ext_sysfs_show(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	struct goodix_ext_module *module = to_ext_module(kobj);
	struct goodix_ext_attribute *ext_attr = to_ext_attr(attr);

	if (ext_attr->show)
		return ext_attr->show(module, buf);

	return -EIO;
}

static ssize_t goodix_ext_sysfs_store(struct kobject *kobj,
		struct attribute *attr, const char *buf, size_t count)
{
	struct goodix_ext_module *module = to_ext_module(kobj);
	struct goodix_ext_attribute *ext_attr = to_ext_attr(attr);

	if (ext_attr->store)
		return ext_attr->store(module, buf, count);

	return -EIO;
}

static const struct sysfs_ops goodix_ext_ops = {
	.show = goodix_ext_sysfs_show,
	.store = goodix_ext_sysfs_store
};

static struct kobj_type goodix_ext_ktype = {
	.release = goodix_ext_sysfs_release,
	.sysfs_ops = &goodix_ext_ops,
};

struct kobj_type *goodix_get_default_ktype(void)
{
	return &goodix_ext_ktype;
}

struct kobject *goodix_get_default_kobj(void)
{
	struct kobject *kobj = NULL;

	if (goodix_modules.core_data &&
			goodix_modules.core_data->pdev)
		kobj = &goodix_modules.core_data->pdev->dev.kobj;
	return kobj;
}

/* show driver infomation */
static ssize_t goodix_ts_driver_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "DriverVersion:%s\n",
			GOODIX_DRIVER_VERSION);
}

/* show chip infoamtion */
static ssize_t goodix_ts_chip_info_show(struct device  *dev,
		struct device_attribute *attr, char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_fw_version chip_ver;
	u8 temp_pid[8] = {0};
	int ret;
	int cnt = -EINVAL;

	if (hw_ops->read_version) {
		ret = hw_ops->read_version(core_data, &chip_ver);
		if (!ret) {
			memcpy(temp_pid, chip_ver.rom_pid, sizeof(chip_ver.rom_pid));
			cnt = snprintf(&buf[0], PAGE_SIZE,
				"rom_pid:%s\nrom_vid:%02x%02x%02x\n",
				temp_pid, chip_ver.rom_vid[0],
				chip_ver.rom_vid[1], chip_ver.rom_vid[2]);
			cnt += snprintf(&buf[cnt], PAGE_SIZE,
				"patch_pid:%s\npatch_vid:%02x%02x%02x%02x\n",
				chip_ver.patch_pid, chip_ver.patch_vid[0],
				chip_ver.patch_vid[1], chip_ver.patch_vid[2],
				chip_ver.patch_vid[3]);
			cnt += snprintf(&buf[cnt], PAGE_SIZE,
				"sensorid:%d\n", chip_ver.sensor_id);
		}
	}

	if (hw_ops->get_ic_info) {
		ret = hw_ops->get_ic_info(core_data, &core_data->ic_info);
		if (!ret) {
			cnt += snprintf(&buf[cnt], PAGE_SIZE,
				"config_id:%x\n", core_data->ic_info.version.config_id);
			cnt += snprintf(&buf[cnt], PAGE_SIZE,
				"config_version:%x\n", core_data->ic_info.version.config_version);
		}
	}

	return cnt;
}

/* reset chip */
static ssize_t goodix_ts_reset_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;

	if (!buf || count <= 0)
		return -EINVAL;
	if (buf[0] != '0')
		hw_ops->reset(core_data, GOODIX_NORMAL_RESET_DELAY_MS);
	return count;
}

/* read config */
static ssize_t goodix_ts_read_cfg_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret;
	int i;
	int offset;
	char *cfg_buf = NULL;

	cfg_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!cfg_buf)
		return -ENOMEM;

	if (hw_ops->read_config)
		ret = hw_ops->read_config(core_data, cfg_buf, PAGE_SIZE);
	else
		ret = -EINVAL;

	if (ret > 0) {
		offset = 0;
		for (i = 0; i < 200; i++) {
			offset += snprintf(&buf[offset], PAGE_SIZE - offset,
					"%02x,", cfg_buf[i]);
			if ((i + 1) % 20 == 0)
				buf[offset++] = '\n';
		}
	}

	kfree(cfg_buf);
	if (ret <= 0)
		return ret;

	return offset;
}

static u8 ascii2hex(u8 a)
{
	s8 value = 0;

	if (a >= '0' && a <= '9')
		value = a - '0';
	else if (a >= 'A' && a <= 'F')
		value = a - 'A' + 0x0A;
	else if (a >= 'a' && a <= 'f')
		value = a - 'a' + 0x0A;
	else
		value = 0xff;

	return value;
}

static int goodix_ts_convert_0x_data(const u8 *buf, int buf_size,
				u8 *out_buf, int *out_buf_len)
{
	int i, m_size = 0;
	int temp_index = 0;
	u8 high, low;

	for (i = 0; i < buf_size; i++) {
		if (buf[i] == 'x' || buf[i] == 'X')
			m_size++;
	}

	if (m_size <= 1) {
		ts_err("cfg file ERROR, valid data count:%d", m_size);
		return -EINVAL;
	}
	*out_buf_len = m_size;

	for (i = 0; i < buf_size; i++) {
		if (buf[i] != 'x' && buf[i] != 'X')
			continue;

		if (temp_index >= m_size) {
			ts_err("exchange cfg data error, overflow,"
					"temp_index:%d,m_size:%d",
					temp_index, m_size);
			return -EINVAL;
		}
		high = ascii2hex(buf[i + 1]);
		low = ascii2hex(buf[i + 2]);
		if (high == 0xff || low == 0xff) {
			ts_err("failed convert: 0x%x, 0x%x",
				buf[i + 1], buf[i + 2]);
			return -EINVAL;
		}
		out_buf[temp_index++] = (high << 4) + low;
	}
	return 0;
}

/* send config */
static ssize_t goodix_ts_send_cfg_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_ic_config *config = NULL;
	const struct firmware *cfg_img = NULL;
	int en;
	int ret;

	if (sscanf(buf, "%d", &en) != 1)
		return -EINVAL;

	if (en != 1)
		return -EINVAL;

	hw_ops->irq_enable(core_data, false);

	ret = request_firmware(&cfg_img, GOODIX_DEFAULT_CFG_NAME, dev);
	if (ret < 0) {
		ts_err("cfg file [%s] not available,errno:%d",
			GOODIX_DEFAULT_CFG_NAME, ret);
		goto exit;
	} else {
		ts_info("cfg file [%s] is ready", GOODIX_DEFAULT_CFG_NAME);
	}

	config = kzalloc(sizeof(*config), GFP_KERNEL);
	if (!config)
		goto exit;

	if (goodix_ts_convert_0x_data(cfg_img->data, cfg_img->size,
			config->data, &config->len)) {
		ts_err("convert config data FAILED");
		goto exit;
	}

	if (hw_ops->send_config) {
		ret = hw_ops->send_config(core_data, config->data, config->len);
		if (ret < 0)
			ts_err("send config failed");
	}

exit:
	hw_ops->irq_enable(core_data, true);
	kfree(config);
	if (cfg_img)
		release_firmware(cfg_img);

	return count;
}

/* reg read/write */
static u32 rw_addr;
static u32 rw_len;
static u8 rw_flag;
static u8 store_buf[32];
static u8 show_buf[PAGE_SIZE];
static ssize_t goodix_ts_reg_rw_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret;

	if (!rw_addr || !rw_len) {
		ts_err("address(0x%x) and length(%d) can't be null",
			rw_addr, rw_len);
		return -EINVAL;
	}

	if (rw_flag != 1) {
		ts_err("invalid rw flag %d, only support [1/2]", rw_flag);
		return -EINVAL;
	}

	ret = hw_ops->read(core_data, rw_addr, show_buf, rw_len);
	if (ret < 0) {
		ts_err("failed read addr(%x) length(%d)", rw_addr, rw_len);
		return snprintf(buf, PAGE_SIZE, "failed read addr(%x), len(%d)\n",
			rw_addr, rw_len);
	}

	return snprintf(buf, PAGE_SIZE, "0x%x,%d {%*ph}\n",
		rw_addr, rw_len, rw_len, show_buf);
}

static ssize_t goodix_ts_reg_rw_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	char *pos = NULL;
	char *token = NULL;
	long result = 0;
	int ret;
	int i;

#ifdef TOUCH_TRUSTED_SUPPORT
	if (goodix_core_data->tui_process) {
		if (wait_for_completion_interruptible(&goodix_core_data->tui_finish) ) {
			ts_err("cautious, ERESTARTSYS may cause cmd loss recomand try again");
			return -EINVAL;
		}
		ts_info("wait finished, its time to go ahead");
	}
#endif // TOUCH_TRUSTED_SUPPORT

	if (!buf || !count) {
		ts_err("invalid parame");
		goto err_out;
	}

	if (buf[0] == 'r') {
		rw_flag = 1;
	} else if (buf[0] == 'w') {
		rw_flag = 2;
	} else {
		ts_err("string must start with 'r/w'");
		goto err_out;
	}

	/* get addr */
	pos = (char *)buf;
	pos += 2;
	token = strsep(&pos, ":");
	if (!token) {
		ts_err("invalid address info");
		goto err_out;
	} else {
		if (kstrtol(token, 16, &result)) {
			ts_err("failed get addr info");
			goto err_out;
		}
		rw_addr = (u32)result;
		ts_info("rw addr is 0x%x", rw_addr);
	}

	/* get length */
	token = strsep(&pos, ":");
	if (!token) {
		ts_err("invalid length info");
		goto err_out;
	} else {
		if (kstrtol(token, 0, &result)) {
			ts_err("failed get length info");
			goto err_out;
		}
		rw_len = (u32)result;
		ts_info("rw length info is %d", rw_len);
		if (rw_len > sizeof(store_buf)) {
			ts_err("data len > %lu", sizeof(store_buf));
			goto err_out;
		}
	}

	if (rw_flag == 1)
		return count;

	for (i = 0; i < rw_len; i++) {
		token = strsep(&pos, ":");
		if (!token) {
			ts_err("invalid data info");
			goto err_out;
		} else {
			if (kstrtol(token, 16, &result)) {
				ts_err("failed get data[%d] info", i);
				goto err_out;
			}
			store_buf[i] = (u8)result;
			ts_info("get data[%d]=0x%x", i, store_buf[i]);
		}
	}
	ret = hw_ops->write(core_data, rw_addr, store_buf, rw_len);
	if (ret < 0) {
		ts_err("failed write addr(%x) data %*ph", rw_addr,
			rw_len, store_buf);
		goto err_out;
	}

	ts_info("%s write to addr (%x) with data %*ph",
		"success", rw_addr, rw_len, store_buf);

	return count;
err_out:
	snprintf(show_buf, PAGE_SIZE, "%s\n",
		"invalid params, format{r/w:4100:length:[41:21:31]}");
	return -EINVAL;

}

/* show irq infomation */
static ssize_t goodix_ts_irq_info_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct irq_desc *desc;
	size_t offset = 0;
	int r;

	r = snprintf(&buf[offset], PAGE_SIZE, "irq:%u\n", core_data->irq);
	if (r < 0)
		return -EINVAL;

	offset += r;
	r = snprintf(&buf[offset], PAGE_SIZE - offset, "state:%s\n",
			atomic_read(&core_data->irq_enabled) ?
			"enabled" : "disabled");
	if (r < 0)
		return -EINVAL;

	desc = irq_to_desc(core_data->irq);
	offset += r;
	r = snprintf(&buf[offset], PAGE_SIZE - offset, "disable-depth:%d\n",
			desc->depth);
	if (r < 0)
		return -EINVAL;

	offset += r;
	r = snprintf(&buf[offset], PAGE_SIZE - offset, "trigger-count:%zu\n",
		core_data->irq_trig_cnt);
	if (r < 0)
		return -EINVAL;

	offset += r;
	r = snprintf(&buf[offset], PAGE_SIZE - offset,
			"echo 0/1 > irq_info to disable/enable irq\n");
	if (r < 0)
		return -EINVAL;

	offset += r;
	return offset;
}

/* enable/disable irq */
static ssize_t goodix_ts_irq_info_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;

	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] != '0')
		hw_ops->irq_enable(core_data, true);
	else
		hw_ops->irq_enable(core_data, false);
	return count;
}

/* show esd status */
static ssize_t goodix_ts_esd_info_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;
	int r = 0;

	r = snprintf(buf, PAGE_SIZE, "state:%s\n",
			atomic_read(&ts_esd->esd_on) ?
			"enabled" : "disabled");

	return r;
}

/* enable/disable esd */
static ssize_t goodix_ts_esd_info_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] != '0')
		goodix_ts_blocking_notify(NOTIFY_ESD_ON, NULL);
	else
		goodix_ts_blocking_notify(NOTIFY_ESD_OFF, NULL);
	return count;
}

/* debug level show */
static ssize_t goodix_ts_debug_log_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int r = 0;

	r = snprintf(buf, PAGE_SIZE, "state:%s\n",
			debug_log_flag ? "enabled" : "disabled");

	return r;
}

/* debug level store */
static ssize_t goodix_ts_debug_log_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] == 'u' && goodix_core_data) {
		goodix_core_data->need_update_cfg = 1;
		goodix_core_data->cfg_cloud_state = false;
		ts_info("test for update cfg, mode = 0");
	} else if (buf[0] == 'v' && goodix_core_data) {
		goodix_core_data->need_update_cfg = 1;
		goodix_core_data->cfg_cloud_state = true;
		ts_info("test for update cfg, mode = 1");
	} else if (!strncmp(buf, "240Hz", 5)) {
		goodix_core_data->report_rate = 240;
		if (brl_switch_report_rate(goodix_core_data, false))
			ts_info("set 240Hz failed");
		else
			ts_info("set 240Hz successfully");
		return count;
	} else if (!strncmp(buf, "480Hz", 5)) {
		goodix_core_data->report_rate = 480;
		if (brl_switch_report_rate(goodix_core_data, true))
			ts_info("set 480Hz failed");
		else
			ts_info("set 480Hz successfully");
		return count;
	}

	if (buf[0] != '0')
		debug_log_flag = true;
	else
		debug_log_flag = false;

	return count;
}

static void set_touch_mode(int mode, int value)
{
	int touch_mode[DATA_MODE_35];
	long update_mode_mask = 0;
	if (mode < 0 || mode >= DATA_MODE_35 || value < 0)
		return;
	touch_mode[mode] = value;
	update_mode_mask |= 1 << mode;
	driver_update_touch_mode(TOUCH_ID, touch_mode, update_mode_mask);
}

/* double tap gesture show */
static ssize_t goodix_ts_double_tap_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int value = driver_get_touch_mode(TOUCH_ID, DATA_MODE_14);
	return snprintf(buf, PAGE_SIZE, "state:%s\n",
			value ? "enabled" : "disabled");
}

/* double tap gesture store */
static ssize_t goodix_ts_double_tap_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	if (!buf || count <= 0)
		return -EINVAL;
	set_touch_mode(DATA_MODE_14, buf[0] != '0');
	return count;
}

/* single tap gesture show */
static ssize_t goodix_ts_single_tap_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int value = driver_get_touch_mode(TOUCH_ID, DATA_MODE_11);
	return snprintf(buf, PAGE_SIZE, "state:%s\n",
			value ? "enabled" : "disabled");
}

/* single tap gesture store */
static ssize_t goodix_ts_single_tap_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	if (!buf || count <= 0)
		return -EINVAL;
	set_touch_mode(DATA_MODE_11, buf[0] != '0');
	return count;
}

/* aod gesture show */
static ssize_t goodix_ts_aod_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int value = driver_get_touch_mode(TOUCH_ID, DATA_MODE_11);
	return snprintf(buf, PAGE_SIZE, "state:%s\n",
			value ? "enabled" : "disabled");
}

/* aod gesture_store */
static ssize_t goodix_ts_aod_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	if (!buf || count <= 0)
		return -EINVAL;
	set_touch_mode(DATA_MODE_11, buf[0] != '0');
	return count;
}

#ifdef TOUCH_FOD_SUPPORT
/* fod gesture show */
static ssize_t goodix_ts_fod_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int value = driver_get_touch_mode(TOUCH_ID, DATA_MODE_10);
	return snprintf(buf, PAGE_SIZE, "state:%s\n",
			value ? "enabled" : "disabled");
}

/* fod gesture_store */
static ssize_t goodix_ts_fod_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	if (!buf || count <= 0)
		return -EINVAL;

	set_touch_mode(DATA_MODE_10, buf[0] != '0');
	return count;
}
#endif

/* report_rate show */
static ssize_t goodix_report_rate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int r = 0;

	r = snprintf(buf, PAGE_SIZE, "touch report rate::%d\n",goodix_core_data->report_rate);

	return r;
}

/* report_rate_store */
static ssize_t goodix_report_rate_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	// struct goodix_ts_core *core_data = dev_get_drvdata(dev);

	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] == '0') {
		goodix_core_data->report_rate = 240;
		brl_switch_report_rate(goodix_core_data, false);
	}  else {
		goodix_core_data->report_rate = 480;
		brl_switch_report_rate(goodix_core_data, true);
	}

	return count;
}

#ifdef TOUCH_THP_SUPPORT
static ssize_t goodix_ts_scan_freq_index_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);

	if (!buf || count <= 0)
		return -EINVAL;
	if (buf[0] - '0' < core_data->ic_info.parm.mutual_freq_num) {
		ts_info("set scan freq index: %d", buf[0] - '0');
		goodix_htc_set_scan_freq(buf[0] - '0');
	} else
		ts_err("out of scan freq num!");
	return count;
}
#endif

static DEVICE_ATTR(driver_info, 0444, goodix_ts_driver_info_show, NULL);
static DEVICE_ATTR(chip_info, 0444, goodix_ts_chip_info_show, NULL);
static DEVICE_ATTR(reset, 0220, NULL, goodix_ts_reset_store);
static DEVICE_ATTR(send_cfg, 0220, NULL, goodix_ts_send_cfg_store);
static DEVICE_ATTR(read_cfg, 0444, goodix_ts_read_cfg_show, NULL);
static DEVICE_ATTR(reg_rw, 0664, goodix_ts_reg_rw_show, goodix_ts_reg_rw_store);
static DEVICE_ATTR(irq_info, 0664, goodix_ts_irq_info_show, goodix_ts_irq_info_store);
static DEVICE_ATTR(esd_info, 0664, goodix_ts_esd_info_show, goodix_ts_esd_info_store);
static DEVICE_ATTR(debug_log, 0664, goodix_ts_debug_log_show, goodix_ts_debug_log_store);
static DEVICE_ATTR(double_tap_enable, 0664, goodix_ts_double_tap_show, goodix_ts_double_tap_store);
static DEVICE_ATTR(single_tap_enable, 0664, goodix_ts_single_tap_show, goodix_ts_single_tap_store);
static DEVICE_ATTR(aod_enable, 0664, goodix_ts_aod_show, goodix_ts_aod_store);
static DEVICE_ATTR(switch_report_rate, 0664, goodix_report_rate_show, goodix_report_rate_store);
#ifdef TOUCH_FOD_SUPPORT
static DEVICE_ATTR(fod_enable, 0664, goodix_ts_fod_show, goodix_ts_fod_store);
#endif
#ifdef TOUCH_THP_SUPPORT
static DEVICE_ATTR(scan_freq_index, 0220, NULL, goodix_ts_scan_freq_index_store);
#endif

static struct attribute *sysfs_attrs[] = {
	&dev_attr_driver_info.attr,
	&dev_attr_chip_info.attr,
	&dev_attr_reset.attr,
	&dev_attr_send_cfg.attr,
	&dev_attr_read_cfg.attr,
	&dev_attr_reg_rw.attr,
	&dev_attr_irq_info.attr,
	&dev_attr_esd_info.attr,
	&dev_attr_debug_log.attr,
	&dev_attr_double_tap_enable.attr,
	&dev_attr_single_tap_enable.attr,
	&dev_attr_aod_enable.attr,
	&dev_attr_switch_report_rate.attr,
#ifdef TOUCH_FOD_SUPPORT
	&dev_attr_fod_enable.attr,
#endif
#ifdef TOUCH_THP_SUPPORT
	&dev_attr_scan_freq_index.attr,
#endif
	NULL,
};

static const struct attribute_group sysfs_group = {
	.attrs = sysfs_attrs,
};

static int goodix_ts_sysfs_init(struct goodix_ts_core *core_data)
{
	int ret;

	ret = sysfs_create_group(&core_data->pdev->dev.kobj, &sysfs_group);
	if (ret) {
		ts_err("failed create core sysfs group");
		return ret;
	}

	return ret;
}

static void goodix_ts_sysfs_exit(struct goodix_ts_core *core_data)
{
	sysfs_remove_group(&core_data->pdev->dev.kobj, &sysfs_group);
}

static int goodix_ic_data_collect(char *buf, int *length)
{
	struct ts_rawdata_info *info;
	struct goodix_ts_core *cd = goodix_core_data;
	int tx;
	int rx;
	int ret;
	int i;
	int index;
	int buf_size = PAGE_SIZE * 3;
	int cnt = 0;

	if (!cd) {
		ts_err("rawdata_proc_show, input null ptr");
		return -EIO;
	}

	if (!buf || !length) {
		ts_err("invalid params");
		return -EINVAL;
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		ts_err("Failed to alloc rawdata info memory");
		return -ENOMEM;
	}

	ret = cd->hw_ops->get_capacitance_data(cd, info);
	if (ret < 0) {
		ts_err("failed to get_capacitance_data, exit!");
		goto exit;
	}

	rx = info->buff[0];
	tx = info->buff[1];
	cnt += snprintf(buf + cnt, buf_size - cnt, "TX:%d  RX:%d\n", tx, rx);
	cnt += snprintf(buf + cnt, buf_size - cnt, "mutual_rawdata:\n");
	index = 2;
	for (i = 0; i < tx * rx; i++) {
		cnt += snprintf(buf + cnt, buf_size - cnt, "%5d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			cnt += snprintf(buf + cnt, buf_size - cnt, "\n");
	}
	cnt += snprintf(buf + cnt, buf_size - cnt, "mutual_diffdata:\n");
	index += tx * rx;
	for (i = 0; i < tx * rx; i++) {
		cnt += snprintf(buf + cnt, buf_size - cnt, "%3d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			cnt += snprintf(buf + cnt, buf_size - cnt, "\n");
	}
	cnt += snprintf(buf + cnt, buf_size - cnt, "mutual_refdata:\n");
	index += tx * rx;
	for (i = 0; i < tx * rx; i++) {
		cnt += snprintf(buf + cnt, buf_size - cnt, "%5d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			cnt += snprintf(buf + cnt, buf_size - cnt, "\n");
	}
	*length = cnt;

exit:
	kfree(info);
	return ret;
}

/* prosfs create */
static int rawdata_proc_show(struct seq_file *m, void *v)
{
	struct ts_rawdata_info *info;
	struct goodix_ts_core *cd = !m ? NULL : m->private;
	int tx;
	int rx;
	int ret;
	int i;
	int index;

	if (!m || !v || !cd) {
		ts_err("rawdata_proc_show, input null ptr");
		return -EIO;
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		ts_err("Failed to alloc rawdata info memory");
		return -ENOMEM;
	}

	ret = cd->hw_ops->get_capacitance_data(cd, info);
	if (ret < 0) {
		ts_err("failed to get_capacitance_data, exit!");
		goto exit;
	}

	rx = info->buff[0];
	tx = info->buff[1];
	seq_printf(m, "TX:%d  RX:%d\n", tx, rx);
	seq_printf(m, "mutual_rawdata:\n");
	index = 2;
	for (i = 0; i < tx * rx; i++) {
		seq_printf(m, "%5d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			seq_printf(m, "\n");
	}
	seq_printf(m, "mutual_diffdata:\n");
	index += tx * rx;
	for (i = 0; i < tx * rx; i++) {
		seq_printf(m, "%3d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			seq_printf(m, "\n");
	}
	seq_printf(m, "mutual_refdata:\n");
	index += tx * rx;
	for (i = 0; i < tx * rx; i++) {
		seq_printf(m, "%5d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			seq_printf(m, "\n");
	}

exit:
	kfree(info);
	return ret;
}

static int rawdata_proc_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, rawdata_proc_show, pde_data(inode), PAGE_SIZE * 10);
}

static const struct proc_ops rawdata_proc_fops = {
	.proc_open = rawdata_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int framedata_proc_show(struct seq_file *m, void *v)
{
	struct goodix_ts_core *cd = m->private;
	struct ts_framedata *info;
	int ret;
	int i;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		ts_err("Failed to alloc framedata info memory");
		return -ENOMEM;
	}

	ret = cd->hw_ops->get_frame_data(cd, info);
	if (ret < 0 || info->used_size == 0)
		goto exit;

	for (i = 0; i < info->used_size; i++) {
		seq_printf(m, "0x%02x,", info->buff[i]);
		if ((i + 1) % 32 == 0)
			seq_printf(m, "\n");
	}
	seq_printf(m, "\n");

exit:
	kfree(info);
	return 0;
}

static int framedata_proc_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, framedata_proc_show, pde_data(inode), PAGE_SIZE * 10);
}

static const struct proc_ops framedata_proc_fops = {
	.proc_open = framedata_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static void goodix_ts_procfs_init(struct goodix_ts_core *core_data)
{
	if (!proc_mkdir("goodix_ts", NULL))
		return;
	proc_create_data("goodix_ts/tp_capacitance_data",
			0666, NULL, &rawdata_proc_fops, core_data);

	if (core_data->bus->ic_type == IC_TYPE_BERLIN_D) {
		proc_create_data("goodix_ts/get_frame_data",
				0666, NULL, &framedata_proc_fops, core_data);
	}
}

static void goodix_ts_procfs_exit(struct goodix_ts_core *core_data)
{
	remove_proc_entry("goodix_ts/tp_capacitance_data", NULL);
	if (core_data->bus->ic_type == IC_TYPE_BERLIN_D)
		remove_proc_entry("goodix_ts/get_frame_data", NULL);
	remove_proc_entry("goodix_ts", NULL);
}

/* event notifier */
static BLOCKING_NOTIFIER_HEAD(ts_notifier_list);
/**
 * goodix_ts_register_client - register a client notifier
 * @nb: notifier block to callback on events
 *  see enum ts_notify_event in goodix_ts_core.h
 */
int goodix_ts_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&ts_notifier_list, nb);
}

/**
 * goodix_ts_unregister_client - unregister a client notifier
 * @nb: notifier block to callback on events
 *	see enum ts_notify_event in goodix_ts_core.h
 */
int goodix_ts_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&ts_notifier_list, nb);
}

/**
 * fb_notifier_call_chain - notify clients of fb_events
 *	see enum ts_notify_event in goodix_ts_core.h
 */
int goodix_ts_blocking_notify(enum ts_notify_event evt, void *v)
{
	int ret;

	ret = blocking_notifier_call_chain(&ts_notifier_list,
			(unsigned long)evt, v);
	return ret;
}

#ifdef CONFIG_OF
/**
 * goodix_parse_dt_resolution - parse resolution from dt
 * @node: devicetree node
 * @board_data: pointer to board data structure
 * return: 0 - no error, <0 error
 */
static int goodix_parse_dt_resolution(struct device_node *node,
		struct goodix_ts_board_data *board_data)
{
	int ret;

	ret = of_property_read_u32(node, "goodix,panel-max-x",
				&board_data->panel_max_x);
	if (ret) {
		ts_err("failed get panel-max-x");
		return ret;
	}

	ret = of_property_read_u32(node, "goodix,panel-max-y",
				&board_data->panel_max_y);
	if (ret) {
		ts_err("failed get panel-max-y");
		return ret;
	}

	board_data->invert_xy = of_property_read_bool(node,
					"invert_xy");
	if (board_data->invert_xy) {
		swap(board_data->panel_max_x, board_data->panel_max_y);
		ts_info("Panel max x and max y inverted\n");
	}

	ret = of_property_read_u32(node, "goodix,panel-max-w",
				&board_data->panel_max_w);
	if (ret) {
		ts_err("failed get panel-max-w");
		return ret;
	}

	ret = of_property_read_u32(node, "goodix,panel-max-p",
				&board_data->panel_max_p);
	if (ret) {
		ts_err("failed get panel-max-p, use default");
		board_data->panel_max_p = 8192;
	}

	ret = of_property_read_u32(node, "goodix,super-resolution-factor",
				&board_data->super_resolution_factor);
	if (ret < 0) {
		ts_info("super-resolution-factor not found, default to 16");
		board_data->super_resolution_factor = 16;
	}

	return 0;
}

/**
 * goodix_parse_dt - parse board data from dt
 * @dev: pointer to device
 * @board_data: pointer to board data structure
 * return: 0 - no error, <0 error
 */
static int goodix_parse_dt(struct device_node *node,
	struct goodix_ts_board_data *board_data)
{
	const char *name_tmp;
	const char *fw_prop, *cfg_prop;
	int r;

	if (!board_data) {
		ts_err("invalid board data");
		return -EINVAL;
	}

	fw_prop  = "goodix,firmware-name";
	cfg_prop = "goodix,config-name";
	if (ID_Flag == 0) {
		fw_prop  = "goodix,firmware-name-second";
		cfg_prop = "goodix,config-name-second";
		ts_info("ID_Flag=0, using secondary DT keys: %s / %s", fw_prop, cfg_prop);
	} else {
		ts_info("ID_Flag=%d, using primary DT keys: %s / %s", ID_Flag, fw_prop, cfg_prop);
	}

	r = of_get_named_gpio(node, "goodix,avdd-gpio", 0);
	if (r < 0) {
		ts_info("can't find avdd-gpio, use other power supply");
		board_data->avdd_gpio = 0;
	} else {
		ts_info("get avdd-gpio[%d] from dt", r);
		board_data->avdd_gpio = r;
	}

	r = of_get_named_gpio(node, "goodix,iovdd-gpio", 0);
	if (r < 0) {
		ts_info("can't find iovdd-gpio, use other power supply");
		board_data->iovdd_gpio = 0;
	} else {
		ts_info("get iovdd-gpio[%d] from dt", r);
		board_data->iovdd_gpio = r;
	}

	r = of_get_named_gpio(node, "goodix,reset-gpio", 0);
	if (r < 0) {
		ts_err("invalid reset-gpio in dt: %d", r);
		return -EINVAL;
	}
	ts_info("get reset-gpio[%d] from dt", r);
	board_data->reset_gpio = r;

	r = of_get_named_gpio(node, "goodix,irq-gpio", 0);
	if (r < 0) {
		ts_err("invalid irq-gpio in dt: %d", r);
		return -EINVAL;
	}
	ts_info("get irq-gpio[%d] from dt", r);
	board_data->irq_gpio = r;

	r = of_property_read_u32(node, "goodix,irq-flags",
			&board_data->irq_flags);
	if (r) {
		ts_err("invalid irq-flags");
		return -EINVAL;
	}

	memset(board_data->avdd_name, 0, sizeof(board_data->avdd_name));
	r = of_property_read_string(node, "goodix,avdd-name", &name_tmp);
	if (!r) {
		ts_info("avdd name from dt: %s", name_tmp);
		if (strlen(name_tmp) < sizeof(board_data->avdd_name))
			strlcpy(board_data->avdd_name,
				name_tmp, sizeof(board_data->avdd_name));
		else
			ts_info("invalied avdd name length: %ld > %ld",
				strlen(name_tmp),
				sizeof(board_data->avdd_name));
	}

	memset(board_data->iovdd_name, 0, sizeof(board_data->iovdd_name));
	r = of_property_read_string(node, "goodix,iovdd-name", &name_tmp);
	if (!r) {
		ts_info("iovdd name from dt: %s", name_tmp);
		if (strlen(name_tmp) < sizeof(board_data->iovdd_name))
			strlcpy(board_data->iovdd_name,
				name_tmp, sizeof(board_data->iovdd_name));
		else
			ts_info("invalied iovdd name length: %ld > %ld",
				strlen(name_tmp),
				sizeof(board_data->iovdd_name));
	} else {
		ts_info("iovdd can't find from dt");
	}

	/* get firmware file name */
	r = of_property_read_string(node, fw_prop, &name_tmp);
	if (!r) {
		ts_info("firmware name from dt: %s", name_tmp);
		strlcpy(board_data->fw_name, name_tmp, sizeof(board_data->fw_name));
	} else {
		ts_info("can't find firmware name, use default: %s", TS_DEFAULT_FIRMWARE);
		strlcpy(board_data->fw_name, TS_DEFAULT_FIRMWARE, sizeof(board_data->fw_name));
	}

	/* get config file name */
	r = of_property_read_string(node, cfg_prop, &name_tmp);
	if (!r) {
		ts_info("config name from dt: %s", name_tmp);
		strlcpy(board_data->cfg_name, name_tmp, sizeof(board_data->cfg_name));
		strlcpy(board_data->cfg_bin_name, name_tmp, sizeof(board_data->cfg_bin_name));
	} else {
		ts_info("can't find config name, use default: %s", TS_DEFAULT_CFG_BIN);
		strlcpy(board_data->cfg_name, TS_DEFAULT_CFG_BIN, sizeof(board_data->cfg_name));
		strlcpy(board_data->cfg_bin_name, TS_DEFAULT_CFG_BIN, sizeof(board_data->cfg_bin_name));
	}

	/* get second firmware file name */
	/* Secondary names are now handled by ID_Flag and fw_prop/cfg_prop logic above */

	/* get xyz resolutions */
	r = goodix_parse_dt_resolution(node, board_data);
	if (r) {
		ts_err("Failed to parse resolutions:%d", r);
		return r;
	}

	/*get pen-enable switch and pen keys*/
	board_data->pen_enable = of_property_read_bool(node,
					"goodix,pen-enable");
	if (board_data->pen_enable)
		ts_info("goodix pen enabled");

	r = of_property_count_u32_elems(node, "goodix,touch-expert-array");
	if (r == GAME_ARRAY_LEN * GAME_ARRAY_SIZE) {
		of_property_read_u32_array(node,
						"goodix,touch-expert-array",
						board_data->touch_expert_array,
						r);
	} else if (r > 0) {
		ts_err("Failed to parse touch-expert-array:%d", r);
	}

#ifdef TOUCH_TRUSTED_SUPPORT
	goodix_core_data->qts_en = of_property_read_bool(node, "goodix,qts_en");
	if (goodix_core_data->qts_en) {
		ts_info("QTS enabled");
	}

	r = of_property_read_string(node, "goodix,touch-type", &qts_touch_type);
	if (!r) {
		ts_debug("qts touch type from dt: %s", qts_touch_type);
	} else {
		ts_err("fail to get touch type(%s)", qts_touch_type);
	}
#endif // TOUCH_TRUSTED_SUPPORT

	ts_debug("[DT]x:%d, y:%d, w:%d, p:%d", board_data->panel_max_x,
		 board_data->panel_max_y, board_data->panel_max_w,
		 board_data->panel_max_p);

	return 0;
}
#endif



static void goodix_ts_report_finger(struct input_dev *dev,
		struct goodix_touch_data *touch_data)
{
	unsigned int touch_num = touch_data->touch_num;
	int i;
	static int pre_finger_num;

#ifdef TOUCH_THP_SUPPORT
	if (goodix_core_data->enable_touch_raw)
		return;
#endif

	mutex_lock(&dev->mutex);

#ifdef TOUCH_FOD_SUPPORT
	if ((goodix_core_data->eventsdata & 0x08) && ((driver_get_touch_mode(TOUCH_ID, DATA_MODE_10) == 1) ||
						(driver_get_touch_mode(TOUCH_ID, DATA_MODE_10) == 2) ||
						(driver_get_touch_mode(TOUCH_ID, DATA_MODE_10) == 3))
			&& (!goodix_core_data->fod_finger)) {
		goodix_core_data->fod_finger = true;
		if (goodix_core_data->fod_down_before_suspend) {
			ts_info("skip report fod");
			goto finger_pos;
		}
		ts_info("ts fod down, overlay: %d, fod id: %d", touch_data->overlay, touch_data->fod_id);
		update_fod_press_status(1);
		goto finger_pos;
	} else if ((goodix_core_data->eventsdata & 0x08) != 0x08 && goodix_core_data->fod_finger) {
		ts_info("ts fod up, overlay: %d", touch_data->overlay);
		goodix_core_data->fod_down_before_suspend = false;
		input_report_abs(dev, ABS_MT_WIDTH_MAJOR, 0);
		input_report_abs(dev, ABS_MT_WIDTH_MINOR, 0);
		input_sync(dev);
		update_fod_press_status(0);
		goodix_core_data->fod_finger = false;
		goto finger_pos;
	}
finger_pos:
#endif

	for (i = 0; i < GOODIX_MAX_TOUCH; i++) {
		if (touch_data->coords[i].status == TS_TOUCH) {
			touch_data->coords[i].x /= goodix_core_data->board_data.super_resolution_factor;
			touch_data->coords[i].y /= goodix_core_data->board_data.super_resolution_factor;
			ts_debug("report: id %d, x %d, y %d, w %d, factor %d", i,
				touch_data->coords[i].x, touch_data->coords[i].y,
				touch_data->coords[i].w,
				goodix_core_data->board_data.super_resolution_factor);
			input_mt_slot(dev, i);
			input_mt_report_slot_state(dev, MT_TOOL_FINGER, true);
			input_report_abs(dev, ABS_MT_POSITION_X,
					touch_data->coords[i].x);
			input_report_abs(dev, ABS_MT_POSITION_Y,
					touch_data->coords[i].y);
			input_report_abs(dev, ABS_X, touch_data->coords[i].x);
			input_report_abs(dev, ABS_Y, touch_data->coords[i].y);
#ifdef TOUCH_FOD_SUPPORT
			if ((goodix_core_data->eventsdata & 0x08) != 0x08 || !driver_get_touch_mode(TOUCH_ID, DATA_MODE_10))
				touch_data->overlay = 0;
#endif
			input_report_abs(dev, ABS_MT_WIDTH_MAJOR, touch_data->overlay);
			input_report_abs(dev, ABS_MT_WIDTH_MINOR, touch_data->overlay);
			if (!__test_and_set_bit(i, &goodix_core_data->touch_id))
				ts_info("Finger ID[%d] Down", i);
		} else {
			input_mt_slot(dev, i);
			input_mt_report_slot_state(dev, MT_TOOL_FINGER, false);
			if (__test_and_clear_bit(i, &goodix_core_data->touch_id)
				&& touch_data->coords[i].status == TS_RELEASE)
				ts_info("Finger ID[%d] Up", i);
		}
	}

	if (touch_num && !pre_finger_num) {	/*first touch down */
		input_report_key(dev, BTN_TOUCH, 1);
		input_report_key(dev, BTN_TOOL_FINGER, 1);
	} else if (!touch_num && pre_finger_num) {	/*last touch up */
		goodix_core_data->fod_down_before_suspend = false;
		input_report_key(dev, BTN_TOUCH, 0);
		input_report_key(dev, BTN_TOOL_FINGER, 0);
#ifdef TOUCH_FOD_SUPPORT
		if (goodix_core_data->fod_finger) {
			ts_info("ts force fod up!");
			input_report_abs(dev, ABS_MT_WIDTH_MAJOR, 0);
			input_report_abs(dev, ABS_MT_WIDTH_MINOR, 0);
			input_sync(dev);
			update_fod_press_status(0);
			goodix_core_data->fod_finger = false;
		}
#endif
	}
	pre_finger_num = touch_num;

	input_sync(dev);
	mutex_unlock(&dev->mutex);
}

static int goodix_ts_request_handle(struct goodix_ts_core *cd,
	struct goodix_ts_event *ts_event)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int ret = -1;

	if (ts_event->request_code == REQUEST_TYPE_CONFIG)
		ret = goodix_send_ic_config(cd, CONFIG_TYPE_NORMAL);
	else if (ts_event->request_code == REQUEST_TYPE_RESET)
		ret = hw_ops->reset(cd, GOODIX_NORMAL_RESET_DELAY_MS);
	else
		ts_info("can not handle request type 0x%x",
			ts_event->request_code);
	if (ret)
		ts_err("failed handle request 0x%x",
			ts_event->request_code);
	else
		ts_info("success handle ic request 0x%x",
			ts_event->request_code);
	return ret;
}

static void goodix_sched_sethigh(struct task_struct *p)
{
	struct sched_param sp = { .sched_priority = MAX_RT_PRIO - 1 };
	WARN_ON_ONCE(sched_setscheduler_nocheck(p, SCHED_FIFO, &sp) != 0);
}


/**
 * goodix_ts_threadirq_func - Bottom half of interrupt
 * This functions is excuted in thread context,
 * sleep in this function is permit.
 *
 * @data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static irqreturn_t goodix_ts_threadirq_func(int irq, void *data)
{
	struct goodix_ts_core *core_data = data;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_ext_module *ext_module = NULL;
	struct goodix_ext_module *next = NULL;
	struct goodix_ts_event *ts_event = &core_data->ts_event;
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;
	int ret;

	ts_esd->irq_status = true;
	core_data->irq_trig_cnt++;

	pm_stay_awake(core_data->bus->dev);

#ifdef CONFIG_PM
	if (core_data->tp_pm_suspend) {
		ts_info("device in suspend, wait to resume");
		ret = wait_for_completion_timeout(&core_data->pm_resume_completion, msecs_to_jiffies(150));
		if (!ret) {
			pm_relax(core_data->bus->dev);
			ts_err("system can't finished resuming procedure");
			return IRQ_HANDLED;
		}
	}
#endif

#ifdef CONFIG_TOUCH_BOOST
	lpm_disable_for_dev(true, EVENT_INPUT);
#endif

	cpu_latency_qos_add_request(&core_data->pm_qos_req_irq, 0);

	/* inform external module */
	mutex_lock(&goodix_modules.mutex);
	list_for_each_entry_safe(ext_module, next,
				&goodix_modules.head, list) {
		if (!ext_module->funcs->irq_event)
			continue;
		ret = ext_module->funcs->irq_event(core_data, ext_module);
		if (ret == EVT_CANCEL_IRQEVT) {
			mutex_unlock(&goodix_modules.mutex);
#ifdef CONFIG_TOUCH_BOOST
			lpm_disable_for_dev(false, EVENT_INPUT);
#endif
			cpu_latency_qos_remove_request(&core_data->pm_qos_req_irq);
			pm_relax(core_data->bus->dev);
			return IRQ_HANDLED;
		}
	}

	if (core_data->work_status == TP_GESTURE) {
		ret = goodix_gesture_ist(core_data);
		if (ret == EVT_CANCEL_IRQEVT) {
			mutex_unlock(&goodix_modules.mutex);
#ifdef CONFIG_TOUCH_BOOST
			lpm_disable_for_dev(false, EVENT_INPUT);
#endif
			cpu_latency_qos_remove_request(&core_data->pm_qos_req_irq);
			pm_relax(core_data->bus->dev);
			return IRQ_HANDLED;
		}
	}
	mutex_unlock(&goodix_modules.mutex);

	/* read touch data from touch device */
	ret = hw_ops->event_handler(core_data, ts_event);
	if (likely(!ret)) {
		if (ts_event->event_type == EVENT_TOUCH) {
			/* report touch */
			goodix_ts_report_finger(core_data->input_dev,
					&ts_event->touch_data);
		}

		if (ts_event->event_type == EVENT_REQUEST) {
			goodix_ts_request_handle(core_data, ts_event);
		}
	}

	ts_event->retry = 0;
#ifdef CONFIG_TOUCH_BOOST
	lpm_disable_for_dev(false, EVENT_INPUT);
#endif

	if (!core_data->irq_priority_high) {
		goodix_sched_sethigh(current);
		core_data->irq_priority_high = true;
		ts_info("set goodix_irq priority");
	}

	cpu_latency_qos_remove_request(&core_data->pm_qos_req_irq);
	pm_relax(core_data->bus->dev);
	return IRQ_HANDLED;
}

/**
 * goodix_ts_init_irq - Requset interrput line from system
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_irq_setup(struct goodix_ts_core *core_data)
{
	const struct goodix_ts_board_data *ts_bdata = board_data(core_data);
	int ret;

	/* if ts_bdata-> irq is invalid */
	core_data->irq = gpio_to_irq(ts_bdata->irq_gpio);
	if (core_data->irq < 0) {
		ts_err("failed get irq num %d", core_data->irq);
		return -EINVAL;
	}

	ts_info("IRQ:%u,flags:%d", core_data->irq, (int)ts_bdata->irq_flags);
	ret = devm_request_threaded_irq(&core_data->pdev->dev,
					core_data->irq, NULL,
					goodix_ts_threadirq_func,
					ts_bdata->irq_flags | IRQF_ONESHOT,
					"xiaomi_tp" GOODIX_CORE_DRIVER_NAME,
					core_data);
	if (ret < 0)
		ts_err("Failed to requeset threaded irq:%d", ret);
	else
		atomic_set(&core_data->irq_enabled, 1);

	return ret;
}

/**
 * goodix_ts_power_init - Get regulator for touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_power_init(struct goodix_ts_core *core_data)
{
	struct goodix_ts_board_data *ts_bdata = board_data(core_data);
	struct device *dev = core_data->bus->dev;
	int ret = 0;

	ts_info("Power init");
	if (strlen(ts_bdata->avdd_name)) {
		core_data->avdd = devm_regulator_get(dev,
				ts_bdata->avdd_name);
		if (IS_ERR_OR_NULL(core_data->avdd)) {
			ret = PTR_ERR(core_data->avdd);
			ts_err("Failed to get regulator avdd:%d", ret);
			core_data->avdd = NULL;
			return ret;
		}
        //avdd voltage set at pineapple-regulators.dtsi, L14B
		/*
		ret = regulator_set_voltage(core_data->avdd, 3200000, 3200000);
		if (ret < 0) {
			ts_err("set avdd voltage failed");
			return ret;
		}
		*/
	} else {
		ts_info("Avdd name is NULL");
	}

	if (strlen(ts_bdata->iovdd_name)) {
		core_data->iovdd = devm_regulator_get(dev,
				ts_bdata->iovdd_name);
		if (IS_ERR_OR_NULL(core_data->iovdd)) {
			ret = PTR_ERR(core_data->iovdd);
			ts_err("Failed to get regulator iovdd:%d", ret);
			core_data->iovdd = NULL;
		}
		//iovdd voltage set at pineapple-regulators.dtsi, L12B
		/*
		ret = regulator_set_voltage(core_data->iovdd, 1800000, 1800000);
		if (ret < 0) {
			ts_err("set iovdd voltage failed");
			return ret;
		}
		*/
	} else {
		core_data->iovdd = NULL;
		ts_info("iovdd name is NULL");
	}

	return ret;
}
/**
 * goodix_ts_pinctrl_init - Get pinctrl handler and pinctrl_state
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_pinctrl_init(struct goodix_ts_core *core_data)
{
	int r = 0;

	/* get pinctrl handler from of node */
	core_data->pinctrl = devm_pinctrl_get(core_data->bus->dev);
	if (IS_ERR_OR_NULL(core_data->pinctrl)) {
		ts_info("Failed to get pinctrl handler[need confirm]");
		core_data->pinctrl = NULL;
		return -EINVAL;
	}
	/* active state */
	core_data->pin_sta_active = pinctrl_lookup_state(core_data->pinctrl,
				PINCTRL_STATE_ACTIVE);
	if (IS_ERR_OR_NULL(core_data->pin_sta_active)) {
		r = PTR_ERR(core_data->pin_sta_active);
		ts_err("Failed to get pinctrl state:%s, r:%d",
				PINCTRL_STATE_ACTIVE, r);
		core_data->pin_sta_active = NULL;
		goto exit_pinctrl_put;
	}

	/* suspend state */
	core_data->pin_sta_suspend = pinctrl_lookup_state(core_data->pinctrl,
				PINCTRL_STATE_SUSPEND);
	if (IS_ERR_OR_NULL(core_data->pin_sta_suspend)) {
		r = PTR_ERR(core_data->pin_sta_suspend);
		ts_err("Failed to get pinctrl state:%s, r:%d",
				PINCTRL_STATE_SUSPEND, r);
		core_data->pin_sta_suspend = NULL;
		goto exit_pinctrl_put;
	}

	ts_info("success get pinctrl state");

	return 0;
exit_pinctrl_put:
	devm_pinctrl_put(core_data->pinctrl);
	core_data->pinctrl = NULL;
	return r;
}

/**
 * goodix_ts_power_on - Turn on power to the touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
int goodix_ts_power_on(struct goodix_ts_core *cd)
{
	int ret = 0;

	ts_info("power on");
	if (cd->power_on)
		return 0;

	ret = cd->hw_ops->power_on(cd, true);
	if (!ret)
		cd->power_on = 1;
	else
		ts_err("failed power on, %d", ret);
	return ret;
}

/**
 * goodix_ts_power_off - Turn off power to the touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
int goodix_ts_power_off(struct goodix_ts_core *cd)
{
	int ret;

	ts_info("Device power off");
	if (!cd->power_on)
		return 0;

	ret = cd->hw_ops->power_on(cd, false);
	if (!ret)
		cd->power_on = 0;
	else
		ts_err("failed power off, %d", ret);

	return ret;
}

/**
 * goodix_ts_gpio_setup - Request gpio resources from GPIO subsysten
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_gpio_setup(struct goodix_ts_core *core_data)
{
	struct goodix_ts_board_data *ts_bdata = board_data(core_data);
	int r = 0;

	ts_info("GPIO setup,reset-gpio:%d, irq-gpio:%d",
		ts_bdata->reset_gpio, ts_bdata->irq_gpio);
	/*
	 * after kenerl3.13, gpio_ api is deprecated, new
	 * driver should use gpiod_ api.
	 */
	r = devm_gpio_request_one(&core_data->pdev->dev, ts_bdata->reset_gpio,
				  GPIOF_OUT_INIT_LOW, "ts_reset_gpio");
	if (r < 0) {
		ts_err("Failed to request reset gpio, r:%d", r);
		return r;
	}

	r = devm_gpio_request_one(&core_data->pdev->dev, ts_bdata->irq_gpio,
				  GPIOF_IN, "ts_irq_gpio");
	if (r < 0) {
		ts_err("Failed to request irq gpio, r:%d", r);
		return r;
	}

	if (ts_bdata->avdd_gpio > 0) {
		r = devm_gpio_request_one(&core_data->pdev->dev, ts_bdata->avdd_gpio,
				GPIOF_OUT_INIT_LOW, "ts_avdd_gpio");
		if (r < 0) {
			ts_err("Failed to request avdd-gpio, r:%d", r);
			return r;
		}
	}

	if (ts_bdata->iovdd_gpio > 0) {
		r = devm_gpio_request_one(&core_data->pdev->dev, ts_bdata->iovdd_gpio,
				GPIOF_OUT_INIT_LOW, "ts_iovdd_gpio");
		if (r < 0) {
			ts_err("Failed to request iovdd-gpio, r:%d", r);
			return r;
		}
	}

	return 0;
}

/**
 * goodix_ts_input_dev_config - Requset and config a input device
 *  then register it to input sybsystem.
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_input_dev_config(struct goodix_ts_core *core_data)
{
	struct goodix_ts_board_data *ts_bdata = board_data(core_data);
	struct input_dev *input_dev = NULL;
	int r;

	input_dev = input_allocate_device();
	if (!input_dev) {
		ts_err("Failed to allocated input device");
		return -ENOMEM;
	}

	core_data->input_dev = input_dev;
	input_set_drvdata(input_dev, core_data);

	input_dev->name = GOODIX_CORE_DRIVER_NAME;
	input_dev->phys = GOOIDX_INPUT_PHYS;
	input_dev->id.product = 0xDEAD;
	input_dev->id.vendor = 0xBEEF;
	input_dev->id.version = 10427;

	__set_bit(EV_SYN, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(KEY_WAKEUP, input_dev->keybit);
	__set_bit(KEY_GOTO, input_dev->keybit);
	__set_bit(BTN_TOOL_FINGER, input_dev->keybit);

#ifdef INPUT_PROP_DIRECT
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
#endif

	/* set input parameters */
	input_set_abs_params(input_dev, ABS_X,
				0, ts_bdata->panel_max_x / ts_bdata->super_resolution_factor - 1, 0, 0);
	input_set_abs_params(input_dev, ABS_Y,
				0, ts_bdata->panel_max_y / ts_bdata->super_resolution_factor - 1, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
				0, ts_bdata->panel_max_x / ts_bdata->super_resolution_factor - 1, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
				0, ts_bdata->panel_max_y / ts_bdata->super_resolution_factor - 1, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
				0, ts_bdata->panel_max_w, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR,
				0, 100, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MINOR,
				0, 100, 0, 0);
#ifdef INPUT_TYPE_B_PROTOCOL
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 7, 0)
	input_mt_init_slots(input_dev, GOODIX_MAX_TOUCH, INPUT_MT_DIRECT);
#else
	input_mt_init_slots(input_dev, GOODIX_MAX_TOUCH);
#endif
#endif

	input_set_capability(input_dev, EV_KEY, KEY_POWER);
	input_set_capability(input_dev, EV_KEY, KEY_WAKEUP);
	input_set_capability(input_dev, EV_KEY, KEY_GOTO);
	r = input_register_device(input_dev);
	if (r < 0) {
		ts_err("Unable to register input device");
		input_free_device(input_dev);
		return r;
	}

	return 0;
}



/**
 * goodix_ts_esd_work - check hardware status and recovery
 *  the hardware if needed.
 */
static void goodix_ts_esd_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct goodix_ts_esd *ts_esd = container_of(dwork,
			struct goodix_ts_esd, esd_work);
	struct goodix_ts_core *cd = container_of(ts_esd,
			struct goodix_ts_core, ts_esd);
	const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int ret = 0;

#ifdef TOUCH_TRUSTED_SUPPORT
	if (cd->tui_process) {
		if (wait_for_completion_interruptible(&cd->tui_finish)) {
			ts_err("cautious, ERESTARTSYS may cause cmd loss recomand try again");
			return;
		}
		ts_info("wait finished, its time to go ahead");
	}
#endif // TOUCH_TRUSTED_SUPPORT

	if (ts_esd->irq_status)
		goto exit;

	if (!atomic_read(&ts_esd->esd_on))
		return;

	if (!hw_ops->esd_check)
		return;

	ret = hw_ops->esd_check(cd);
	if (ret) {
		ts_err("esd check failed");
		goodix_ts_power_off(cd);
		goodix_ts_power_on(cd);
	}

exit:
	ts_esd->irq_status = false;
	if (atomic_read(&ts_esd->esd_on))
		schedule_delayed_work(&ts_esd->esd_work, 3 * HZ);
}

/**
 * goodix_ts_esd_on - turn on esd protection
 */
static void goodix_ts_esd_on(struct goodix_ts_core *cd)
{
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_esd *ts_esd = &cd->ts_esd;

	if (!misc->esd_addr)
		return;

	if (atomic_read(&ts_esd->esd_on))
		return;

	atomic_set(&ts_esd->esd_on, 1);
	if (!schedule_delayed_work(&ts_esd->esd_work, 3 * HZ)) {
		ts_info("esd work already in workqueue");
	}
	ts_info("esd on");
}

/**
 * goodix_ts_esd_off - turn off esd protection
 */
static void goodix_ts_esd_off(struct goodix_ts_core *cd)
{
	struct goodix_ts_esd *ts_esd = &cd->ts_esd;
	int ret;

	if (!atomic_read(&ts_esd->esd_on))
		return;

	atomic_set(&ts_esd->esd_on, 0);
	ret = cancel_delayed_work_sync(&ts_esd->esd_work);
	ts_info("Esd off, esd work state %d", ret);
}

/**
 * goodix_esd_notifier_callback - notification callback
 *  under certain condition, we need to turn off/on the esd
 *  protector, we use kernel notify call chain to achieve this.
 *
 *  for example: before firmware update we need to turn off the
 *  esd protector and after firmware update finished, we should
 *  turn on the esd protector.
 */
static int goodix_esd_notifier_callback(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct goodix_ts_esd *ts_esd = container_of(nb,
			struct goodix_ts_esd, esd_notifier);

	switch (action) {
	case NOTIFY_FWUPDATE_START:
	case NOTIFY_SUSPEND:
	case NOTIFY_ESD_OFF:
		goodix_ts_esd_off(ts_esd->ts_core);
		break;
	case NOTIFY_FWUPDATE_FAILED:
	case NOTIFY_FWUPDATE_SUCCESS:
	case NOTIFY_RESUME:
	case NOTIFY_ESD_ON:
		goodix_ts_esd_on(ts_esd->ts_core);
		break;
	default:
		break;
	}

	return 0;
}

/**
 * goodix_ts_esd_init - initialize esd protection
 */
int goodix_ts_esd_init(struct goodix_ts_core *cd)
{
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_esd *ts_esd = &cd->ts_esd;

	if (!cd->hw_ops->esd_check || !misc->esd_addr) {
		ts_info("missing key info for esd check");
		return 0;
	}

	INIT_DELAYED_WORK(&ts_esd->esd_work, goodix_ts_esd_work);
	ts_esd->ts_core = cd;
	atomic_set(&ts_esd->esd_on, 0);
	ts_esd->esd_notifier.notifier_call = goodix_esd_notifier_callback;
	goodix_ts_register_notifier(&ts_esd->esd_notifier);
	goodix_ts_esd_on(cd);

	return 0;
}

static void goodix_cmd_fifo_get(u64 address, u8 *buf)
{
	struct goodix_ts_hw_ops *hw_ops = goodix_core_data->hw_ops;
	struct goodix_ic_info_misc *misc = &goodix_core_data->ic_info.misc;

	int node_data_size = 256;
	int ret;
	int pos = 0;
	int i;
	int j;
	u8 *read_buff = NULL;

	read_buff = (u8*)kzalloc(node_data_size, GFP_KERNEL);

	if (!read_buff) {
		ts_err("fail memory");
		return;
	}
	if (!buf) {
		ts_err("fail memory");
		goto end;
	}
	ret = hw_ops->read(goodix_core_data, misc->frame_data_addr,
		read_buff, node_data_size);
	if (ret) {
		ts_err("failed get frame data");
		goto end;
	}

	for (i = 0; i < 16; i++) {
		for (j = 0; j < 16; j++) {
			pos += snprintf(buf + pos, PAGE_SIZE, "%02x ",
			     read_buff[i * 16 + j]);
		}
		buf[pos++] = '\n';
	}
end:
	if (read_buff)
		kfree(read_buff);
}

static int goodix_touch_doze_analysis(int input)
{
	int error =  0;
	int irq_status = 0;
	int update_flag = UPDATE_MODE_FORCE | UPDATE_MODE_BLOCK | UPDATE_MODE_SRC_REQUEST;
	const struct goodix_ts_board_data *ts_bdata = NULL;

	ts_info("input: %d", input);
	if (!goodix_core_data) {
		ts_err("cd is null");
		return -EIO;
	}
	ts_bdata = board_data(goodix_core_data);
	switch(input) {
		case TOUCH_0:
			goodix_core_data->doze_test = 1;
			goodix_resume_suspend(false, xiaomi_get_gesture_type(TOUCH_ID));
			goodix_resume_suspend(true, xiaomi_get_gesture_type(TOUCH_ID));
			goodix_core_data->doze_test = 0;
		break;
		case TOUCH_1:
			error = goodix_do_fw_update(NULL, update_flag);
			if (!error)
				ts_info("success do update work");
		break;
		case TOUCH_2:
			enable_irq(goodix_core_data->irq);
		break;
		case TOUCH_3:
			disable_irq(goodix_core_data->irq);
		break;
		case TOUCH_4:
			error = devm_request_threaded_irq(&goodix_core_data->pdev->dev,
							goodix_core_data->irq, NULL,
							goodix_ts_threadirq_func,
							ts_bdata->irq_flags | IRQF_ONESHOT,
							"xiaomi_tp" GOODIX_CORE_DRIVER_NAME,
							goodix_core_data);
			if (error < 0)
				ts_err("Failed to requeset threaded irq:%d", error);
			else
				enable_irq(goodix_core_data->irq);
		break;
		case TOUCH_5:
			irq_status = gpio_get_value(ts_bdata->irq_gpio) == 0 ? 0 : 1;
		break;
		case TOUCH_6:
			schedule_resume_suspend_work(TOUCH_ID, false);
		break;
		case TOUCH_7:
			schedule_resume_suspend_work(TOUCH_ID, true);
		break;
		case TOUCH_8:
			error = goodix_ts_power_on(goodix_core_data);
			if (error < 0)
				ts_err("Failed to enable regulators");
		break;
		case TOUCH_9:
			error = goodix_ts_power_off(goodix_core_data);
			if (error < 0)
				ts_err("Failed to disable regulators");
		break;
		default:
			ts_err("don't support touch doze analysis");
			break;
	}
	return irq_status;
}
#ifdef CONFIG_TOUCH_FACTORY_BUILD
static bool cmd_flag = true;
void ts_test_cmd_enable(bool en) {
	cmd_flag = en;
}
#endif

static int goodix_htc_ic_setModeValue(common_data_t *common_data)
{
	int data_len = common_data->data_len -1;
	unsigned int addr = common_data->data_buf[0];
	unsigned char *data_buf = (char *)&common_data->data_buf[1];
	int ret;
#ifdef CONFIG_TOUCH_FACTORY_BUILD
	ts_debug("cmd_flag:%d", cmd_flag);
	if (!cmd_flag) {
		ts_err("tp self test in progress, reject htc_ic_setModeValue");
		return 0;
	}
#endif

	ts_debug("setModeValue in, cmd = %d, mode = %d, addr = 0x%x", common_data->cmd, common_data->mode, addr);
	if (!goodix_core_data) {
		ts_err("not inited");
		return -EFAULT;
	}
	if (data_len > sizeof(s32) * CMD_DATA_BUF_SIZE) {
		ts_err("data length is over the limit");
		return -EFAULT;
	}

	ret = goodix_core_data->hw_ops->write(goodix_core_data, addr, data_buf, data_len);
	if (ret < 0) {
		ts_err("failed write addr(%x) data 0x%*ph", addr,
			data_len, data_buf);
	}
	ts_debug("setModeValue out, cmd = %d, mode = %d, value:0x%*ph", common_data->cmd, common_data->mode, data_len, data_buf);
	return 0;
}

static int goodix_htc_ic_getModeValue(common_data_t *common_data)
{
	int data_len = common_data->data_len;
	unsigned int addr = common_data->data_buf[0];
	unsigned char *data_buf = (char *)&common_data->data_buf[1];
	int ret;

#ifdef CONFIG_TOUCH_FACTORY_BUILD
	ts_debug("cmd_flag:%d", cmd_flag);
	if (!cmd_flag) {
		ts_err("tp self test in progress, reject htc_ic_getModeValue");
		return 0;
	}
#endif
	ts_info("getModeValue in, addr = 0x%x, data length = %d", addr, data_len);
	if (!goodix_core_data) {
		ts_err("not inited");
		return -EFAULT;
	}
	if (data_len > sizeof(s32) * (CMD_DATA_BUF_SIZE - 1)) {
		ts_err("data length is over the limit");
		return -EFAULT;
	}

	ret = goodix_core_data->hw_ops->read(goodix_core_data, addr, data_buf, data_len);
	if (ret) {
		ts_err("can't get, error");
		return -EINVAL;
	}

	ts_info("getModeValue out, value:0x%*ph", data_len, data_buf);
	return 0;
}




/**
 * goodix_ts_suspend - Touchscreen suspend function
 * Called by PM/FB/EARLYSUSPEN module to put the device to  sleep
 */
static int goodix_ts_suspend(struct goodix_ts_core *core_data)
{
	struct goodix_ext_module *ext_module = NULL;
	struct goodix_ext_module *next = NULL;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
#ifdef TOUCH_FOD_SUPPORT
#ifdef TOUCH_THP_SUPPORT
	struct input_dev *input_dev = core_data->input_dev;
#endif
#endif
	int error = 0;
	int ret;
#ifdef TOUCH_TRUSTED_SUPPORT
	struct qts_data *qts_data = NULL;
	qts_data = get_qts_data_helper(&qts_vendor_data);

	qts_ts_suspend(qts_data);
#endif // TOUCH_TRUSTED_SUPPORT

	if (core_data->init_stage < CORE_INIT_STAGE2 ||
			atomic_read(&core_data->suspended))
		return 0;
	mutex_lock(&core_data->core_mutex);
	ts_info("Suspend start");
	atomic_set(&core_data->suspended, 1);
	enable_temperature_detection_func(false);
	core_data->irq_trig_cnt = 0;

	/*
	 * notify suspend event, inform the esd protector
	 * and charger detector to turn off the work
	 */
	goodix_ts_blocking_notify(NOTIFY_SUSPEND, NULL);

	hw_ops->irq_enable(core_data, false);
	/* inform external module */
	mutex_lock(&goodix_modules.mutex);
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (!ext_module->funcs->before_suspend)
				continue;
			ret = ext_module->funcs->before_suspend(core_data,ext_module);
			if (ret == EVT_CANCEL_SUSPEND) {
				mutex_unlock(&goodix_modules.mutex);
				ts_info("Canceled by module:%s",
					ext_module->name);
				goto out;
			}
		}
	}

	if(core_data->doze_test == 1) {
		error = goodix_ts_power_off(core_data);
		if (error < 0)
			ts_err("ERROR Failed to enable regulators\n");
	}
	mutex_unlock(&goodix_modules.mutex);

	if (core_data->gesture_enabled) {
		gsx_gesture_before_suspend(core_data);
		goto out;
	}

	core_data->work_status = TP_SLEEP;
	/* enter sleep mode or power off */
	if (hw_ops->suspend)
		hw_ops->suspend(core_data);

	/*if (core_data->pinctrl) {
		ret = pinctrl_select_state(core_data->pinctrl,
				core_data->pin_sta_suspend);
		if (ret < 0)
			ts_err("Failed to select active pinstate, ret:%d", ret);
	}*/

	/* inform exteranl modules */
	mutex_lock(&goodix_modules.mutex);
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (!ext_module->funcs->after_suspend)
				continue;

			ret = ext_module->funcs->after_suspend(core_data, ext_module);
			if (ret == EVT_CANCEL_SUSPEND) {
				mutex_unlock(&goodix_modules.mutex);
				ts_info("Canceled by module:%s",
					ext_module->name);
				goto out;
			}
		}
	}
	mutex_unlock(&goodix_modules.mutex);

out:
	ts_info("tp work status: %d", core_data->work_status);
#ifdef CONFIG_TOUCH_BOOST
	lpm_disable_for_dev(false, EVENT_INPUT);
#endif

#ifdef TOUCH_THP_SUPPORT
	if (core_data->enable_touch_raw) {
#ifdef TOUCH_FOD_SUPPORT
		if (core_data->fod_finger) {
			mutex_lock(&input_dev->mutex);
			core_data->fod_finger = false;

#ifdef TYPE_B_PROTOCOL
			input_mt_slot(input_dev, 9);
			input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
#endif
			input_report_key(input_dev, BTN_TOUCH, 0);
			input_report_key(input_dev, BTN_TOOL_FINGER, 0);
			input_report_abs(input_dev, ABS_MT_WIDTH_MAJOR, 0);
			input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, 0);
			input_sync(input_dev);
			update_fod_press_status(0);
			ts_info("ts fod up for suspend");
			mutex_unlock(&input_dev->mutex);
		}
#endif
		core_data->fod_finger = false;
	}
#endif


#ifdef CONFIG_TOUCH_FACTORY_BUILD
	goodix_ts_power_off(core_data);
#endif

	ts_info("Suspend end");
	mutex_unlock(&core_data->core_mutex);
	return 0;
}

static void goodix_reset_charge_state(struct goodix_ts_core *core_data, int state)
{
	if (state)
		core_data->hw_ops->charger_on(goodix_core_data, true);
	else
		core_data->hw_ops->charger_on(goodix_core_data, false);
	ts_info("%s state:%d", __func__, state);
}

/**
 * goodix_ts_resume - Touchscreen resume function
 * Called by PM/FB/EARLYSUSPEN module to wakeup device
 */
static int goodix_ts_resume(struct goodix_ts_core *core_data)
{
	struct goodix_ext_module *ext_module = NULL;
	struct goodix_ext_module *next = NULL;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int error = 0;
	int ret;
#ifdef TOUCH_TRUSTED_SUPPORT
	struct qts_data *qts_data = NULL;
	qts_data = get_qts_data_helper(&qts_vendor_data);

	qts_ts_resume(qts_data);
#endif // TOUCH_TRUSTED_SUPPORT

	if (core_data->init_stage < CORE_INIT_STAGE2 ||
			!atomic_read(&core_data->suspended)) {
		ts_err("already resumed!");
		return 0;
	}
	mutex_lock(&core_data->core_mutex);
	ts_info("Resume start");
	atomic_set(&core_data->suspended, 0);
	core_data->irq_trig_cnt = 0;

#ifdef CONFIG_TOUCH_FACTORY_BUILD
	goodix_ts_power_on(core_data);
#endif

	mutex_lock(&goodix_modules.mutex);
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (!ext_module->funcs->before_resume)
				continue;

			ret = ext_module->funcs->before_resume(core_data,
					ext_module);
			if (ret == EVT_CANCEL_RESUME) {
				mutex_unlock(&goodix_modules.mutex);
				ts_info("Canceled by module:%s",
					ext_module->name);
				goto out;
			}
		}
	}
	mutex_unlock(&goodix_modules.mutex);

	if (core_data->gesture_enabled || core_data->fod_finger) {
		gsx_gesture_before_resume(core_data);
		goto out;
	}

	if (core_data->pinctrl) {
		ret = pinctrl_select_state(core_data->pinctrl,
					core_data->pin_sta_active);
		if (ret < 0)
			ts_err("Failed to select active pinstate, ret:%d", ret);
	}
	if(core_data->doze_test == 1) {
		error = goodix_ts_power_on(core_data);
		if (error < 0)
			ts_err("ERROR Failed to enable regulators\n");
	}

	/* reset device or power on*/
	if (hw_ops->resume)
		hw_ops->resume(core_data);

	mutex_lock(&goodix_modules.mutex);
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (!ext_module->funcs->after_resume)
				continue;

			ret = ext_module->funcs->after_resume(core_data,ext_module);
			if (ret == EVT_CANCEL_RESUME) {
				mutex_unlock(&goodix_modules.mutex);
				ts_info("Canceled by module:%s",
					ext_module->name);
				goto out;
			}
		}
	}
	mutex_unlock(&goodix_modules.mutex);

out:
	core_data->work_status = TP_NORMAL;
	enable_temperature_detection_func(true);
	goodix_set_thermal_temp(0, true);
	/* check if cfg needs to be updated */
	if (core_data->need_update_cfg > 0) {
		ts_info("update cfg of state:%d, retry %d",
			core_data->cfg_cloud_state, core_data->need_update_cfg);
		core_data->need_update_cfg++;
		ret = goodix_update_cfg(core_data, core_data->cfg_cloud_state);
		if (!ret || core_data->need_update_cfg > UPDATE_CFG_MAX_NUM) {
			core_data->need_update_cfg = 0;
		}
	}
	/* enable irq */
	hw_ops->irq_enable(core_data, true);
	/* open esd */
	goodix_ts_blocking_notify(NOTIFY_RESUME, NULL);
	/* update ic_info */
	hw_ops->get_ic_info(core_data, &core_data->ic_info);
	goodix_reset_charge_state(core_data, core_data->charger_status);
	ts_info("Resume end");
	mutex_unlock(&core_data->core_mutex);
	return 0;
}

static int goodix_resume_suspend(bool is_resume, u8 gesture_type)
{
	if (is_resume) {
		return goodix_ts_resume(goodix_core_data);
	}
	return goodix_ts_suspend(goodix_core_data);
}

#ifdef CONFIG_FB
/**
 * goodix_ts_fb_notifier_callback - Framebuffer notifier callback
 * Called by kernel during framebuffer blanck/unblank phrase
 */
int goodix_ts_fb_notifier_callback(struct notifier_block *self,
	unsigned long event, void *data)
{
	struct goodix_ts_core *core_data =
		container_of(self, struct goodix_ts_core, fb_notifier);
	struct fb_event *fb_event = data;

	if (fb_event && fb_event->data && core_data) {
		if (event == FB_EARLY_EVENT_BLANK) {
			/* before fb blank */
		} else if (event == FB_EVENT_BLANK) {
			int *blank = fb_event->data;
			if (*blank == FB_BLANK_UNBLANK)
				goodix_ts_resume(core_data);
			else if (*blank == FB_BLANK_POWERDOWN)
				goodix_ts_suspend(core_data);
		}
	}

	return 0;
}
#endif


#ifdef CONFIG_PM
/**
 * goodix_ts_pm_suspend - PM suspend function
 * Called by kernel during system suspend phrase
 */
static int goodix_ts_pm_suspend(struct device *dev)
{
	struct goodix_ts_core *core_data =
		dev_get_drvdata(dev);

	ts_info("enter");

	if (device_may_wakeup(dev) && core_data->gesture_enabled) {
		enable_irq_wake(core_data->irq);
	}
	core_data->tp_pm_suspend = true;
	reinit_completion(&core_data->pm_resume_completion);
	return 0;
}
/**
 * goodix_ts_pm_resume - PM resume function
 * Called by kernel during system wakeup
 */
static int goodix_ts_pm_resume(struct device *dev)
{
	struct goodix_ts_core *core_data =
		dev_get_drvdata(dev);
	ts_info("enter");

	if (device_may_wakeup(dev) && core_data->gesture_enabled) {
		disable_irq_wake(core_data->irq);
	}
	core_data->tp_pm_suspend = false;
	complete(&core_data->pm_resume_completion);
	return 0;
}
#endif

/**
 * goodix_generic_noti_callback - generic notifier callback
 *  for goodix touch notification event.
 */
static int goodix_generic_noti_callback(struct notifier_block *self,
		unsigned long action, void *data)
{
	struct goodix_ts_core *cd = container_of(self,
			struct goodix_ts_core, ts_notifier);
	const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	if (cd->init_stage < CORE_INIT_STAGE2)
		return 0;

	ts_info("notify event type 0x%x", (unsigned int)action);
	switch (action) {
	case NOTIFY_FWUPDATE_START:
		hw_ops->irq_enable(cd, 0);
		break;
	case NOTIFY_FWUPDATE_SUCCESS:
	case NOTIFY_FWUPDATE_FAILED:
		if (hw_ops->read_version(cd, &cd->fw_version))
			ts_info("failed read fw version info[ignore]");
		hw_ops->irq_enable(cd, 1);
		break;
	default:
		break;
	}
	return 0;
}

#ifdef TOUCH_THP_SUPPORT
#ifdef TOUCH_FOD_SUPPORT
static void goodix_set_fod_downup(struct goodix_ts_core *cd, int enable)
{
	if (enable) {
		ts_info("ts fod down");
		goodix_core_data->fod_finger = true;
		update_fod_press_status(1);
	} else {
		ts_info("ts fod up");
		goodix_core_data->fod_finger = false;
		update_fod_press_status(0);
	}
}
static void goodix_thp_signal_work(struct work_struct *work)
{
	struct goodix_ts_core *cd = container_of(work, struct goodix_ts_core, thp_signal_work.work);

	if (!cd) {
		ts_err("core data not init");
		return;
	}
	if (!cd->enable_touch_raw) {
		ts_info("not enable touch raw");
		return;
	}
#ifdef CONFIG_TOUCH_FACTORY_BUILD
	{
		int fod_en = 1;
		ts_info("notify fod enable to hal");
		add_common_data_to_buf(0, SET_CUR_VALUE, DATA_MODE_10, 1, &fod_en);
	}
#endif
}
#endif
#endif


static void goodix_self_check(struct work_struct *work)
{
	struct goodix_ts_core *cd =
			container_of(work, struct goodix_ts_core, self_check_work);
	u32 fw_state_addr = cd->ic_info.misc.fw_state_addr;
	int update_flag = UPDATE_MODE_BLOCK | UPDATE_MODE_SRC_REQUEST | UPDATE_MODE_FORCE;
	u8 cur_cycle_cnt = 0;
	u8 pre_cycle_cnt = 0;
	int err_cnt = 0;
	int retry = 5;

	while (retry--) {
		cd->hw_ops->read(cd, fw_state_addr, &cur_cycle_cnt, 1);
		if (cur_cycle_cnt == pre_cycle_cnt)
			err_cnt++;
		pre_cycle_cnt = cur_cycle_cnt;
		msleep(20);
	}
	if (err_cnt > 1) {
		ts_err("Warning! The firmware maybe running abnormal, need upgrade.");
		goodix_get_config_proc(cd);
		goodix_do_fw_update(cd->ic_configs[CONFIG_TYPE_NORMAL], update_flag);
		cd->hw_ops->read_version(cd, &cd->fw_version);
		cd->hw_ops->get_ic_info(cd, &cd->ic_info);
	}
}


#ifdef TOUCH_TRUSTED_SUPPORT
struct spi_device *qts_spi = NULL;
void goodix_set_spi_device(struct spi_device *spi) {
	if (!spi) {
		ts_err("spi is NULL.");
	}
	qts_spi = spi;
}

static int goodix_enable_touch_irq(void *client_data, bool enable)
{
	struct goodix_ts_core *core_data = client_data;
	if (IS_ERR_OR_NULL(core_data))
		return -EINVAL;
	return core_data->hw_ops->irq_enable(core_data, enable);
}

static int goodix_get_irq_num(void *client_data)
{
	struct goodix_ts_core *core_data = client_data;
	if (IS_ERR_OR_NULL(core_data))
		return -EINVAL;
	return core_data->irq;
}

static int goodix_pre_la_tui_enable(void *client_data)
{
	struct goodix_ts_core *core_data = client_data;
	int ret = 0;

	ts_debug("enter");
	if (IS_ERR_OR_NULL(core_data))
		return -EINVAL;
	goodix_htc_enable(0);

	ret = goodix_htc_set_active_scan_rate(120); /* 120Hz, must be after goodix_htc_enable, otherwise setting will fail */
	if (ret)
		ts_info("Failed to set report rate, ret:%d", ret);

	core_data->tui_process = true;
	reinit_completion(&core_data->tui_finish);
	return 0;
}

static int goodix_post_la_tui_disable(void *client_data)
{
	struct goodix_ts_core *core_data = client_data;
	int ret = 0;

	ts_debug("enter");
	if (IS_ERR_OR_NULL(core_data))
		return -EINVAL;
	goodix_htc_enable(1);

	ret = goodix_htc_set_active_scan_rate(120); /* Restore default 240Hz */
	if (ret)
		ts_info("Failed to set report rate, ret:%d", ret);

	complete_all(&core_data->tui_finish);
	core_data->tui_process = false;
	return 0;
}

static int goodix_pre_la_tui_disable(void *client_data)
{
	struct goodix_ts_core *core_data = client_data;

	ts_debug("enter");
	if (IS_ERR_OR_NULL(core_data))
		return -EINVAL;
	return 0;
}

static int goodix_post_la_tui_enable(void *client_data)
{
	struct goodix_ts_core *core_data = client_data;

	ts_debug("enter");
	if (IS_ERR_OR_NULL(core_data))
		return -EINVAL;
	return 0;
}

static void goodix_fill_qts_vendor_data(struct qts_vendor_data *qts_vendor_data,
		struct goodix_ts_core *core_data)
{
	struct spi_device *spi = NULL;
	struct device_node *node = NULL;
	const char *touch_type;
	int rc = 0;

	ts_debug("enter");

	if (!qts_vendor_data || !qts_spi) {
		ts_err("Params is NULL.\n");
		return;
	}

	spi = qts_spi;
	node = spi->dev.of_node;

	if ((qts_touch_type != NULL && strlen(qts_touch_type) == 0) || (qts_touch_type == NULL)) {
		ts_err("No touch type, try again\n");
		rc = of_property_read_string(node, "goodix,touch-type", &touch_type);
		if (rc) {
			ts_err("No touch type\n");
			return;
		}
	} else {
		ts_err("touch type:%s\n", qts_touch_type);
		touch_type = qts_touch_type;
	}

	if (!strcmp(touch_type, "primary")) {
		ts_debug("primary");
		qts_vendor_data->client_type = QTS_CLIENT_PRIMARY_TOUCH;
	} else {
		ts_debug("secondary");
		qts_vendor_data->client_type = QTS_CLIENT_SECONDARY_TOUCH;
	}

	qts_vendor_data->client = NULL;
	qts_vendor_data->spi = spi;
	qts_vendor_data->bus_type = QTS_BUS_TYPE_SPI;

	qts_vendor_data->vendor_data = core_data;
	qts_vendor_data->qts_vendor_ops.enable_touch_irq = goodix_enable_touch_irq;
	qts_vendor_data->qts_vendor_ops.get_irq_num = goodix_get_irq_num;
	qts_vendor_data->qts_vendor_ops.pre_la_tui_enable = goodix_pre_la_tui_enable;
	qts_vendor_data->qts_vendor_ops.post_la_tui_enable = goodix_post_la_tui_enable;
	qts_vendor_data->qts_vendor_ops.pre_la_tui_disable = goodix_pre_la_tui_disable;
	qts_vendor_data->qts_vendor_ops.post_la_tui_disable = goodix_post_la_tui_disable;
}
#endif // TOUCH_TRUSTED_SUPPORT

int goodix_ts_stage2_init(struct goodix_ts_core *cd)
{
	int ret;

	/*init report mutex lock */
	mutex_init(&cd->report_mutex);
	/* alloc/config/register input device */
	ret = goodix_ts_input_dev_config(cd);
	if (ret < 0) {
		ts_err("failed set input device");
		return ret;
	}


	/* request irq line */
	ret = goodix_ts_irq_setup(cd);
	if (ret < 0) {
		ts_info("failed set irq");
		goto exit;
	}
	ts_info("success register irq");

#ifdef CONFIG_FB
	cd->fb_notifier.notifier_call = goodix_ts_fb_notifier_callback;
	if (fb_register_client(&cd->fb_notifier))
		ts_err("Failed to register fb notifier client:%d", ret);
#endif

#ifdef TOUCH_THP_SUPPORT
#ifdef TOUCH_FOD_SUPPORT
	INIT_DELAYED_WORK(&cd->thp_signal_work, goodix_thp_signal_work);
#endif
#endif

	/* get ts lockdown info */
	goodix_ts_get_lockdown_info(cd);

	/* create sysfs files */
	goodix_ts_sysfs_init(cd);

	/* create procfs files */
	goodix_ts_procfs_init(cd);

	/* esd protector */
	goodix_ts_esd_init(cd);

	/* gesture init */
	gesture_module_init();

	/* inspect init */
	inspect_module_init(cd);

	/* Do self check on first boot */
	INIT_WORK(&cd->self_check_work, goodix_self_check);
	schedule_work(&cd->self_check_work);

#ifdef TOUCH_FOD_SUPPORT
#ifdef CONFIG_TOUCH_FACTORY_BUILD
	set_touch_mode(DATA_MODE_10, 1);
#else
	set_touch_mode(DATA_MODE_10, 0);
#endif
#endif
	ts_info("finished");
	return 0;

exit:
	if (cd->input_dev) {
		input_unregister_device(cd->input_dev);
		input_free_device(cd->input_dev);
		cd->input_dev = NULL;
	}
	return ret;
}

/* try send the config specified with type */
static int goodix_send_ic_config(struct goodix_ts_core *cd, int type)
{
	u32 config_id;
	struct goodix_ic_config *cfg;
	int res = 0;

	if (type >= GOODIX_MAX_CONFIG_GROUP) {
		ts_err("unsupproted config type %d", type);
		return -EINVAL;
	}

	cfg = cd->ic_configs[type];
	if (!cfg || cfg->len <= 0) {
		ts_info("no valid normal config found");
		return -EINVAL;
	}

	config_id = goodix_get_file_config_id(cfg->data);
	if (cd->ic_info.version.config_id == config_id) {
		ts_info("config id is equal 0x%x, skiped", config_id);
		return 0;
	}

	res = goodix_get_self_config_state(config_id, cd->ic_info.version.config_id);
	if (res != SELF_UNKNOWN && cd->need_update_cfg == 0) {
		ts_info("just update waterproof cfg (current: 0x%x) when power on, skipped",
			cd->ic_info.version.config_id);
		return 0;
	} else if (res == SELF_UNKNOWN && cd->need_update_cfg > 0) {
		ts_info("not the cfg (current: 0x%x) required for cloud control, skipped",
			cd->ic_info.version.config_id);
		return 0;
	}

	ts_info("try send config, id=0x%x", config_id);
	return cd->hw_ops->send_config(cd, cfg->data, cfg->len);
}

static int goodix_match_panel(struct goodix_ts_core *cd)
{
	strlcpy(cd->board_data.limit_csv_name, TS_DEFAULT_LIMIT_CSV,
		GOODIX_MAX_STR_LABLE_LEN);

	return 0;
}

static int goodix_lockdown_info_read(u8 *lockdown_info_buf)
{
	if (!goodix_core_data)
	if (!goodix_core_data)
		return -1;

	for (int i = 0; i < GOODIX_LOCKDOWN_SIZE; i++)
		lockdown_info_buf[i] = goodix_core_data->lockdown_info[i];

	return 0;
}

static int goodix_fw_version_info_read(char *buf)
{
	struct goodix_ts_hw_ops *hw_ops = goodix_core_data->hw_ops;
	struct goodix_fw_version chip_ver;
	int ret = 0;
	int cnt = 0;

	if (!hw_ops)
		return -1;
	if (hw_ops->read_version) {
		ret = hw_ops->read_version(goodix_core_data, &chip_ver);
		if (!ret) {
			cnt = snprintf(&buf[cnt], 64 - cnt,
					"patch_pid:%s\n",
					chip_ver.patch_pid);
			cnt += snprintf(&buf[cnt], 64 - cnt,
					"patch_vid:%02x%02x%02x%02x\n",
					chip_ver.patch_vid[0], chip_ver.patch_vid[1],
					chip_ver.patch_vid[2], chip_ver.patch_vid[3]);
		} else {
			ts_err("read fw version failed, cnt value: %d", cnt);
		}
	}

	if (hw_ops->get_ic_info) {
		ret = hw_ops->get_ic_info(goodix_core_data, &goodix_core_data->ic_info);
		if (!ret) {
			cnt += snprintf(&buf[cnt], 64 - cnt,
					"config_version:%x\n", goodix_core_data->ic_info.version.config_version);
		} else {
			ts_err("get ic info failed, cnt value: %d", cnt);
		}
	}
	return 0;
}

static int goodix_short_open_test(void)
{
	struct ts_rawdata_info *info = NULL;
	int test_result;

	info = vzalloc(sizeof(*info));
	if (!info) {
		ts_err("Failed to alloc rawdata info memory");
		return GTP_RESULT_INVALID;
	}

	if (goodix_get_rawdata(&goodix_core_data->pdev->dev, info)) {
		ts_err("Factory_test FAIL");
		test_result = GTP_RESULT_INVALID;
		goto exit;
	}

	if (80 == (*(info->result + 1))) {
		ts_info("test PASS!");
		test_result = GTP_RESULT_PASS;
	} else {
		ts_err("test FAILED!");
		test_result = GTP_RESULT_FAIL;
	}

exit:
	ts_info("resultInfo: %s", info->result);
	/* ret = snprintf(buf, PAGE_SIZE, "resultInfo: %s", info->result); */

	vfree(info);
	return test_result;
}

static int goodix_ic_self_test(char *type, int *result)
{
	struct goodix_fw_version chip_ver;
	struct goodix_ts_hw_ops *hw_ops;
	int retval = 0;

	if (!goodix_core_data)
		return GTP_RESULT_INVALID;
	else
		hw_ops = goodix_core_data->hw_ops;

	if (!strncmp("short", type, 5) || !strncmp("open", type, 4)) {
		retval = goodix_short_open_test();
	} else if (!strncmp("i2c", type, 3)) {
		hw_ops->read_version(goodix_core_data, &chip_ver);
		if (chip_ver.sensor_id == 255)
			retval = GTP_RESULT_PASS;
		else
			retval = GTP_RESULT_FAIL;
	}

	*result = retval;

	return 0;
}

int goodix_ts_get_lockdown_info(struct goodix_ts_core *cd)
{
	int ret = 0;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	ret = hw_ops->read(cd, TS_LOCKDOWN_REG,
				cd->lockdown_info, GOODIX_LOCKDOWN_SIZE);
	if (ret) {
		ts_err("can't get lockdown");
		return -EINVAL;
	}

	ts_info("lockdown is:0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x",
			cd->lockdown_info[0], cd->lockdown_info[1],
			cd->lockdown_info[2], cd->lockdown_info[3],
			cd->lockdown_info[4], cd->lockdown_info[5],
			cd->lockdown_info[6], cd->lockdown_info[7]);
	return 0;
}

#ifdef TOUCH_FOD_SUPPORT
static void  goodix_xiaomi_touch_fod_test(int value)
{
	struct input_dev *input_dev = goodix_core_data->input_dev;
	ts_info("fod_test enter, value = %d", value);
	if (value) {
		update_fod_press_status(1);
		input_mt_slot(input_dev, 0);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 1);
		input_report_key(input_dev, BTN_TOUCH, 1);
		input_report_key(input_dev, BTN_TOOL_FINGER, 1);
		input_report_abs(input_dev, ABS_MT_TRACKING_ID, 0);
		input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, 1);
		input_report_abs(input_dev, ABS_MT_POSITION_X, 60900);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, 243700);
		input_sync(input_dev);
	} else {
		input_mt_slot(input_dev, 0);
		input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, 0);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
		input_report_key(input_dev, BTN_TOUCH, 0);
		input_report_key(input_dev, BTN_TOOL_FINGER, 0);
		input_report_abs(input_dev, ABS_MT_TRACKING_ID, -1);
		update_fod_press_status(0);
		input_sync(input_dev);
	}
}
#endif


#ifdef GOODIX_XIAOMI_TOUCHFEATURE
static hardware_operation_t hardware_operation;
static hardware_param_t hardware_param;

static void goodix_sleep_to_gesture(struct goodix_ts_core *cd)
{
	int ret;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	ts_info("ic is in sleep already, need to reset");
	hw_ops->reset(cd, GOODIX_NORMAL_RESET_DELAY_MS);
	cd->work_status = TP_GESTURE;
	ret = hw_ops->gesture(cd, cd->gesture_enabled);
	if (ret)
		ts_err("failed enter gesture mode");
	else
		ts_info("enter gesture mode");

	hw_ops->irq_enable(cd, true);
	enable_irq_wake(cd->irq);
}

void goodix_ic_switch_mode(u8 _gesture_type)
{
	struct goodix_ts_core *core_data = goodix_core_data;
	struct goodix_ts_hw_ops *hw_ops = goodix_core_data->hw_ops;
	int gesture_type = 0;
	int ret;

#ifdef TOUCH_TRUSTED_SUPPORT
	if (goodix_core_data->tui_process) {
		if (wait_for_completion_interruptible(&goodix_core_data->tui_finish) ) {
			ts_err("cautious, ERESTARTSYS may cause cmd loss recomand try again");
			return;
		}
		ts_info("wait finished, its time to go ahead");
	}
#endif // TOUCH_TRUSTED_SUPPORT

	pm_stay_awake(core_data->bus->dev);
	if (core_data->tp_pm_suspend) {
		ts_info("device in suspend, wait to resume");
		ret = wait_for_completion_timeout(&core_data->pm_resume_completion, msecs_to_jiffies(150));
		if (!ret) {
			pm_relax(core_data->bus->dev);
			ts_err("system can't finished resuming procedure");
			return;
		}
	}
	ts_debug("fod is 0x%x", driver_get_touch_mode(TOUCH_ID, DATA_MODE_10));
	ts_debug("gesture enabled is 0x%x", core_data->gesture_enabled);
	mutex_lock(&core_data->core_mutex);
	if (_gesture_type & GESTURE_SINGLETAP_EVENT)
		gesture_type |= SINGLE_TAP_EN;
	if (_gesture_type & GESTURE_DOUBLETAP_EVENT)
		gesture_type |= DOUBLE_TAP_EN;
	if (_gesture_type & GESTURE_LONGPRESS_EVENT)
		gesture_type |= FOD_EN;


	if (core_data->gesture_enabled != gesture_type) {
		ts_info("gesture enable changed from 0x%x to 0x%x", core_data->gesture_enabled, gesture_type);
		core_data->gesture_enabled = gesture_type;

		if (0 == atomic_read(&core_data->suspended)) {
			ts_debug("tp is in resume state, wait suspend to send cmd!");
			goto out;
		}

		if (core_data->fod_finger) {
			ts_info("fod has already pressed!");
			goto out;
		}

		if (core_data->gesture_enabled && core_data->work_status == TP_SLEEP)
			goodix_sleep_to_gesture(core_data);
		else
			hw_ops->gesture(core_data, core_data->gesture_enabled);
	}
out:
	pm_relax(core_data->bus->dev);
	mutex_unlock(&core_data->core_mutex);
	return;
}

void goodix_game_mode_update(long mode_update_flag, int mode_value[DATA_MODE_35])
{
	struct goodix_ts_hw_ops *hw_ops = goodix_core_data->hw_ops;
	u8 data0 = 0;
	u8 data1 = 0;
	bool on = false;
	u8 temp_value = 0;
	static u8 last_temp_value = 0;
	int ret = 0;
	int i = 0;
	static bool expert_mode = false;

	ts_info("enter");

	if (goodix_core_data->work_status == TP_SLEEP) {
		last_temp_value = 0;
		ts_info("suspended, skip");
		return;
	}

	if (!mode_update_flag) {
		ts_info("no need update mode value");
		return;
	}

#ifdef TOUCH_THP_SUPPORT
	if (goodix_core_data->enable_touch_raw) {
		mutex_lock(&goodix_core_data->core_mutex);
		temp_value = driver_get_touch_mode(TOUCH_ID, DATA_MODE_0);
		ts_info("temp_value is %d, last_temp_value is %d", temp_value, last_temp_value);
		if (temp_value != last_temp_value) {
			ts_info("game mode status: %s   %d", temp_value ? "ON" : "OFF", temp_value);
			ret = goodix_htc_set_game_mode(temp_value);
			if (ret < 0) {
				ts_info("failed to send game mode: %d, ret=%d", temp_value, ret);
			}
		}
		last_temp_value = temp_value;
		mutex_unlock(&goodix_core_data->core_mutex);
		return;
	}
#endif
	mutex_lock(&goodix_core_data->core_mutex);
	if (!mode_update_flag) {
		ts_info("no need update mode value");
		mutex_unlock(&goodix_core_data->core_mutex);
		return;
	}

	expert_mode = mode_update_flag & (1 << DATA_MODE_6);

	for (i = 0; i <= DATA_MODE_8; i++) {
		switch (i) {
		case DATA_MODE_0:
			temp_value = mode_value[DATA_MODE_0];
			on = !!temp_value;

			break;
		case DATA_MODE_1:
			break;
		case DATA_MODE_2:
			temp_value = mode_value[DATA_MODE_2];
			data0 &= 0xF8;
			data0 |= temp_value;
			break;
		case DATA_MODE_3:
			temp_value = mode_value[DATA_MODE_3];
			data0 &= 0xC7;
			data0 |= (temp_value << 3);
			break;
		case DATA_MODE_8:
			temp_value = mode_value[DATA_MODE_8];
			if (PANEL_ORIENTATION_DEGREE_90 == temp_value)
				temp_value = 1;
			else if (PANEL_ORIENTATION_DEGREE_270 == temp_value)
				temp_value = 2;
			else
				temp_value = 0;
			data0 &= 0x3F;
			data0 |= (temp_value << 6);
			break;
		case DATA_MODE_4:
			temp_value = mode_value[DATA_MODE_4];
			data1 &= 0xC7;
			data1 |= (temp_value << 3);
			break;
		case DATA_MODE_5:
			temp_value = mode_value[DATA_MODE_5];
			data1 &= 0xF8;
			data1 |= temp_value;
			break;
		case DATA_MODE_7:
			temp_value = mode_value[DATA_MODE_7];
			data1 &= 0x3F;
			data1 |= (temp_value << 6);
			break;
		case DATA_MODE_6:
			temp_value = mode_value[DATA_MODE_6];
			temp_value = temp_value - 1;
			if (expert_mode) {
				data0 &= 0xF8;
				data0 |= (u8)goodix_core_data->board_data.touch_expert_array[temp_value * GAME_ARRAY_LEN + 1];
				data0 &= 0xC7;
				data0 |= (u8)(goodix_core_data->board_data.touch_expert_array[temp_value * GAME_ARRAY_LEN] << 3);
				data1 &= 0xC7;
				data1 |= (u8)(goodix_core_data->board_data.touch_expert_array[temp_value * GAME_ARRAY_LEN + 2] << 3);
				data1 &= 0xF8;
				data1 |= (u8)goodix_core_data->board_data.touch_expert_array[temp_value * GAME_ARRAY_LEN + 3];
			}
			break;
		default:
			ts_err("not support mode, mode(%d)", i);
			break;
		};
	}

	ret = hw_ops->game(goodix_core_data, data0, data1, !!on);

	if (ret < 0) {
		ts_err("send game mode fail");
	}
	mutex_unlock(&goodix_core_data->core_mutex);
	return;
}


static int goodix_update_cfg(struct goodix_ts_core *cd, bool enable) {
	int ret;
	char name_buf[GOODIX_MAX_STR_LABLE_LEN];
	char *dot;

	strlcpy(name_buf, cd->board_data.cfg_name, sizeof(name_buf));
	if (enable) { //WithSelf
		/* use base name */
	} else {
		dot = strrchr(name_buf, '.');
		if (dot) *dot = '\0';
		strlcat(name_buf, "_SelfDis.bin", sizeof(name_buf));
	}
	strlcpy(cd->board_data.cfg_bin_name, name_buf, GOODIX_MAX_STR_LABLE_LEN);
	ts_info("config name updated to: %s", cd->board_data.cfg_bin_name);

	ret = goodix_get_config_proc(cd);
	if (ret) {
		ts_err("no valid ic config found");
		return ret;
	} else {
		ts_debug("get valid ic config successfully");
	}

	ret = goodix_send_ic_config(cd, CONFIG_TYPE_NORMAL);
	if (ret) {
		ts_err("send ic config fail");
	} else {
		ts_debug("send ic config successfully");
	}

	return ret;
}

static void goodix_set_cur_value(int mode, int *value)
{
	int gtp_mode = mode;
	int gtp_value = value[0];
	ts_info("mode:%d, value:%d", gtp_mode, gtp_value);

#ifdef CONFIG_TOUCH_FACTORY_BUILD
	ts_debug("cmd_flag:%d", cmd_flag);
	if (!cmd_flag && (gtp_mode == DATA_MODE_51 || gtp_mode == DATA_MODE_122)) {
		ts_err("tp self test in progress, reject mode:%d", gtp_mode);
		return;
	}
#endif

	if (!goodix_core_data || goodix_core_data->init_stage != CORE_INIT_STAGE2) {
		ts_err("initialization not completed, return");
		return;
	}

#ifdef TOUCH_TRUSTED_SUPPORT
	if (goodix_core_data->tui_process) {
		if (wait_for_completion_interruptible(&goodix_core_data->tui_finish) ) {
			ts_err("cautious, ERESTARTSYS may cause cmd loss recomand try again");
			return;
		}
		ts_info("wait finished, its time to go ahead");
	}
#endif // TOUCH_TRUSTED_SUPPORT

	if (gtp_value < 0) {
		ts_err("invalid mode value");
		return;
	}
	/*
	if (atomic_read(&goodix_core_data->suspended)) {
		ts_info("tp is suspend, skip set_cur_value :mode:%d, value:%d", gtp_mode, gtp_value);
		return;
	}
	*/
	ts_info("mode:%d, value:%d", gtp_mode, gtp_value);

	switch(gtp_mode) {
		case DATA_MODE_36:
			goodix_core_data->need_update_cfg = 1;
			goodix_core_data->cfg_cloud_state = !!gtp_value;
			break;
		case DATA_MODE_41:
			ts_info("touchfeature already notify hal to boost, skip");
			break;
		case DATA_MODE_42:
			goodix_htc_enable_empty_int(!!gtp_value);
			break;
		case DATA_MODE_43:
			brl_switch_report_rate(goodix_core_data, !!gtp_value);
			break;
		case DATA_MODE_44:
		//not to do, to thp
			break;
		case DATA_MODE_47:
		//not to do, to thp
			break;
#ifdef TOUCH_THP_SUPPORT
		case DATA_MODE_51:
			ts_info("THP enable doze mode [%d]", gtp_value);
			goodix_htc_enter_idle(value);
			break;
#endif
#ifdef TOUCH_FOD_SUPPORT
		case DATA_MODE_52:
			if (goodix_core_data->enable_touch_raw)
				goodix_set_fod_downup(goodix_core_data, gtp_value);
			break;
#endif
#ifdef TOUCH_THP_SUPPORT
#ifdef TOUCH_FOD_SUPPORT
		case DATA_MODE_55:
			ts_info("hal init ready.");
			schedule_delayed_work(&goodix_core_data->thp_signal_work, 1 * HZ);
			break;
#endif
#endif
		case DATA_MODE_62:
			ts_info("need set report rate: %d", gtp_value);
			goodix_core_data->report_rate = gtp_value;
			goodix_htc_set_active_scan_rate(gtp_value);
			break;
#ifdef TOUCH_THP_SUPPORT
		case DATA_MODE_119:
			ts_info("B array is not reasonable, need scan freq...");
			goodix_htc_start_calibration();
			break;
		case DATA_MODE_121:
			ts_info("THP request B array");
			goodix_htc_enable_b_array();
			break;
#endif
		case DATA_MODE_122:
			ts_debug("THP update idle baseline");
			goodix_htc_update_idle_baseline();
			break;
		case DATA_MODE_127:
		#ifdef TOUCH_DUMP_TIC_SUPPORT
			if (gtp_value == DUMP_OFF || gtp_value == DUMP_ON || gtp_value == DUMP_BASE) {
				ts_debug("change dump state(%d) as %d",
					goodix_core_data->dump_type, gtp_value);
				goodix_core_data->dump_type = gtp_value;
			}
		#endif //TOUCH_DUMP_TIC_SUPPORT
			break;
		case DATA_MODE_135:
			goodix_htc_set_idle_threshold(gtp_value);
			break;
		case DATA_MODE_149:
			goodix_htc_set_freq_hopping(gtp_value);
			break;
		case DATA_MODE_151:
			goodix_htc_set_soft_reset(gtp_value);
			break;
		case DATA_MODE_152:
			goodix_htc_set_double_scan(gtp_value);
			break;
		case DATA_MODE_153:
			goodix_htc_set_normalize_study(value);
			break;
		case DATA_MODE_154:
			goodix_htc_set_gesture_feedback(gtp_value);
			break;
		default:
			ts_err("not support mode, mode(%d)!", gtp_mode);
			break;
	}
}

static u8 goodix_panel_color_read(void)
{
	if (!goodix_core_data)
		return 0;

	return goodix_core_data->lockdown_info[2];
}

static u8 goodix_panel_vendor_read(void)
{
	if (!goodix_core_data)
		return 0;

	return goodix_core_data->lockdown_info[0];
}

static u8 goodix_panel_display_read(void)
{
	if (!goodix_core_data)
		return 0;

	return goodix_core_data->lockdown_info[1];
}

static char goodix_touch_vendor_read(void)
{
	return '2';
}

static void goodix_set_charge_state(int state)
{
	if (!goodix_core_data || goodix_core_data->init_stage < CORE_INIT_STAGE2) {
		ts_err("%s not inited", __func__);
		return;
	} else {
		ts_info("%s state:%d", __func__, state);
	}
	goodix_core_data->charger_status = state;
	if (atomic_read(&goodix_core_data->suspended))
		return;
	if (state)
		goodix_core_data->hw_ops->charger_on(goodix_core_data, true);
	else
		goodix_core_data->hw_ops->charger_on(goodix_core_data, false);
}

int goodix_set_thermal_temp(int temp, bool force)
{
	int ret = 0;
	int temp0 = 0;
	int restemp[1] = {0};

	restemp[0] = temp;
	if (force) {
		temp0 = get_bms_temp_common();
		if (abs(temp0) >= INVAILD_TEMPERATURE)
			return -1;
		restemp[0] = (temp0 + 5) / 10; // Rounding, in degrees Celsius
		add_common_data_to_buf(0, SET_CUR_VALUE, DATA_MODE_145, 1, restemp);
	}

	ts_info("temp: %d", temp);

	return ret;
}
#endif

#ifdef GOODIX_DEBUGFS_ENABLE
static void tpdbg_suspend(struct goodix_ts_core *core_data, bool enable)
{
	schedule_resume_suspend_work(TOUCH_ID, !enable);
}

static int tpdbg_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return 0;
}

static ssize_t tpdbg_read(struct file *file, char __user *buf, size_t size,
		loff_t *ppos)
{
	const char *str = "cmd support as below:\n \
			\necho \"irq-disable\" or \"irq-enable\" to ctrl irq\n \
			\necho \"tp-suspend-en\" or \"tp-suspend-off\" to ctrl panel in or off suspend status\n \
			\necho \"tp-sd-en\" or \"tp-sd-off\" to ctrl panel in or off sleep status\n";

	loff_t pos = *ppos;
	int len = strlen(str);

	if (pos < 0)
		return -EINVAL;
	if (pos >= len)
		return 0;

	if (copy_to_user(buf, str, len))
		return -EFAULT;

	*ppos = pos + len;

	return len;
}

static ssize_t tpdbg_write(struct file *file, const char __user *buf,
		size_t size, loff_t *ppos)
{
	struct goodix_ts_core *core_data = file->private_data;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	char *cmd = kzalloc(size + 1, GFP_KERNEL);
	int ret = size;

	if (!cmd)
		return -ENOMEM;

	if (core_data->init_stage < CORE_INIT_STAGE2) {
		ts_err("initialization not completed");
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(cmd, buf, size)) {
		ret = -EFAULT;
		goto out;
	}

	cmd[size] = '\0';

	if (!strncmp(cmd, "irq-disable", 11))
		hw_ops->irq_enable(core_data, false);
	else if (!strncmp(cmd, "irq-enable", 10))
		hw_ops->irq_enable(core_data, true);
	else if (!strncmp(cmd, "tp-sd-en", 8))
		tpdbg_suspend(core_data, true);
	else if (!strncmp(cmd, "tp-sd-off", 9))
		tpdbg_suspend(core_data, false);
	else if (!strncmp(cmd, "tp-suspend-en", 13))
		tpdbg_suspend(core_data, true);
	else if (!strncmp(cmd, "tp-suspend-off", 14))
		tpdbg_suspend(core_data, false);
out:
	kfree(cmd);

	return ret;
}

static int tpdbg_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;

	return 0;
}

static const struct file_operations tpdbg_operations = {
	.owner = THIS_MODULE,
	.open = tpdbg_open,
	.read = tpdbg_read,
	.write = tpdbg_write,
	.release = tpdbg_release,
};
#endif

/**
 * goodix_start_later_init - init IC fw and config
 * @data: point to goodix_ts_core
 *
 * This function respond for get fw version and try upgrade fw and config.
 * Note: when init encounter error, need release all resource allocated here.
 */
static int goodix_start_later_init(struct goodix_ts_core *ts_core)
{
	int ret, i;
	int update_flag = UPDATE_MODE_BLOCK | UPDATE_MODE_SRC_REQUEST;
	struct goodix_ts_board_data *ts_bdata = board_data(ts_core);
	struct goodix_ts_core *cd = ts_core;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	/* step 1: read version */
	// return 0;
	ret = cd->hw_ops->read_version(cd, &cd->fw_version);
	if (ret < 0) {
		ts_err("failed to get version info, try to upgrade");
		update_flag |= UPDATE_MODE_FORCE;
		goto upgrade;
	}
	ts_err("get version info!!!!!!");
	goodix_match_panel(cd);

	/* setp 2: get config data from config bin */
	ret = goodix_get_config_proc(cd);
	if (ret)
		ts_info("no valid ic config found");
	else
		ts_info("success get valid ic config");

upgrade:
	/* setp 3: init fw struct add try do fw upgrade */
	ret = goodix_fw_update_init(cd);
	if (ret) {
		ts_err("failed init fw update module");
		goto err_out;
	}

	ts_info("update flag: 0x%X", update_flag);
	ret = goodix_do_fw_update(cd->ic_configs[CONFIG_TYPE_NORMAL],update_flag);
	if (ret) {
		ts_err("[DIS-TF-TOUCH] failed do fw update");
	}
	/* setp3: get fw version and ic_info
	 * at this step we believe that the ic is in normal mode,
	 * if the version info is invalid there must have some
	 * problem we cann't cover so exit init directly.
	 */
	ret = hw_ops->read_version(cd, &cd->fw_version);
	if (ret) {
		ts_err("invalid fw version, abort");
		goto uninit_fw;
	}
	ret = hw_ops->get_ic_info(cd, &cd->ic_info);
	if (ret) {
		ts_err("[DIS-TF-TOUCH] invalid ic info, abort");
		goto uninit_fw;
	}

	/* the recomend way to update ic config is throuth ISP,
	 * if not we will send config with interactive mode
	 */
	goodix_send_ic_config(cd, CONFIG_TYPE_NORMAL);
    /*
#ifdef TOUCH_THP_SUPPORT
	if (cd->enable_touch_raw && cd->ic_configs[CONFIG_TYPE_NORMAL]
		&& cd->ic_configs[CONFIG_TYPE_NORMAL]->len) {
		// must have a config for update K coeffi
		if (goodix_normalize_coeffi_update(cd)) {
			ts_err("failed update normalize coeffi!");
			goto uninit_fw;
		}
	} else {
		ts_info("no config data, skip update normalize coeffi");
	}
#endif
    */ //n11u driver no longer need to send K-matrix to fw

	/* init other resources */
	ret = goodix_ts_stage2_init(cd);
	if (ret) {
		ts_err("stage2 init failed");
		goto uninit_fw;
	}

#ifdef GOODIX_DEBUGFS_ENABLE
	cd->debugfs = debugfs_create_dir("tp_debug", NULL);
	if (cd->debugfs) {
		debugfs_create_file("switch_state", 0660, cd->debugfs, cd,
					&tpdbg_operations);
	}
#endif

	if (cd->goodix_tp_class == NULL) {
#ifdef GOODIX_XIAOMI_TOUCHFEATURE
		cd->goodix_tp_class = get_xiaomi_touch_class();
#else
		cd->goodix_tp_class = class_create(THIS_MODULE, "touch");
#endif
	}

#ifdef GOODIX_XIAOMI_TOUCHFEATURE

	goodix_core_data->sync_mode = DIFF_AUTO_SYNC;
	hardware_param.x_resolution = ts_bdata->panel_max_x / ts_bdata->super_resolution_factor;
	hardware_param.y_resolution = ts_bdata->panel_max_y / ts_bdata->super_resolution_factor;
	hardware_param.rx_num = (u16) goodix_get_rx_num();
	hardware_param.tx_num = (u16) goodix_get_tx_num();
	hardware_param.super_resolution_factor = ts_bdata->super_resolution_factor;
#ifdef TOUCH_DUMP_TIC_SUPPORT
	hardware_param.frame_data_page_size = 1;
#else
	hardware_param.frame_data_page_size = 1;
#endif //TOUCH_DUMP_TIC_SUPPORT
	hardware_param.frame_data_buf_size = 5;
	hardware_param.raw_data_page_size = 0;
	hardware_param.raw_data_buf_size = 0;
	memset(hardware_param.config_file_name, 0, 64);
	strlcpy(hardware_param.config_file_name, cd->board_data.cfg_bin_name, sizeof(hardware_param.config_file_name));
	memset(hardware_param.driver_version, 0, 64);
	memcpy(hardware_param.driver_version, GOODIX_DRIVER_VERSION, strlen(GOODIX_DRIVER_VERSION));
	goodix_lockdown_info_read(hardware_param.lockdown_info);
	goodix_fw_version_info_read(hardware_param.fw_version);

	memset(&hardware_operation, 0, sizeof(hardware_operation_t));
	hardware_operation.ic_self_test = goodix_ic_self_test;
	hardware_operation.ic_data_collect = goodix_ic_data_collect;
	hardware_operation.ic_get_lockdown_info = goodix_lockdown_info_read;
	hardware_operation.ic_get_fw_version = goodix_fw_version_info_read;
	hardware_operation.set_mode_value = goodix_set_cur_value;
	hardware_operation.ic_switch_mode = goodix_ic_switch_mode;
	hardware_operation.cmd_update_func = goodix_game_mode_update;
	hardware_operation.set_mode_long_value = NULL;
	hardware_operation.ic_resume_suspend = goodix_resume_suspend;
	hardware_operation.palm_sensor_write = NULL;
#ifdef TOUCH_THP_SUPPORT
	ts_info("enable thp");
	hardware_operation.enable_touch_raw = goodix_htc_enable;
	goodix_core_data->enable_touch_raw = 0;
	goodix_htc_enable(0);
#ifdef TOUCH_DUMP_TIC_SUPPORT
	goodix_core_data->dump_type = DUMP_OFF;
#endif //TOUCH_DUMP_TIC_SUPPORT
#endif
	hardware_operation.panel_vendor_read = goodix_panel_vendor_read;
	hardware_operation.panel_color_read = goodix_panel_color_read;
	hardware_operation.panel_display_read = goodix_panel_display_read;
	hardware_operation.touch_vendor_read = goodix_touch_vendor_read;
	hardware_operation.get_touch_ic_buffer = goodix_cmd_fifo_get;
	hardware_operation.touch_doze_analysis = goodix_touch_doze_analysis;
	hardware_operation.htc_ic_setModeValue = goodix_htc_ic_setModeValue;
	hardware_operation.htc_ic_getModeValue = goodix_htc_ic_getModeValue;
	hardware_operation.ic_set_charge_state = goodix_set_charge_state;
	hardware_operation.set_thermal_temp = goodix_set_thermal_temp;
	hardware_param.temp_change_value = 2;
#ifdef TOUCH_FOD_SUPPORT
	hardware_operation.xiaomi_touch_fod_test = goodix_xiaomi_touch_fod_test;
#endif

	register_touch_panel(cd->bus->dev, TOUCH_ID, &hardware_param, &hardware_operation);
	xiaomi_register_panel_notifier(cd->bus->dev, TOUCH_ID,
		PANEL_EVENT_NOTIFICATION_PRIMARY, PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_TOUCH);
#endif

#ifdef TOUCH_TRUSTED_SUPPORT
	if (cd->qts_en) {
		ts_info("enable QTS");
		goodix_fill_qts_vendor_data(&qts_vendor_data, cd);
		ret = qts_client_register(qts_vendor_data);
		if (ret)
			ts_err("qts client register failed, rc %d\n", ret);
	}
	init_completion(&cd->tui_finish);
	cd->tui_process = false;
#endif // TOUCH_TRUSTED_SUPPORT

	cd->init_stage = CORE_INIT_STAGE2;

	return 0;

uninit_fw:
	goodix_fw_update_uninit();
err_out:
	ts_err("stage2 init failed");
	cd->init_stage = CORE_INIT_FAIL;
	for (i = 0; i < GOODIX_MAX_CONFIG_GROUP; i++) {
		if (cd->ic_configs[i])
			kfree(cd->ic_configs[i]);
		cd->ic_configs[i] = NULL;
	}
	return ret;
}

/**
 * goodix_ts_probe - called by kernel when Goodix touch
 *  platform driver is added.
 */
static int goodix_ts_probe(struct platform_device *pdev)
{
	struct goodix_ts_core *core_data = NULL;
	struct goodix_bus_interface *bus_interface;
	int ret;

	ts_info("goodix_ts_probe IN");
	bus_interface = pdev->dev.platform_data;
	if (!bus_interface) {
		ts_err("Invalid touch device");
		core_module_prob_sate = CORE_MODULE_PROB_FAILED;
		return -ENODEV;
	}

	core_data = devm_kzalloc(&pdev->dev,
			sizeof(struct goodix_ts_core), GFP_KERNEL);
	if (!core_data) {
		ts_err("Failed to allocate memory for core data");
		core_module_prob_sate = CORE_MODULE_PROB_FAILED;
		return -ENOMEM;
	}
	goodix_core_data = core_data;
	if (IS_ENABLED(CONFIG_OF) && bus_interface->dev->of_node) {
		/* parse devicetree property */
		ret = goodix_parse_dt(bus_interface->dev->of_node,
				&core_data->board_data);
		if (ret) {
			ts_err("[DIS-TF-TOUCH] failed parse device info form dts, %d", ret);
			return -EINVAL;
		}
	} else {
		ts_err("no valid device tree node found");
		return -ENODEV;
	}

	core_data->hw_ops = goodix_get_hw_ops();
	if (!core_data->hw_ops) {
		ts_err("hw ops is NULL");
		core_module_prob_sate = CORE_MODULE_PROB_FAILED;
		return -EINVAL;
	}
	mutex_init(&core_data->core_mutex);
	goodix_core_module_init();
	/* touch core layer is a platform driver */
	core_data->pdev = pdev;
	core_data->bus = bus_interface;
	platform_set_drvdata(pdev, core_data);

	/* get GPIO resource */
	ret = goodix_ts_gpio_setup(core_data);
	if (ret) {
		ts_err("[DIS-TF-TOUCH] failed init gpio");
		goto err_out;
	}

	ret = goodix_ts_power_init(core_data);
	if (ret) {
		ts_err("[DIS-TF-TOUCH] failed init power");
		goto err_out;
	}
	/*set pinctrl */
	ret = goodix_ts_pinctrl_init(core_data);
	if (!ret && core_data->pinctrl) {
		ret = pinctrl_select_state(core_data->pinctrl,
					core_data->pin_sta_active);
		if (ret < 0)
			ts_err("Failed to select active pinstate, r:%d", ret);
	}

	ret = goodix_ts_power_on(core_data);
	if (ret) {
		ts_err("[DIS-TF-TOUCH] failed power on");
		goto err_out;
	}

	/* confirm it's goodix touch dev or not */
	ret = core_data->hw_ops->dev_confirm(core_data);
	if (ret) {
		ts_err("[DIS-TF-TOUCH] goodix device confirm failed");
		/*goto err_out;*/
	}

	/* generic notifier callback */
	core_data->ts_notifier.notifier_call = goodix_generic_noti_callback;
	goodix_ts_register_notifier(&core_data->ts_notifier);

	device_init_wakeup(core_data->bus->dev, 1);

	/* debug node init */
	goodix_tools_init();
	core_data->tp_pm_suspend = false;

	init_completion(&core_data->pm_resume_completion);
	device_init_wakeup(&pdev->dev, 1);
	core_data->init_stage = CORE_INIT_STAGE1;
	core_data->report_rate = 240;
	goodix_modules.core_data = core_data;
	core_module_prob_sate = CORE_MODULE_PROB_SUCCESS;

	ts_info("goodix_ts_core probe success");

	/* Try start a thread to get config-bin info */
	ret = goodix_start_later_init(core_data);
	if (ret) {
		ts_err("[DIS-TF-TOUCH] Failed start cfg_bin_proc, %d", ret);
		goto err_out;
	}

	return 0;

err_out:
	core_data->init_stage = CORE_INIT_FAIL;
	core_module_prob_sate = CORE_MODULE_PROB_FAILED;
	if (core_data->pinctrl) {
			pinctrl_select_state(core_data->pinctrl,
					core_data->pin_sta_suspend);
			devm_pinctrl_put(core_data->pinctrl);
		}
	core_data->pinctrl = NULL;
	ts_err("goodix_ts_core failed, ret:%d", ret);
	return ret;
}

static int goodix_ts_remove(struct platform_device *pdev)
{
	struct goodix_ts_core *core_data = platform_get_drvdata(pdev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;

	goodix_ts_unregister_notifier(&core_data->ts_notifier);
	goodix_tools_exit();

	if (core_data->init_stage >= CORE_INIT_STAGE2) {
		gesture_module_exit();
		inspect_module_exit();
		hw_ops->irq_enable(core_data, false);
	#ifdef CONFIG_FB
		fb_unregister_client(&core_data->fb_notifier);
	#endif
		core_module_prob_sate = CORE_MODULE_REMOVED;
		if (atomic_read(&core_data->ts_esd.esd_on))
			goodix_ts_esd_off(core_data);
		goodix_ts_unregister_notifier(&ts_esd->esd_notifier);

		goodix_fw_update_uninit();
		if (core_data->input_dev) {
			input_unregister_device(core_data->input_dev);
			input_free_device(core_data->input_dev);
			core_data->input_dev = NULL;
		}
		goodix_ts_sysfs_exit(core_data);
		goodix_ts_procfs_exit(core_data);
		goodix_ts_power_off(core_data);
	}

	return 0;
}

#ifdef CONFIG_PM
static const struct dev_pm_ops dev_pm_ops = {
	.suspend = goodix_ts_pm_suspend,
	.resume = goodix_ts_pm_resume,
};
#endif

static const struct platform_device_id ts_core_ids[] = {
	{.name = GOODIX_CORE_DRIVER_NAME},
	{}
};
MODULE_DEVICE_TABLE(platform, ts_core_ids);

static struct platform_driver goodix_ts_driver = {
	.driver = {
		.name = GOODIX_CORE_DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &dev_pm_ops,
#endif
	},
	.probe = goodix_ts_probe,
	.remove = goodix_ts_remove,
	.id_table = ts_core_ids,
};


static int __init goodix_ts_core_init(void)
{
	int ret;

	ts_info("Core layer init:%s", GOODIX_DRIVER_VERSION);
	#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_SPI
	ret = goodix_spi_bus_init();
#else
	ret = goodix_i2c_bus_init();
#endif
	if (ret) {
		ts_err("failed add bus driver");
		return ret;
	}
	return platform_driver_register(&goodix_ts_driver);
}

static void __exit goodix_ts_core_exit(void)
{
	ts_info("Core layer exit");
	platform_driver_unregister(&goodix_ts_driver);
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_SPI
	goodix_spi_bus_exit();
#else
	goodix_i2c_bus_exit();
#endif
}

module_init(goodix_ts_core_init);
module_exit(goodix_ts_core_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Core Module");
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
MODULE_AUTHOR("Goodix, Inc.");
MODULE_LICENSE("GPL v2");

