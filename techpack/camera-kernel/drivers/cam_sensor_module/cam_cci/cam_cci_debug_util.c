// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2022, The Linux Foundation. All rights reserved.
 */

#include "cam_cci_debug_util.h"

static int cam_cci_debug_str_show(struct seq_file *s, void *ignored)
{
	char *str = s->private;

	if (!str)
		seq_printf(s, "Not Assigned\n");
	else
		seq_printf(s, "%s\n", str);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(cam_cci_debug_str);

static int cam_cci_debug_get_val(void *data, u64 *val)
{
	if (!data)
		return -EINVAL;

	*val = *(uint32_t *)data;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(cam_cci_debug_val_readonly,
	cam_cci_debug_get_val, NULL, "%llu\n");

static int cam_cci_debug_set_val(void *data, u64 val)
{
	if (!data)
		return -EINVAL;

	*(uint32_t *)data = val;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(cam_cci_debug_val,
	cam_cci_debug_get_val, cam_cci_debug_set_val, "%llu\n");

static int cam_cci_debug_set_addr_type(void *data, u64 val)
{
	struct cam_cci_debug *cci_dbg;

	if (!data)
		return -EINVAL;

	cci_dbg = container_of(data, struct cam_cci_debug, reg_addr_type);

	if (val == CAMERA_SENSOR_I2C_TYPE_BYTE) {
		*(uint32_t *)data = val;
		if (cci_dbg->min_reg_addr > MAX_DEV_TYPE_BYTE) {
			cci_dbg->min_reg_addr = MAX_DEV_TYPE_BYTE;
		}
		if (cci_dbg->max_reg_addr > MAX_DEV_TYPE_BYTE) {
			cci_dbg->max_reg_addr = MAX_DEV_TYPE_BYTE;
		}
	}
	else if (val == CAMERA_SENSOR_I2C_TYPE_WORD) {
		*(uint32_t *)data = val;
		if (cci_dbg->min_reg_addr > MAX_DEV_TYPE_WORD) {
			cci_dbg->min_reg_addr = MAX_DEV_TYPE_WORD;
		}
		if (cci_dbg->max_reg_addr > MAX_DEV_TYPE_WORD) {
			cci_dbg->max_reg_addr = MAX_DEV_TYPE_WORD;
		}
	}
	else if (val == CAMERA_SENSOR_I2C_TYPE_3B ||
			 val == CAMERA_SENSOR_I2C_TYPE_DWORD) {
		CAM_ERR(MI_DEBUG, "Unsupported register addr type");
		return -EINVAL;
	}
	else {
		CAM_ERR(MI_DEBUG, "Invalid register addr type");
		return -EINVAL;
	}

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(cam_cci_debug_addr_type,
	cam_cci_debug_get_val, cam_cci_debug_set_addr_type, "%llu\n");

static int cam_cci_debug_set_data_type(void *data, u64 val)
{
	struct cam_cci_debug *cci_dbg;

	if (!data)
		return -EINVAL;

	cci_dbg = container_of(data, struct cam_cci_debug, reg_data_type);

	if (!cci_dbg)
		return -EINVAL;

	/* temporarily fix i2c data type to BYTE */
	if (val == CAMERA_SENSOR_I2C_TYPE_BYTE) {
		*(uint32_t *)data = val;
	}
	else if (val == CAMERA_SENSOR_I2C_TYPE_WORD ||
			 val == CAMERA_SENSOR_I2C_TYPE_3B ||
			 val == CAMERA_SENSOR_I2C_TYPE_DWORD) {
		CAM_ERR(MI_DEBUG, "Unsupported register addr type");
		return -EINVAL;
	}
	else {
		CAM_ERR(MI_DEBUG, "Invalid register addr type");
		return -EINVAL;
	}

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(cam_cci_debug_data_type,
	cam_cci_debug_get_val, cam_cci_debug_set_data_type, "%llu\n");

static ssize_t cam_cci_debug_range_read(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	char    *buf;
	ssize_t  rc;
	struct cam_cci_debug *cci_dbg =
		(struct cam_cci_debug *) file->private_data;

	if (!cci_dbg)
		return -EINVAL;

	buf = kzalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rc = snprintf(buf, count, "0x%.*x - 0x%.*x\n",
		cci_dbg->reg_addr_type * 2, cci_dbg->min_reg_addr,
		cci_dbg->reg_addr_type * 2, cci_dbg->max_reg_addr);

	rc = simple_read_from_buffer(user_buf, count, ppos, buf, rc);

	kfree(buf);
	return rc;
}

static void __check_data_range(unsigned long *data,
	enum camera_sensor_i2c_type type) {
	if (type == CAMERA_SENSOR_I2C_TYPE_WORD &&
		*data > MAX_DEV_TYPE_WORD) {
		CAM_WARN(MI_DEBUG, "Start address exceeds upper limit then modified to: %d",
			MAX_DEV_TYPE_BYTE);
		*data = MAX_DEV_TYPE_WORD;
	}
	else if (type == CAMERA_SENSOR_I2C_TYPE_BYTE &&
		*data > MAX_DEV_TYPE_BYTE) {
		CAM_WARN(MI_DEBUG, "Start address exceeds upper limit then modified to: %d",
			MAX_DEV_TYPE_BYTE);
		*data = MAX_DEV_TYPE_BYTE;
	}
}

static ssize_t cam_cci_debug_range_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	char    buf[DEBUGFS_STR_MAX_SIZE];
	char   *start = buf;
	size_t  buf_size;
	unsigned long reg_start, reg_end;
	struct cam_cci_debug *cci_dbg =
		(struct cam_cci_debug *) file->private_data;

	if (!cci_dbg)
		return -EINVAL;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	while (*start == ' ')
		start++;
	reg_start = simple_strtoul(start, &start, 16);

	__check_data_range(&reg_start, cci_dbg->reg_addr_type);

	cci_dbg->min_reg_addr = reg_start;

	while (*start == ' ')
		start++;
	if (kstrtoul(start, 16, &reg_end)) {
		CAM_INFO(MI_DEBUG, "Set to single register read: %d", reg_start);
		cci_dbg->max_reg_addr = reg_start;
		return buf_size;
	}

	if (reg_end < reg_start) {
		CAM_WARN(MI_DEBUG, "End address is lower than start address then modified to: %d",
			reg_start);
		reg_end = reg_start;
	}

	__check_data_range(&reg_end, cci_dbg->reg_addr_type);

	cci_dbg->max_reg_addr = reg_end;
	CAM_DBG(MI_DEBUG, "Register read range set to: %d - %d", reg_start, reg_end);

	return buf_size;
}

static const struct file_operations cam_cci_debug_range = {
	.open = simple_open,
	.read = cam_cci_debug_range_read,
	.write = cam_cci_debug_range_write,
	.llseek = default_llseek,
};

static int __cam_cci_debug_cci_read_seq(struct camera_io_master *io_master_info,
	uint8_t *data, uint32_t start, uint32_t end, enum camera_sensor_i2c_type addr_type,
	enum camera_sensor_i2c_type data_type)
{
	int rc = 0;

	CAM_DBG(MI_DEBUG, "Try cci sequential read in cci debugfs");

	rc = camera_io_init(io_master_info);
	if (rc < 0) {
		CAM_ERR(MI_DEBUG, "Init cci master%d failed",
			io_master_info->cci_client->cci_i2c_master);
		return rc;
	}

	rc = cam_camera_cci_i2c_read_seq(io_master_info->cci_client, start, data,
		addr_type, data_type, end - start + 1);
	if (rc < 0) {
		CAM_ERR(MI_DEBUG, "Sequential read from cci master%d failed",
			io_master_info->cci_client->cci_i2c_master);
		return rc;
	}

	camera_io_release(io_master_info);

	return rc;
}

static ssize_t cam_cci_debug_monitor_read(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct cam_cci_debug *cci_dbg =
		(struct cam_cci_debug *) file->private_data;
	char     *buf;
	size_t    buf_pos = 0;
	ssize_t   rc      = 0;
	uint8_t  *data;
	uint32_t  i;
	uint32_t  start, end;
	uint64_t  hrs, min, sec, ms;
	struct timespec64 timestamp;

	if (!cci_dbg || !cci_dbg->io_master_info)
		return -EINVAL;

	start = cci_dbg->min_reg_addr;
	end   = cci_dbg->max_reg_addr;

	CAM_DBG(MI_DEBUG, "Monitor registers from 0x%x to 0x%x",
		start, end);

	if (end - start + 1 > MAX_DEV_MONITOR_COUNT) {
		end = start + MAX_DEV_MONITOR_COUNT - 1;
		CAM_WARN(MI_DEBUG, "Max monitor regs count is %d, "
			"reset regs to 0x%x - 0x%x", MAX_DEV_MONITOR_COUNT,
			start, end);
	}

	data = vzalloc((end - start + 1));
	if (!data)
		return -ENOMEM;

	rc = __cam_cci_debug_cci_read_seq(cci_dbg->io_master_info,
		data, start, end, cci_dbg->reg_addr_type, cci_dbg->reg_data_type);
	if (rc) {
		goto exit;
	}

	CAM_DBG(MI_DEBUG, "Sequential read from cci master%d done",
		cci_dbg->io_master_info->cci_client->cci_i2c_master);

	buf = kzalloc(count, GFP_KERNEL);
	if (!buf) {
		rc = -ENOMEM;
		goto exit;
	}

	CAM_GET_TIMESTAMP(timestamp);
	CAM_CONVERT_TIMESTAMP_FORMAT(timestamp, hrs, min, sec, ms);

	hrs = hrs + TIME_SHIFT_HOURS % TIME_MAX_HOURS;
	snprintf(buf + buf_pos, count - buf_pos, "--- %.*lu:%.*lu:%.*lu.%.*lu\n",
		PRINT_SINGLE_TIME, hrs, PRINT_SINGLE_TIME, min, PRINT_SINGLE_TIME,
		sec, PRINT_SINGLE_TIME, ms);
	buf_pos += PRINT_TIMESTAMP;

	for (i = start; i <= end; i++) {
		if ((buf_pos + cci_dbg->reg_addr_type * 2 + PRINT_FIXED_LENGTH_4 +
			 cci_dbg->reg_data_type * 2 + 1) > count)
			break;

		snprintf(buf + buf_pos, count - buf_pos, "0x%.*x: ",
			cci_dbg->reg_addr_type * 2, i);
		/* (+ 4) for '0x' and ': ' */
		buf_pos += cci_dbg->reg_addr_type * 2 + PRINT_FIXED_LENGTH_4;

		snprintf(buf + buf_pos, count - buf_pos, "%.*x",
			cci_dbg->reg_data_type * 2, data[i - cci_dbg->min_reg_addr]);
		buf_pos += cci_dbg->reg_data_type * 2;

		buf[buf_pos++] = '\n';

		CAM_DBG(MI_DEBUG, "CCI debug read register 0x%x data 0x%x",
			i, data[i - cci_dbg->min_reg_addr]);
	}

	if (copy_to_user(user_buf, buf, buf_pos)) {
		kfree(buf);
		vfree(cci_dbg->read_data);
		return -EFAULT;
	}

	rc = buf_pos;
	*ppos = 0;

	usleep_range(cci_dbg->interval_ms * 1000,
		 cci_dbg->interval_ms * 1000 + 10);

	kfree(buf);
exit:
	vfree(data);
	return rc;
}

static const struct file_operations cam_cci_debug_monitor = {
	.open = simple_open,
	.read = cam_cci_debug_monitor_read,
	.llseek = default_llseek,
};

static ssize_t cam_cci_debug_regs_read(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct cam_cci_debug *cci_dbg =
		(struct cam_cci_debug *) file->private_data;
	char     *buf;
	size_t    buf_pos = 0;
	ssize_t   rc      = 0;
	uint8_t  *data;
	uint32_t  i;
	uint32_t  start;
	uint32_t *end;
	uint32_t *current_pos;

	if (!cci_dbg || !cci_dbg->io_master_info)
		return -EINVAL;

	start =  cci_dbg->min_reg_addr;
	end   = &cci_dbg->max_reg_addr;
	current_pos = &cci_dbg->current_pos;

	CAM_DBG(MI_DEBUG, "Read registers from 0x%x to 0x%x, "
		"current output position start from %d",
		start, *end, *current_pos);

	if (*end - start + 1 > I2C_REG_DATA_MAX) {
		*end = start + I2C_REG_DATA_MAX - 1;
		CAM_WARN(MI_DEBUG, "Registers count exceeds the maximum limit, "
			"reset register end to 0x%x", *end);
	}

	if (!(*ppos)) {
		cci_dbg->read_data = vzalloc((*end - start + 1));
		if (!cci_dbg->read_data)
			return -ENOMEM;

		data = cci_dbg->read_data;

		rc = __cam_cci_debug_cci_read_seq(cci_dbg->io_master_info,
			data, start, *end, cci_dbg->reg_addr_type,
			cci_dbg->reg_data_type);
		if (rc) {
			goto exit;
		}

		CAM_DBG(MI_DEBUG, "Sequential read from cci master%d done",
			cci_dbg->io_master_info->cci_client->cci_i2c_master);
	}
	else {
		start = *current_pos;
		if (start > *end) {
			*current_pos = cci_dbg->min_reg_addr;
			rc = 0;
			goto exit;
		}

		data = cci_dbg->read_data;
	}

	buf = kzalloc(count, GFP_KERNEL);
	if (!buf) {
		rc = -ENOMEM;
		goto exit;
	}

	for (i = start; i <= *end; i++) {
		if ((buf_pos + cci_dbg->reg_addr_type * 2 + PRINT_FIXED_LENGTH_4 +
			 cci_dbg->reg_data_type * 2 + 1) > count)
			break;

		snprintf(buf + buf_pos, count - buf_pos, "0x%.*x: ",
			cci_dbg->reg_addr_type * 2, i);
		/* (+ 4) for '0x' and ': ' */
		buf_pos += cci_dbg->reg_addr_type * 2 + PRINT_FIXED_LENGTH_4;

		snprintf(buf + buf_pos, count - buf_pos, "%.*x",
			cci_dbg->reg_data_type * 2, data[i - cci_dbg->min_reg_addr]);
		buf_pos += cci_dbg->reg_data_type * 2;

		buf[buf_pos++] = '\n';

		CAM_DBG(MI_DEBUG, "CCI debug read register 0x%x data 0x%x",
			i, data[i - cci_dbg->min_reg_addr]);
	}

	rc = buf_pos;
	*current_pos = i;

	if (copy_to_user(user_buf, buf, buf_pos)) {
		kfree(buf);
		vfree(cci_dbg->read_data);
		return -EFAULT;
	}

	*ppos += buf_pos;

	kfree(buf);
	return rc;
exit:
	vfree(cci_dbg->read_data);
	return rc;
}

static int __cam_cci_debug_cci_write(struct camera_io_master *io_master_info,
	uint32_t reg, uint32_t val, enum camera_sensor_i2c_type addr_type,
	enum camera_sensor_i2c_type data_type)
{
	int rc = 0;
	struct cam_sensor_i2c_reg_setting *write_setting;

	CAM_DBG(MI_DEBUG, "Try cci write in cci debugfs");

	write_setting = kzalloc(sizeof(*write_setting), GFP_KERNEL);
	if (!write_setting) {
		return -ENOMEM;
	}

	write_setting->reg_setting = vzalloc(sizeof(*write_setting->reg_setting));
	if (!write_setting->reg_setting) {
		rc = -ENOMEM;
		goto exit;
	}

	rc = camera_io_init(io_master_info);
	if (rc < 0) {
		CAM_ERR(MI_DEBUG, "Init cci master%d failed",
			io_master_info->cci_client->cci_i2c_master);
		goto out;
	}

	write_setting->size = 1;
	write_setting->addr_type = addr_type;
	write_setting->data_type = data_type;
	write_setting->reg_setting[0].reg_addr = reg;
	write_setting->reg_setting[0].reg_data = val;
	write_setting->reg_setting[0].data_mask = 0;
	rc = camera_io_dev_write(io_master_info, write_setting);
	if (rc < 0) {
		CAM_ERR(MI_DEBUG, "CCI write to master%d client failed",
			io_master_info->cci_client->cci_i2c_master);
	}

	camera_io_release(io_master_info);

out:
	vfree(write_setting->reg_setting);
exit:
	kfree(write_setting);
	return rc;
}

static ssize_t cam_cci_debug_regs_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	int     rc;
	char    buf[DEBUGFS_STR_MAX_SIZE];
	char   *start = buf;
	size_t  buf_size;
	unsigned long reg, val;
	struct cam_cci_debug *cci_dbg =
		(struct cam_cci_debug *) file->private_data;

	if (!cci_dbg || !cci_dbg->io_master_info)
		return -EINVAL;

	buf_size = min(count, (sizeof(buf) - 1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	while (*start == ' ')
		start++;
	reg = simple_strtoul(start, &start, 16);

	if ((cci_dbg->reg_addr_type == CAMERA_SENSOR_I2C_TYPE_WORD &&
		 reg > MAX_DEV_TYPE_WORD) || (reg > MAX_DEV_TYPE_BYTE &&
		 cci_dbg->reg_addr_type == CAMERA_SENSOR_I2C_TYPE_BYTE)) {
		CAM_ERR(MI_DEBUG, "Assigned address exceeds upper limit");
		return -EINVAL;
	}

	while (*start == ' ')
		start++;
	if (kstrtoul(start, 16, &val)) {
		CAM_ERR(MI_DEBUG, "Data to write is invalid");
		return -EINVAL;
	}
	if ((cci_dbg->reg_data_type == CAMERA_SENSOR_I2C_TYPE_WORD &&
		 val > MAX_DEV_TYPE_WORD) || (val > MAX_DEV_TYPE_BYTE &&
		 cci_dbg->reg_data_type == CAMERA_SENSOR_I2C_TYPE_BYTE)) {
		CAM_ERR(MI_DEBUG, "Assigned data exceeds upper limit");
		return -EINVAL;
	}

	CAM_INFO(MI_DEBUG, "Set value 0x%x to register 0x%x via cci debugfs",
		val, reg);

	rc = __cam_cci_debug_cci_write(cci_dbg->io_master_info,
		reg, val, cci_dbg->reg_addr_type, cci_dbg->reg_data_type);
	if (rc < 0) {
		CAM_ERR(MI_DEBUG, "Write to cci master%d client failed",
			cci_dbg->io_master_info->cci_client->cci_i2c_master);
		return  rc;
	}

	return buf_size;
}

static const struct file_operations cam_cci_debug_regs = {
	.open = simple_open,
	.read = cam_cci_debug_regs_read,
	.write = cam_cci_debug_regs_write,
	.llseek = default_llseek,
};

int cam_cci_dev_rename_debugfs_entry(void *cci_debug,
	char *device_name)
{
	int rc = 0;
	struct dentry *dbgfileptr = NULL;
	struct cam_cci_debug *cci_dbg =
		(struct cam_cci_debug *)cci_debug;

	if (!cci_dbg || !cci_dbg->parent_entry || !cci_dbg->entry)
		return -EINVAL;

	dbgfileptr = debugfs_rename(cci_dbg->parent_entry, cci_dbg->entry,
		cci_dbg->parent_entry, device_name);
	if (!dbgfileptr) {
		CAM_WARN(MI_DEBUG, "%s: debugfs directory rename failed",
			device_name);
		rc = -ENOENT;
		return rc;
	}

	CAM_DBG(MI_DEBUG, "%s: success to rename debugfs entry", device_name);
	return rc;
}

int cam_cci_dev_create_debugfs_entry(char *dev_name,
	uint32_t dev_index, char *dev_type,
	struct camera_io_master *io_master_info,
	enum cci_i2c_master_t cci_i2c_master,
	void **cci_debug)
{
	int rc = 0;
	struct dentry *dbgfileptr     = NULL;
	struct dentry *debugfs_device = NULL;
	struct dentry *debugfs_prop   = NULL;
	struct cam_cci_debug *cci_dbg = NULL;
	char debugfs_name[DEBUGFS_NAME_MAX_SIZE];

	CAM_DBG(MI_DEBUG, "start to create debugfs for %s%u",
		dev_type, dev_index);

	cci_dbg = kzalloc(sizeof(*cci_dbg), GFP_KERNEL);
	if (!cci_dbg) {
		CAM_ERR(MI_DEBUG, "%s: debugfs struct creation failed",
			dev_type);
		return -ENOMEM;
	}

	cci_dbg->name  = dev_name;
	cci_dbg->index = dev_index;
	cci_dbg->type  = dev_type;
	cci_dbg->io_master_info = io_master_info;
	cci_dbg->min_reg_addr   = MIN_DEV_REG_ADDR;
	cci_dbg->max_reg_addr   = MAX_DEV_TYPE_WORD;
	cci_dbg->current_pos    = MIN_DEV_REG_ADDR;
	cci_dbg->interval_ms    = DEFAULT_MONITOR_INTERVAL_MS;
	cci_dbg->reg_addr_type  = CAMERA_SENSOR_I2C_TYPE_WORD;
	cci_dbg->reg_data_type  = CAMERA_SENSOR_I2C_TYPE_BYTE;

	dbgfileptr = debugfs_lookup("camera", NULL);
	if (!dbgfileptr) {
		CAM_ERR(MI_DEBUG, "%s: camera root debugfs dir lookup failed",
			dev_type);
		rc = -ENOENT;
		goto end;
	}

	dbgfileptr = debugfs_lookup("cci", dbgfileptr);
	if (!dbgfileptr) {
		CAM_ERR(MI_DEBUG, "%s: cci root debugfs dir lookup failed",
			dev_type);
		rc = -ENOENT;
		goto end;
	}

	snprintf(debugfs_name, DEBUGFS_NAME_MAX_SIZE, "cci%d",
		io_master_info->cci_client->cci_device);
	dbgfileptr = debugfs_lookup(debugfs_name, dbgfileptr);
	if (!dbgfileptr) {
		CAM_ERR(MI_DEBUG, "%s: cci master debugfs dir lookup failed");
		rc = -ENOENT;
		goto end;
	}
	cci_dbg->parent_entry = dbgfileptr;

	snprintf(debugfs_name, DEBUGFS_NAME_MAX_SIZE, "%s%u",
		dev_type, dev_index);
	dbgfileptr = debugfs_create_dir(debugfs_name, dbgfileptr);
	if (!dbgfileptr) {
		CAM_ERR(MI_DEBUG, "%s: cci device debugfs dir creation fail",
			dev_type);
		rc = -ENOENT;
		goto end;
	}
	debugfs_device = dbgfileptr;
	cci_dbg->entry = debugfs_device;

	dbgfileptr = debugfs_create_file("name", 0400,
		debugfs_device, cci_dbg->name, &cam_cci_debug_str_fops);

	dbgfileptr = debugfs_create_file("range", 0600,
		debugfs_device, cci_dbg, &cam_cci_debug_range);

	dbgfileptr = debugfs_create_file("registers_func", 0600,
		debugfs_device, cci_dbg, &cam_cci_debug_regs);

	dbgfileptr = debugfs_create_file("monitor_func", 0400,
		debugfs_device, cci_dbg, &cam_cci_debug_monitor);

	snprintf(debugfs_name, DEBUGFS_NAME_MAX_SIZE, "property");
	dbgfileptr = debugfs_create_dir(debugfs_name, debugfs_device);
	if (!dbgfileptr) {
		CAM_ERR(MI_DEBUG, "%s: cci device property debugfs dir creation fail",
			dev_type);
		rc = -ENOENT;
		goto end;
	}
	debugfs_prop = dbgfileptr;

	dbgfileptr = debugfs_create_file("index", 0400,
		debugfs_prop, &cci_dbg->index, &cam_cci_debug_val_readonly);

	dbgfileptr = debugfs_create_file("type", 0400,
		debugfs_prop, cci_dbg->type, &cam_cci_debug_str_fops);

	dbgfileptr = debugfs_create_file("reg_addr_type", 0600,
		debugfs_prop, &cci_dbg->reg_addr_type, &cam_cci_debug_addr_type);

	dbgfileptr = debugfs_create_file("reg_data_type", 0600,
		debugfs_prop, &cci_dbg->reg_data_type, &cam_cci_debug_data_type);

	dbgfileptr = debugfs_create_file("monitor_interval_ms", 0600,
		debugfs_prop, &cci_dbg->interval_ms, &cam_cci_debug_val);

	if (IS_ERR(dbgfileptr)) {
		if (PTR_ERR(dbgfileptr) == -ENODEV)
			CAM_ERR(MI_DEBUG, "DebugFS not enabled");
		else {
			rc = PTR_ERR(dbgfileptr);
			goto end;
		}
	}

	*cci_debug = cci_dbg;

	CAM_DBG(MI_DEBUG, "success to create debugfs for %s%u",
		dev_type, dev_index);
	return rc;
end:
	kfree(cci_dbg);
	return rc;
}

void cam_cci_dev_remove_debugfs_entry(void *cci_debug)
{
	struct cam_cci_debug *cci_dbg =
		(struct cam_cci_debug *)cci_debug;

	debugfs_remove_recursive(cci_dbg->entry);
	kfree(cci_dbg);
}
