/*
 * Copyright (c) 2023 Xiaomi, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Xiaomi, Inc.
 */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/version.h>
#include <linux/uidgid.h>

#include "xiaomi_touch.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0))
/*
* Commit 359745d78351 ("proc: remove PDE_DATA() completely")
* Replaced PDE_DATA() with pde_data()
*/
#define pde_data(inode) PDE_DATA(inode)
#endif

#define PROC_COUNT_FOR_PANEL	(4)
#define DUMP_DATA_BUF_SIZE		(PAGE_SIZE * 3)
#define NORMAL_DATA_BUF_SIZE	(256)

typedef struct xiaomi_touch_proc_data {
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param;
	long tp_proc_result_length;
	u8 *tp_proc_result_buf;
} xiaomi_touch_proc_data_t;

static struct proc_dir_entry *tp_pde[MAX_TOUCH_PANEL_COUNT][PROC_COUNT_FOR_PANEL];
static int self_test_result[MAX_TOUCH_PANEL_COUNT];

static int proc_open(struct inode *inode, struct file *file)
{
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = (xiaomi_touch_driver_param_t *)pde_data(inode);
	char *name = file->f_path.dentry->d_iname;
	s8 touch_id = -1;
	xiaomi_touch_proc_data_t *xiaomi_touch_proc_data = NULL;

	if (!xiaomi_touch_driver_param)
		return -EFAULT;

	touch_id = xiaomi_touch_driver_param->touch_id;
	if (IS_TOUCH_ID_INVALID(touch_id))
		return -EFAULT;

	xiaomi_touch_proc_data = kvzalloc_retry(sizeof(xiaomi_touch_proc_data_t), 3);
	if (!xiaomi_touch_proc_data) {
		LOG_ERROR("alloc tp proc data memory error");
		return -EFAULT;
	}

	/* change private data */
	xiaomi_touch_proc_data->xiaomi_touch_driver_param = xiaomi_touch_driver_param;
	xiaomi_touch_proc_data->tp_proc_result_length = 0;
	file->private_data = xiaomi_touch_proc_data;
	
	if (!strncmp("tp_data_dump", name, 12)) {
		LOG_DEBUG("alloc dump data memory");
		xiaomi_touch_proc_data->tp_proc_result_buf = kvzalloc_retry(DUMP_DATA_BUF_SIZE, 3);
		if (!xiaomi_touch_proc_data->tp_proc_result_buf) {
			LOG_ERROR("alloc tp proc dump memory error");
			return -1;
		}
	} else {
		LOG_DEBUG("alloc proc data memory");
		xiaomi_touch_proc_data->tp_proc_result_buf = kvzalloc_retry(NORMAL_DATA_BUF_SIZE, 3);
		if (!xiaomi_touch_proc_data->tp_proc_result_buf) {
			LOG_ERROR("alloc tp proc data memory error");
			return -1;
		}
	}

	LOG_DEBUG("open %s", name);
	return 0;
}

static int proc_release(struct inode *inode, struct file *file)
{
	xiaomi_touch_proc_data_t *xiaomi_touch_proc_data = (xiaomi_touch_proc_data_t *)file->private_data;
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = xiaomi_touch_proc_data->xiaomi_touch_driver_param;
	char *name = file->f_path.dentry->d_iname;
	s8 touch_id = -1;

	if (!xiaomi_touch_driver_param)
		return -EFAULT;

	touch_id = xiaomi_touch_driver_param->touch_id;
	if (IS_TOUCH_ID_INVALID(touch_id))
		return -EFAULT;

	if (xiaomi_touch_proc_data->tp_proc_result_buf) {
		LOG_DEBUG("free proc data buf memory");
		kvzalloc_free(xiaomi_touch_proc_data->tp_proc_result_buf);
		xiaomi_touch_proc_data->tp_proc_result_buf = NULL;
	}

	if (xiaomi_touch_proc_data) {
		LOG_DEBUG("free proc data buf memory");
		kvzalloc_free(xiaomi_touch_proc_data->tp_proc_result_buf);
		xiaomi_touch_proc_data = NULL;
	}

	LOG_DEBUG("release %s", name);
	return 0;
}

static ssize_t proc_tp_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	xiaomi_touch_proc_data_t *xiaomi_touch_proc_data = (xiaomi_touch_proc_data_t *)file->private_data;
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = xiaomi_touch_proc_data->xiaomi_touch_driver_param;
	char *name = file->f_path.dentry->d_iname;
	int valid_length;
	s8 touch_id = -1;
	int ret = 0;
	int n = 0;

	if (!xiaomi_touch_driver_param)
		return -EFAULT;

	touch_id = xiaomi_touch_driver_param->touch_id;
	if (IS_TOUCH_ID_INVALID(touch_id))
		return -EFAULT;

	LOG_DEBUG("*pos is %lld", *pos);
	if (*pos && *pos >= xiaomi_touch_proc_data->tp_proc_result_length) {
		return 0;
	}

	if (*pos) {
		goto copy_data_to_user;
	}

	LOG_DEBUG("open proc tp node name is %s", name);
	if (!strncmp("tp_fw_version", name, 10)) {
		LOG_DEBUG("read tp_fw_version");
		if (xiaomi_touch_driver_param->hardware_operation.ic_get_fw_version)
			xiaomi_touch_driver_param->hardware_operation.ic_get_fw_version(xiaomi_touch_driver_param->hardware_param.fw_version);
		n += snprintf(xiaomi_touch_proc_data->tp_proc_result_buf + n, NORMAL_DATA_BUF_SIZE - n, "fw version: %s\n", xiaomi_touch_driver_param->hardware_param.fw_version);
		n += snprintf(xiaomi_touch_proc_data->tp_proc_result_buf + n, NORMAL_DATA_BUF_SIZE - n, "driver version: %s\n", xiaomi_touch_driver_param->hardware_param.driver_version);
		n += snprintf(xiaomi_touch_proc_data->tp_proc_result_buf + n, NORMAL_DATA_BUF_SIZE - n, "hal version: %s\n", xiaomi_touch_driver_param->hal_version);
		n += snprintf(xiaomi_touch_proc_data->tp_proc_result_buf + n, NORMAL_DATA_BUF_SIZE - n, "xiaomi-touch version: %s\n", XIAOMI_TOUCH_VERSION);
		xiaomi_touch_proc_data->tp_proc_result_length = n;
	} else if (!strncmp("tp_lockdown_info", name, 16)) {
		u8 *lockdown_info = xiaomi_touch_driver_param->hardware_param.lockdown_info;
		LOG_DEBUG("read tp_lockdown_info");
		if (xiaomi_touch_driver_param->hardware_operation.ic_get_lockdown_info) {
			ret = xiaomi_touch_driver_param->hardware_operation.ic_get_lockdown_info(lockdown_info);
			if (ret < 0)
				LOG_ERROR("read tp_lockdown_info error");
			n = snprintf(xiaomi_touch_proc_data->tp_proc_result_buf + n, NORMAL_DATA_BUF_SIZE - n,
					"0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X\n",
					lockdown_info[0], lockdown_info[1], lockdown_info[2], lockdown_info[3],
					lockdown_info[4], lockdown_info[5], lockdown_info[6], lockdown_info[7]);
		} else {
			n = snprintf(xiaomi_touch_proc_data->tp_proc_result_buf + n, NORMAL_DATA_BUF_SIZE - n, "null");
		}
		xiaomi_touch_proc_data->tp_proc_result_length = n;
	} else if (!strncmp("tp_selftest", name, 11)) {
		LOG_DEBUG("read tp_selftest result %d", self_test_result[touch_id]);
		n = snprintf(xiaomi_touch_proc_data->tp_proc_result_buf + n, NORMAL_DATA_BUF_SIZE - n, "%d\n", self_test_result[touch_id]);
		xiaomi_touch_proc_data->tp_proc_result_length = n;
	} else if (!strncmp("tp_data_dump", name, 12)) {
		LOG_DEBUG("read tp_data_dump");
		if (xiaomi_touch_driver_param->hardware_operation.ic_data_collect)
			xiaomi_touch_driver_param->hardware_operation.ic_data_collect(xiaomi_touch_proc_data->tp_proc_result_buf, &n);
		xiaomi_touch_proc_data->tp_proc_result_length = n;
	}

copy_data_to_user:

	valid_length = xiaomi_touch_proc_data->tp_proc_result_length - *pos;
	valid_length = valid_length < count ? valid_length : count;
	LOG_DEBUG("result length %ld, valid length %d, count %zu", xiaomi_touch_proc_data->tp_proc_result_length, valid_length, count);
	if (valid_length && (ret = copy_to_user(buf, xiaomi_touch_proc_data->tp_proc_result_buf + *pos, valid_length))) {
		LOG_ERROR("copy result to user failed, result is %d", ret);
		return -EFAULT;
	}
	*pos += valid_length;
	return valid_length;
}

static ssize_t proc_tp_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	xiaomi_touch_proc_data_t *xiaomi_touch_proc_data = (xiaomi_touch_proc_data_t *)file->private_data;
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = xiaomi_touch_proc_data->xiaomi_touch_driver_param;
	char *name = file->f_path.dentry->d_iname;
	s8 touch_id = -1;

	if (!xiaomi_touch_driver_param)
		return -EFAULT;
	touch_id = xiaomi_touch_driver_param->touch_id;
	if (IS_TOUCH_ID_INVALID(touch_id))
		return -EFAULT;
	LOG_DEBUG("open proc tp node name is %s", name);

	if (!strncmp("tp_fw_version", name, 10)) {
		size_t temp_count = count;
		memset(xiaomi_touch_driver_param->hal_version, 0x00, HAL_VERSION_LENGTH);
		if (temp_count >= HAL_VERSION_LENGTH) {
			LOG_ERROR("write hal version length too long, current length is %zu", temp_count);
			temp_count = HAL_VERSION_LENGTH - 1;
		}
		if (copy_from_user(xiaomi_touch_driver_param->hal_version, buf, temp_count)) {
			LOG_ERROR("copy hal version to buf has error");
			return -EFAULT;
		}
		LOG_INFO("hal version is %s", xiaomi_touch_driver_param->hal_version);
		LOG_INFO("fw version is %s", xiaomi_touch_driver_param->hardware_param.fw_version);
		LOG_INFO("driver version is %s", xiaomi_touch_driver_param->hardware_param.driver_version);
		LOG_INFO("xiaomi-touch version is %s", XIAOMI_TOUCH_VERSION);
	} else if (!strncmp("tp_selftest", name, 11)) {
		static char self_test_cmd[64];
		size_t temp_count = count;
		memset(self_test_cmd, 0, 64);
		if (temp_count >= 64) {
			LOG_ERROR("write self test cmd length too long, current length is %zu", temp_count);
			temp_count = 64 - 1;
		}
		if (copy_from_user(self_test_cmd, buf, temp_count)) {
			LOG_ERROR("copy self test has error");
			return -EFAULT;
		}
		if (xiaomi_touch_driver_param->hardware_operation.ic_self_test)
			xiaomi_touch_driver_param->hardware_operation.ic_self_test(self_test_cmd, &self_test_result[touch_id]);
		LOG_ERROR("start self test cmd: %s", self_test_cmd);
	}

	return count;
}

static const struct proc_ops proc_tp_ops = {
	.proc_open = proc_open,
	.proc_release = proc_release,
	.proc_read = proc_tp_read,
	.proc_write = proc_tp_write,
	.proc_lseek = default_llseek,
};

static struct proc_dir_entry *create_proc_node(char *name, const struct proc_ops *proc_ops, xiaomi_touch_driver_param_t *xiaomi_touch_driver_param)
{
	struct proc_dir_entry *pde = proc_create_data(name, 0644, NULL, proc_ops, xiaomi_touch_driver_param);
	kuid_t uid;
	kgid_t gid;

	if (!pde) {
		LOG_ERROR("proc_create_data has error, exit!");
		return NULL;
	}

	uid = make_kuid(current_user_ns(), 1000);
	if (!uid_valid(uid))
		uid = KUIDT_INIT(1000);
	gid = make_kgid(current_user_ns(), 1000);
	if (!gid_valid(gid))
		gid = KGIDT_INIT(1000);
	proc_set_user(pde, uid, gid);

	return pde;
}

int xiaomi_touch_create_proc(xiaomi_touch_driver_param_t *xiaomi_touch_driver_param)
{
	s8 touch_id = -1;
	char proc_name[64];
	char name_suffix[10];

	if (!xiaomi_touch_driver_param )
		return -1;

	touch_id = xiaomi_touch_driver_param->touch_id;
	LOG_DEBUG("touch id is %d", touch_id);
	if (IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_DEBUG("invalid touch id %d", touch_id);
		return -1;
	}

	memset(name_suffix, 0, 10);
	if (touch_id > 0)
		snprintf(name_suffix, 10, "_%d", touch_id);

	/* create tp proc */
	memset(proc_name, 0, 64);
	snprintf(proc_name, 64, "tp_fw_version%s", name_suffix);
	tp_pde[touch_id][0] = create_proc_node(proc_name, &proc_tp_ops, xiaomi_touch_driver_param);

	memset(proc_name, 0, 64);
	snprintf(proc_name, 64, "tp_lockdown_info%s", name_suffix);
	tp_pde[touch_id][1] = create_proc_node(proc_name, &proc_tp_ops, xiaomi_touch_driver_param);

	memset(proc_name, 0, 64);
	snprintf(proc_name, 64, "tp_selftest%s", name_suffix);
	tp_pde[touch_id][2] = create_proc_node(proc_name, &proc_tp_ops, xiaomi_touch_driver_param);

	memset(proc_name, 0, 64);
	snprintf(proc_name, 64, "tp_data_dump%s", name_suffix);
	tp_pde[touch_id][3] = create_proc_node(proc_name, &proc_tp_ops, xiaomi_touch_driver_param);


	return 0;
}

int xiaomi_touch_remove_proc(s8 touch_id)
{
	int i = 0;

	if (IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_ERROR("touch id is invalid, return!");
		return -1;
	}

	for (i = 0; i < PROC_COUNT_FOR_PANEL; i++) {
		if (!tp_pde[touch_id][i])
			continue;
		LOG_DEBUG("remove proc node %p", tp_pde[touch_id][i]);
		proc_remove(tp_pde[touch_id][i]);
		tp_pde[touch_id][i] = NULL;
	}
	return 0;
}
