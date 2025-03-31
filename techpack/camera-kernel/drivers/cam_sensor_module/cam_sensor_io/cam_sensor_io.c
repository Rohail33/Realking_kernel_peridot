// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "cam_sensor_io.h"
#include "cam_sensor_i2c.h"
#include "cam_sensor_i3c.h"
#include <linux/pm_runtime.h>

static int cam_cci_io_protect = 0;
module_param(cam_cci_io_protect, int, 0644);

int32_t camera_io_dev_poll(struct camera_io_master *io_master_info,
	uint32_t addr, uint16_t data, uint32_t data_mask,
	enum camera_sensor_i2c_type addr_type,
	enum camera_sensor_i2c_type data_type,
	uint32_t delay_ms)
{
	int16_t mask = data_mask & 0xFF;

	if (!io_master_info) {
		CAM_ERR(CAM_SENSOR, "Invalid Args");
		return -EINVAL;
	}

	switch (io_master_info->master_type) {
	case CCI_MASTER:
		return cam_cci_i2c_poll(io_master_info->cci_client,
			addr, data, mask, data_type, addr_type, delay_ms);
	case I2C_MASTER:
		return cam_qup_i2c_poll(io_master_info->client,
			addr, data, data_mask, addr_type, data_type, delay_ms);
	case I3C_MASTER:
		return cam_qup_i3c_poll(io_master_info->i3c_client,
			addr, data, data_mask, addr_type, data_type, delay_ms);
	default:
		CAM_ERR(CAM_SENSOR, "Invalid Master Type: %d", io_master_info->master_type);
	}

	return -EINVAL;
}

int32_t camera_io_dev_erase(struct camera_io_master *io_master_info,
	uint32_t addr, uint32_t size)
{
	if (!io_master_info) {
		CAM_ERR(CAM_SENSOR, "Invalid Args");
		return -EINVAL;
	}

	if (size == 0)
		return 0;

	switch (io_master_info->master_type) {
	case SPI_MASTER:
		CAM_DBG(CAM_SENSOR, "Calling SPI Erase");
		return cam_spi_erase(io_master_info, addr, CAMERA_SENSOR_I2C_TYPE_WORD, size);
	case I2C_MASTER:
	case CCI_MASTER:
	case I3C_MASTER:
		CAM_ERR(CAM_SENSOR, "Erase not supported on Master Type: %d",
			io_master_info->master_type);
		return -EINVAL;
	default:
		CAM_ERR(CAM_SENSOR, "Invalid Master Type: %d", io_master_info->master_type);
	}

	return -EINVAL;
}

int32_t camera_io_dev_read(struct camera_io_master *io_master_info,
	uint32_t addr, uint32_t *data,
	enum camera_sensor_i2c_type addr_type,
	enum camera_sensor_i2c_type data_type,
	bool is_probing)
{
	if (!io_master_info) {
		CAM_ERR(CAM_SENSOR, "Invalid Args");
		return -EINVAL;
	}

	switch (io_master_info->master_type) {
	case SPI_MASTER:
		return cam_spi_read(io_master_info, addr, data, addr_type, data_type);
	case I2C_MASTER:
		return cam_qup_i2c_read(io_master_info->client,
			addr, data, addr_type, data_type);
	case CCI_MASTER:
		return cam_cci_i2c_read(io_master_info->cci_client,
			addr, data, addr_type, data_type, is_probing);
	case I3C_MASTER:
		return cam_qup_i3c_read(io_master_info->i3c_client,
			addr, data, addr_type, data_type);
	default:
		CAM_ERR(CAM_SENSOR, "Invalid Master Type: %d", io_master_info->master_type);
	}

	return -EINVAL;
}

int32_t camera_io_dev_read_seq(struct camera_io_master *io_master_info,
	uint32_t addr, uint8_t *data,
	enum camera_sensor_i2c_type addr_type,
	enum camera_sensor_i2c_type data_type, int32_t num_bytes)
{
	switch (io_master_info->master_type) {
	case CCI_MASTER:
		return cam_camera_cci_i2c_read_seq(io_master_info->cci_client,
			addr, data, addr_type, data_type, num_bytes);
	case I2C_MASTER:
		return cam_qup_i2c_read_seq(io_master_info->client,
			addr, data, addr_type, num_bytes);
	case SPI_MASTER:
		return cam_spi_read_seq(io_master_info, addr, data, addr_type, num_bytes);
	case I3C_MASTER:
		return cam_qup_i3c_read_seq(io_master_info->i3c_client,
			addr, data, addr_type, num_bytes);
	default:
		CAM_ERR(CAM_SENSOR, "Invalid Master Type: %d", io_master_info->master_type);
	}

	return -EINVAL;
}

int32_t camera_io_dev_write(struct camera_io_master *io_master_info,
	struct cam_sensor_i2c_reg_setting *write_setting)
{
	if (!write_setting || !io_master_info) {
		CAM_ERR(CAM_SENSOR,
			"Input parameters not valid ws: %pK ioinfo: %pK",
			write_setting, io_master_info);
		return -EINVAL;
	}

	if (!write_setting->reg_setting) {
		CAM_ERR(CAM_SENSOR, "Invalid Register Settings");
		return -EINVAL;
	}

	switch (io_master_info->master_type) {
	case CCI_MASTER:
		return cam_cci_i2c_write_table(io_master_info, write_setting);
	case I2C_MASTER:
		return cam_qup_i2c_write_table(io_master_info, write_setting);
	case SPI_MASTER:
		return cam_spi_write_table(io_master_info, write_setting);
	case I3C_MASTER:
		return cam_qup_i3c_write_table(io_master_info, write_setting);
	default:
		CAM_ERR(CAM_SENSOR, "Invalid Master Type:%d", io_master_info->master_type);
	}

	return -EINVAL;
}

int32_t camera_io_dev_write_continuous(struct camera_io_master *io_master_info,
	struct cam_sensor_i2c_reg_setting *write_setting,
	uint8_t cam_sensor_i2c_write_flag)
{
	if (!write_setting || !io_master_info) {
		CAM_ERR(CAM_SENSOR,
			"Input parameters not valid ws: %pK ioinfo: %pK",
			write_setting, io_master_info);
		return -EINVAL;
	}

	if (!write_setting->reg_setting) {
		CAM_ERR(CAM_SENSOR, "Invalid Register Settings");
		return -EINVAL;
	}

	switch (io_master_info->master_type) {
	case CCI_MASTER:
		return cam_cci_i2c_write_continuous_table(io_master_info,
			write_setting, cam_sensor_i2c_write_flag);
	case I2C_MASTER:
		return cam_qup_i2c_write_continuous_table(io_master_info,
			write_setting, cam_sensor_i2c_write_flag);
	case SPI_MASTER:
		return cam_spi_write_table(io_master_info, write_setting);
	case I3C_MASTER:
		return cam_qup_i3c_write_continuous_table(io_master_info,
			write_setting, cam_sensor_i2c_write_flag);
	default:
		CAM_ERR(CAM_SENSOR, "Invalid Master Type:%d", io_master_info->master_type);
	}

	return -EINVAL;
}

int32_t camera_io_init(struct camera_io_master *io_master_info)
{
	int rc = 0;

	if (!io_master_info) {
		CAM_ERR(CAM_SENSOR, "Invalid Args");
		return -EINVAL;
	}

	switch (io_master_info->master_type) {
	case CCI_MASTER:
		io_master_info->cci_client->cci_subdev = cam_cci_get_subdev(
						io_master_info->cci_client->cci_device);
		return cam_sensor_cci_i2c_util(io_master_info->cci_client, MSM_CCI_INIT);
	case I2C_MASTER:
	case I3C_MASTER:
		if ((io_master_info->client != NULL) &&
			(io_master_info->client->adapter != NULL)) {
			CAM_DBG(CAM_SENSOR, "%s:%d: Calling get_sync",
				__func__, __LINE__);
			rc = pm_runtime_get_sync(io_master_info->client->adapter->dev.parent);
			if (rc < 0) {
				CAM_ERR(CAM_SENSOR, "Failed to get sync rc: %d", rc);
				return -EINVAL;
			}
		}
		return 0;
	case SPI_MASTER: return 0;
	default:
		CAM_ERR(CAM_SENSOR, "Invalid Master Type:%d", io_master_info->master_type);
	}

	return -EINVAL;
}

int32_t camera_io_release(struct camera_io_master *io_master_info)
{
	if (!io_master_info) {
		CAM_ERR(CAM_SENSOR, "Invalid Args");
		return -EINVAL;
	}

	switch (io_master_info->master_type) {
	case CCI_MASTER:
		return cam_sensor_cci_i2c_util(io_master_info->cci_client, MSM_CCI_RELEASE);
	case I2C_MASTER:
	case I3C_MASTER:
		if ((io_master_info->client != NULL) &&
			(io_master_info->client->adapter != NULL)) {
			CAM_DBG(CAM_SENSOR, "%s:%d: Calling put_sync",
				__func__, __LINE__);
			pm_runtime_put_sync(io_master_info->client->adapter->dev.parent);
		}
		return 0;
	case SPI_MASTER: return 0;
	default:
		CAM_ERR(CAM_SENSOR, "Invalid Master Type:%d", io_master_info->master_type);
	}

	return -EINVAL;
}

void cam_cci_add_io_fail_count(struct cci_error_info *error_info)
{
	if (cam_cci_io_protect == 1)
		atomic_add(1, &error_info->io_fail_count);
}

void cam_cci_reset_io_fail_count(struct cci_error_info *error_info)
{
	if (cam_cci_io_protect == 1)
		atomic_set(&error_info->io_fail_count, 0);
}

int cam_cci_read_io_fail_count(struct cci_error_info *error_info)
{
	return atomic_read(&error_info->io_fail_count);
}

bool cam_check_cci_is_error(struct cci_error_info *error_info)
{
	// Hardware exception protection not enabled
	if (cam_cci_io_protect != 1) {
		return false;
	}

	if (atomic_read(&error_info->damage_count) >= MAX_DAMAGE_COUNT ||
		atomic_read(&error_info->io_fail_count) >= MAX_IO_FAIL_COUNT) {
		return true;
	}

	return false;
}

void cam_cci_set_init_fail(struct cci_error_info *error_info)
{
	if (cam_cci_io_protect == 1)
		atomic_set(&error_info->init_fail_flag, 1);
}

void cam_cci_clear_init_fail(struct cci_error_info *error_info)
{
	if (cam_cci_io_protect == 1)
		atomic_set(&error_info->init_fail_flag, 0);
}

int cam_cci_read_damage_count(struct cci_error_info *error_info)
{
	return atomic_read(&error_info->damage_count);
}

bool cam_check_cci_is_damage(struct cci_error_info *error_info)
{
	struct timespec64 curr_timestamp;
	struct tm ts_damage;
	struct tm ts_curr;

	// Hardware exception protection not enabled
	if (cam_cci_io_protect != 1) {
		return false;
	}

	// Continuous initialization failure or read/write failure
	if (MAX_IO_FAIL_COUNT <= atomic_read(&error_info->io_fail_count) ||
		1 == atomic_read(&error_info->init_fail_flag)) {
		if (MAX_DAMAGE_COUNT > atomic_read(&error_info->damage_count)) {
			atomic_add(1, &error_info->damage_count);

			if (1 == atomic_read(&error_info->damage_count))
				ktime_get_clocktai_ts64(&error_info->frist_damage_timestamp);

			/* If the time difference between the last and first damages is greater than 24 hours,
			   it will not be considered as hardware damage
			*/
			if (MAX_DAMAGE_COUNT == atomic_read(&error_info->damage_count)) {
				time64_to_tm(error_info->frist_damage_timestamp.tv_sec, 0, &ts_damage);
				CAM_INFO(CAM_SENSOR, "frist damage time,year of day:%d time: %d-%d %d:%d:%d",
					ts_damage.tm_yday, ts_damage.tm_mon + 1, ts_damage.tm_mday, ts_damage.tm_hour,
					ts_damage.tm_min, ts_damage.tm_sec);

				ktime_get_clocktai_ts64(&curr_timestamp);
				time64_to_tm(curr_timestamp.tv_sec, 0, &ts_curr);
				CAM_INFO(CAM_SENSOR, "current damage time,year of day:%d time: %d-%d %d:%d:%d",
					ts_curr.tm_yday, ts_curr.tm_mon + 1, ts_curr.tm_mday, ts_curr.tm_hour,
					ts_curr.tm_min, ts_curr.tm_sec);

				if (ts_curr.tm_yday != ts_damage.tm_yday) {
					// In the same year, a damage event occurred the next day
					if (ts_curr.tm_yday > ts_damage.tm_yday &&
						((ts_curr.tm_yday-ts_damage.tm_yday)*24+ts_curr.tm_hour-ts_damage.tm_hour) > 24)
						atomic_set(&error_info->damage_count, 0);

					// If crossing the New Year, start counting from the starting point of the new year
					if (ts_curr.tm_yday < ts_damage.tm_yday &&
						(ts_curr.tm_yday*24+ts_curr.tm_hour-ts_damage.tm_hour) > 24)
						atomic_set(&error_info->damage_count, 0);

					CAM_INFO(CAM_SENSOR, "current time:device damage count reset:%d",
						atomic_read(&error_info->damage_count));
				}
			}

			CAM_ERR(CAM_SENSOR, "device damage count:%d",
				atomic_read(&error_info->damage_count));

			return true;
		}
	}//If the number of damages has not been reached and no abnormalities have occurred, reset to zero
	else if (MAX_DAMAGE_COUNT > atomic_read(&error_info->damage_count)){
		atomic_set(&error_info->damage_count, 0);
		CAM_DBG(CAM_SENSOR, "device damage count reset:%d",
			atomic_read(&error_info->damage_count));
	}

	return false;
}