// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/firmware.h>

#include "cam_sensor_cmn_header.h"
#include "cam_ois_core.h"
#include "cam_ois_soc.h"
#include "cam_sensor_util.h"
#include "cam_debug_util.h"
#include "cam_res_mgr_api.h"
#include "cam_common_util.h"
#include "cam_packet_util.h"
#include "xiaomi_flash_ois.h"
#include "cam_parklens_thread.h" //xiaomi add

#define CAM_OIS_FW_VERSION_CHECK_MASK 0x1

#define OIS_TRANS_SIZE                256
#define MAX_OIS_DEV                   5
#define CAM_OIS_FW_VERSION_MAX_LENGTH 2048

static struct ois_firmware_info ois_firmware[MAX_OIS_DEV] = {0};

static inline uint64_t swap_high_byte_and_low_byte(uint8_t *src,
	uint8_t size_bytes)
{
	uint64_t ret_value = 0x00;
	uint8_t  cycle = 0;

	for (cycle = 0; cycle < size_bytes; cycle++)
		ret_value = ((ret_value<<8) | ((*(src+cycle))&0xff));

	return ret_value;
}

static inline uint64_t swap_high_word_and_low_word(uint16_t *src,
	uint8_t size_words)
{
	uint64_t ret_value = 0x00;
	uint8_t  cycle = 0;

	for (cycle = 0; cycle < size_words; cycle++)
		ret_value = ((ret_value<<16) | ((*(src+cycle))&0xffff));

	return ret_value;
}


int32_t cam_ois_construct_default_power_setting(
	struct cam_sensor_power_ctrl_t *power_info)
{
	int rc = 0;

	power_info->power_setting_size = 1;
	power_info->power_setting =
		kzalloc(sizeof(struct cam_sensor_power_setting),
			GFP_KERNEL);
	if (!power_info->power_setting)
		return -ENOMEM;

	power_info->power_setting[0].seq_type = SENSOR_VAF;
	power_info->power_setting[0].seq_val = CAM_VAF;
	power_info->power_setting[0].config_val = 1;
	power_info->power_setting[0].delay = 2;

	power_info->power_down_setting_size = 1;
	power_info->power_down_setting =
		kzalloc(sizeof(struct cam_sensor_power_setting),
			GFP_KERNEL);
	if (!power_info->power_down_setting) {
		rc = -ENOMEM;
		goto free_power_settings;
	}

	power_info->power_down_setting[0].seq_type = SENSOR_VAF;
	power_info->power_down_setting[0].seq_val = CAM_VAF;
	power_info->power_down_setting[0].config_val = 0;

	return rc;

free_power_settings:
	kfree(power_info->power_setting);
	power_info->power_setting = NULL;
	power_info->power_setting_size = 0;
	return rc;
}


/**
 * cam_ois_get_dev_handle - get device handle
 * @o_ctrl:     ctrl structure
 * @arg:        Camera control command argument
 *
 * Returns success or failure
 */
static int cam_ois_get_dev_handle(struct cam_ois_ctrl_t *o_ctrl,
	void *arg)
{
	struct cam_sensor_acquire_dev    ois_acq_dev;
	struct cam_create_dev_hdl        bridge_params;
	struct cam_control              *cmd = (struct cam_control *)arg;

	if (o_ctrl->bridge_intf.device_hdl != -1) {
		CAM_ERR(CAM_OIS, "Device is already acquired");
		return -EFAULT;
	}
	if (copy_from_user(&ois_acq_dev, u64_to_user_ptr(cmd->handle),
		sizeof(ois_acq_dev)))
		return -EFAULT;

	bridge_params.session_hdl = ois_acq_dev.session_handle;
	bridge_params.ops = &o_ctrl->bridge_intf.ops;
	bridge_params.v4l2_sub_dev_flag = 0;
	bridge_params.media_entity_flag = 0;
	bridge_params.priv = o_ctrl;
	bridge_params.dev_id = CAM_OIS;

	ois_acq_dev.device_handle =
		cam_create_device_hdl(&bridge_params);
	if (ois_acq_dev.device_handle <= 0) {
		CAM_ERR(CAM_OIS, "Can not create device handle");
		return -EFAULT;
	}
	o_ctrl->bridge_intf.device_hdl = ois_acq_dev.device_handle;
	o_ctrl->bridge_intf.session_hdl = ois_acq_dev.session_handle;

	CAM_DBG(CAM_OIS, "Device Handle: %d", ois_acq_dev.device_handle);
	if (copy_to_user(u64_to_user_ptr(cmd->handle), &ois_acq_dev,
		sizeof(struct cam_sensor_acquire_dev))) {
		CAM_ERR(CAM_OIS, "ACQUIRE_DEV: copy to user failed");
		return -EFAULT;
	}
	return 0;
}

static int cam_ois_power_up(struct cam_ois_ctrl_t *o_ctrl)
{
	int                                     rc = 0;
	struct cam_hw_soc_info                 *soc_info = &o_ctrl->soc_info;
	struct cam_ois_soc_private             *soc_private;
	struct cam_sensor_power_ctrl_t         *power_info;
	struct completion                      *i3c_probe_completion = NULL;
	struct timespec64                       ts1, ts2; // xiaomi add
	long                                    microsec = 0; // xiaomi add

	/* xiaomi add begin */
	CAM_GET_TIMESTAMP(ts1);
	CAM_DBG(MI_DEBUG, "%s start power_up", o_ctrl->ois_name);
	/* xiaomi add end */

	soc_private = (struct cam_ois_soc_private *)o_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;

	if ((power_info->power_setting == NULL) &&
		(power_info->power_down_setting == NULL)) {
		CAM_INFO(CAM_OIS,
			"Using default power settings");
		rc = cam_ois_construct_default_power_setting(power_info);
		if (rc < 0) {
			CAM_ERR(CAM_OIS,
				"Construct default ois power setting failed.");
			return rc;
		}
	}

	/* Parse and fill vreg params for power up settings */
	rc = msm_camera_fill_vreg_params(
		soc_info,
		power_info->power_setting,
		power_info->power_setting_size);
	if (rc) {
		CAM_ERR(CAM_OIS,
			"failed to fill vreg params for power up rc:%d", rc);
		return rc;
	}

	/* Parse and fill vreg params for power down settings*/
	rc = msm_camera_fill_vreg_params(
		soc_info,
		power_info->power_down_setting,
		power_info->power_down_setting_size);
	if (rc) {
		CAM_ERR(CAM_OIS,
			"failed to fill vreg params for power down rc:%d", rc);
		return rc;
	}

	power_info->dev = soc_info->dev;

	if (o_ctrl->io_master_info.master_type == I3C_MASTER)
		i3c_probe_completion = cam_ois_get_i3c_completion(o_ctrl->soc_info.index);

	rc = cam_sensor_core_power_up(power_info, soc_info, i3c_probe_completion);
	if (rc) {
		CAM_ERR(CAM_OIS, "failed in ois power up rc %d", rc);
		return rc;
	}

	rc = camera_io_init(&o_ctrl->io_master_info);
	if (rc) {
		CAM_ERR(CAM_OIS, "cci_init failed: rc: %d", rc);
		goto cci_failure;
	}
	/* xiaomi add begin */
	CAM_GET_TIMESTAMP(ts2);
	CAM_GET_TIMESTAMP_DIFF_IN_MICRO(ts1, ts2, microsec);
	CAM_DBG(MI_DEBUG, "%s end power_up, occupy time is: %ld ms",
		o_ctrl->ois_name, microsec/1000);
	/* xiaomi add end */

	return rc;
cci_failure:
	if (cam_sensor_util_power_down(power_info, soc_info))
		CAM_ERR(CAM_OIS, "Power Down failed");

	return rc;
}

/**
 * cam_ois_power_down - power down OIS device
 * @o_ctrl:     ctrl structure
 *
 * Returns success or failure
 */
static int cam_ois_power_down(struct cam_ois_ctrl_t *o_ctrl)
{
	int32_t                         rc = 0;
	struct cam_sensor_power_ctrl_t  *power_info;
	struct cam_hw_soc_info          *soc_info =
		&o_ctrl->soc_info;
	struct cam_ois_soc_private *soc_private;
	struct timespec64               ts1, ts2; // xiaomi add
	long                            microsec = 0; // xiaomi add

	/* xiaomi add begin */
	CAM_GET_TIMESTAMP(ts1);
	CAM_DBG(MI_DEBUG, "%s start power_down", o_ctrl->ois_name);
	/* xiaomi add end */

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "failed: o_ctrl %pK", o_ctrl);
		return -EINVAL;
	}

	soc_private =
		(struct cam_ois_soc_private *)o_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;
	soc_info = &o_ctrl->soc_info;

	if (!power_info) {
		CAM_ERR(CAM_OIS, "failed: power_info %pK", power_info);
		return -EINVAL;
	}

	rc = cam_sensor_util_power_down(power_info, soc_info);
	if (rc) {
		CAM_ERR(CAM_OIS, "power down the core is failed:%d", rc);
		return rc;
	}

	camera_io_release(&o_ctrl->io_master_info);
	o_ctrl->cam_ois_state = CAM_OIS_ACQUIRE;
	/* xiaomi add begin */
	CAM_GET_TIMESTAMP(ts2);
	CAM_GET_TIMESTAMP_DIFF_IN_MICRO(ts1, ts2, microsec);
	CAM_DBG(MI_DEBUG, "%s end power_down, occupy time is: %ld ms",
		o_ctrl->ois_name, microsec/1000);
	/* xiaomi add end */

	return rc;
}

static int cam_ois_update_time(struct i2c_settings_array *i2c_set,
	enum cam_endianness_type endianness)
{
	struct i2c_settings_list *i2c_list;
	int32_t rc = 0;
	uint32_t size = 0;
	uint32_t i = 0;
	uint64_t qtime_ns = 0;

	if (i2c_set == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	rc = cam_sensor_util_get_current_qtimer_ns(&qtime_ns);
	if (rc < 0) {
		CAM_ERR(CAM_OIS,
			"Failed to get current qtimer value: %d",
			rc);
		return rc;
	}

	if (endianness == CAM_ENDIANNESS_BIG)
		qtime_ns = swap_high_word_and_low_word((uint16_t *)(&qtime_ns),
					sizeof(qtime_ns) / sizeof(uint16_t));

	list_for_each_entry(i2c_list,
		&(i2c_set->list_head), list) {
		if (i2c_list->op_code ==  CAM_SENSOR_I2C_WRITE_SEQ) {
			size = i2c_list->i2c_settings.size;
			/* qtimer is 8 bytes so validate here*/
			if (size * (uint32_t)(i2c_list->i2c_settings.data_type) != 8) {
				CAM_ERR(CAM_OIS, "Invalid write time settings");
				return -EINVAL;
			}
			switch (i2c_list->i2c_settings.data_type) {
			case CAMERA_SENSOR_I2C_TYPE_BYTE:
				for (i = 0; i < size; i++) {
					CAM_DBG(CAM_OIS, "time: reg_data[%d]: 0x%x",
						i, (qtime_ns & 0xFF));
					i2c_list->i2c_settings.reg_setting[i].reg_data =
						(qtime_ns & 0xFF);
					qtime_ns >>= 8;
				}

				break;
			case CAMERA_SENSOR_I2C_TYPE_WORD:
				for (i = 0; i < size; i++) {
					uint16_t  data = (qtime_ns & 0xFFFF);
					i2c_list->i2c_settings.reg_setting[i].reg_data =
						data;

					qtime_ns >>= 16;

					CAM_DBG(CAM_OIS, "time: reg_data[%d]: 0x%x",
							i, data);
				}

				break;
			default:
				CAM_ERR(CAM_OIS, "Unsupported reg data type");
				return -EINVAL;
			}
		}
	}

	return rc;
}

static int cam_ois_apply_settings(struct cam_ois_ctrl_t *o_ctrl,
	struct i2c_settings_array *i2c_set)
{
	struct i2c_settings_list *i2c_list;
	int32_t rc = 0;
	uint32_t i, size;
	int32_t j = 0; // xiaomi add

	if (o_ctrl == NULL || i2c_set == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	if (i2c_set->is_settings_valid != 1) {
		CAM_ERR(CAM_OIS, " Invalid settings");
		return -EINVAL;
	}

	list_for_each_entry(i2c_list,
		&(i2c_set->list_head), list) {
		/* xiaomi add I2C trace begin */
		switch (i2c_list->op_code) {
		case CAM_SENSOR_I2C_WRITE_RANDOM:
		case CAM_SENSOR_I2C_WRITE_BURST:
		case CAM_SENSOR_I2C_WRITE_SEQ: {
			for (j = 0;j < i2c_list->i2c_settings.size;j++) {
				trace_cam_i2c_write_log_event("[OISSETTINGS]", o_ctrl->ois_name,
					i2c_set->request_id, j, "WRITE", i2c_list->i2c_settings.reg_setting[j].reg_addr,
					i2c_list->i2c_settings.reg_setting[j].reg_data);
			}
			break;
		}
		case CAM_SENSOR_I2C_READ_RANDOM:
		case CAM_SENSOR_I2C_READ_SEQ: {
			for (j = 0;j < i2c_list->i2c_settings.size;j++) {
				trace_cam_i2c_write_log_event("[OISSETTINGS]", o_ctrl->ois_name,
					i2c_set->request_id, j, "READ", i2c_list->i2c_settings.reg_setting[j].reg_addr,
					i2c_list->i2c_settings.reg_setting[j].reg_data);
			}
			break;
		}
		case CAM_SENSOR_I2C_POLL: {
			for (j = 0;j < i2c_list->i2c_settings.size;j++) {
				trace_cam_i2c_write_log_event("[OISSETTINGS]", o_ctrl->ois_name,
					i2c_set->request_id, j, "POLL", i2c_list->i2c_settings.reg_setting[j].reg_addr,
					i2c_list->i2c_settings.reg_setting[j].reg_data);
			}
			break;
		}
		default:
			break;
		} /* xiaomi add I2C trace end */

		if (true == cam_check_cci_is_error(&o_ctrl->cci_err_info)) {
			CAM_ERR(CAM_OIS, "damage count:%d IO fail count:%d skip apply setting!",
				cam_cci_read_damage_count(&o_ctrl->cci_err_info),
				cam_cci_read_io_fail_count(&o_ctrl->cci_err_info));
			return -ENODEV;
		}

		if (i2c_list->op_code ==  CAM_SENSOR_I2C_WRITE_RANDOM) {
			rc = camera_io_dev_write(&(o_ctrl->io_master_info),
				&(i2c_list->i2c_settings));
			if (rc < 0) {
				cam_cci_add_io_fail_count(&o_ctrl->cci_err_info);
				CAM_ERR(CAM_OIS,
					"Failed in Applying i2c wrt settings, IO fail count:%d",
					cam_cci_read_io_fail_count(&o_ctrl->cci_err_info));
				return rc;
			}
		} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_SEQ) {
			rc = camera_io_dev_write_continuous(
				&(o_ctrl->io_master_info),
				&(i2c_list->i2c_settings),
				CAM_SENSOR_I2C_WRITE_SEQ);
			if (rc < 0) {
				cam_cci_add_io_fail_count(&o_ctrl->cci_err_info);
				CAM_ERR(CAM_OIS,
					"Failed to seq write I2C settings: %d, IO fail count:%d",
					rc, cam_cci_read_io_fail_count(&o_ctrl->cci_err_info));
				return rc;
			}
		} else if (i2c_list->op_code == CAM_SENSOR_I2C_POLL) {
			size = i2c_list->i2c_settings.size;
			for (i = 0; i < size; i++) {
				/* xiaomi add begin poll with 4bytes little endian*/
				if (o_ctrl->opcode.is_littleendian_op){
					rc = cam_cci_i2c_poll_with_32(o_ctrl->io_master_info.cci_client,
					i2c_list->i2c_settings.reg_setting[i].reg_addr,
					i2c_list->i2c_settings.reg_setting[i].reg_data,
					i2c_list->i2c_settings.reg_setting[i].data_mask,
					i2c_list->i2c_settings.data_type,
					i2c_list->i2c_settings.addr_type,
					i2c_list->i2c_settings.reg_setting[i].delay);
					if (I2C_COMPARE_MATCH == rc){
						CAM_DBG(CAM_OIS, "i2c poll addr 0x%x data 0x%x success",
							i2c_list->i2c_settings.reg_setting[i].reg_addr,
							i2c_list->i2c_settings.reg_setting[i].reg_data);
					}
				} else {/* xiaomi add end*/
					rc = camera_io_dev_poll(
					&(o_ctrl->io_master_info),
					i2c_list->i2c_settings.reg_setting[i].reg_addr,
					i2c_list->i2c_settings.reg_setting[i].reg_data,
					i2c_list->i2c_settings.reg_setting[i].data_mask,
					i2c_list->i2c_settings.addr_type,
					i2c_list->i2c_settings.data_type,
					i2c_list->i2c_settings.reg_setting[i].delay);
				}
				if (rc < 0) {
					cam_cci_add_io_fail_count(&o_ctrl->cci_err_info);
					CAM_ERR(CAM_OIS,
						"i2c poll apply setting Fail, IO fail count:%d",
						cam_cci_read_io_fail_count(&o_ctrl->cci_err_info));
					return rc;
				} else if (rc ==  I2C_COMPARE_MISMATCH) {
					// xiaomi modify begin
					rc = 0;
					CAM_ERR(CAM_OIS, "%s i2c poll addr 0x%x data 0x%x mismatch",
							o_ctrl->ois_name,
							i2c_list->i2c_settings.reg_setting[i].reg_addr,
							i2c_list->i2c_settings.reg_setting[i].reg_data);
					// xiaomi modify end
				}else if (rc == I2C_COMPARE_MATCH){
					if (o_ctrl->opcode.fw_addr ==
						i2c_list->i2c_settings.reg_setting[i].reg_addr)
						ois_fw_version_set(
							i2c_list->i2c_settings.reg_setting[i].reg_data,
							o_ctrl->ois_name);
				}
			}
		}

		cam_cci_reset_io_fail_count(&o_ctrl->cci_err_info);
	}

	return rc;
}

//xiaomi add begin
static int32_t cam_ois_i2c_modes_util(
		struct camera_io_master *io_master_info,
		struct i2c_settings_list *i2c_list)
{
	int32_t rc = 0;
	uint32_t i, size;

	if (i2c_list->op_code ==  CAM_SENSOR_I2C_WRITE_RANDOM) {
		rc = camera_io_dev_write((io_master_info),
			&(i2c_list->i2c_settings));

		CAM_DBG(MI_PARKLENS, "[OISParklensLog] write addr: %x  data: %x delay %d",
		i2c_list->i2c_settings.reg_setting->reg_addr,
		i2c_list->i2c_settings.reg_setting->reg_data,
		i2c_list->i2c_settings.delay);

		if (rc < 0) {
			CAM_ERR(MI_PARKLENS, "[OISParklensLog] Failed in Applying i2c wrt settings");
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_SEQ) {
		rc = camera_io_dev_write_continuous(
			(io_master_info),
			&(i2c_list->i2c_settings),
			CAM_SENSOR_I2C_WRITE_SEQ);

		CAM_DBG(MI_PARKLENS, "[OISParklensLog] write continuous addr: %x  data: %x delay %d",
		i2c_list->i2c_settings.reg_setting->reg_addr,
		i2c_list->i2c_settings.reg_setting->reg_data,
		i2c_list->i2c_settings.delay);

		if (rc < 0) {
			CAM_ERR(MI_PARKLENS, "[OISParklensLog] Failed to seq write I2C settings: %d",
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_POLL) {
		size = i2c_list->i2c_settings.size;
		for (i = 0; i < size; i++) {
			rc = camera_io_dev_poll(
			(io_master_info),
			i2c_list->i2c_settings.reg_setting[i].reg_addr,
			i2c_list->i2c_settings.reg_setting[i].reg_data,
			i2c_list->i2c_settings.reg_setting[i].data_mask,
			i2c_list->i2c_settings.addr_type,
			i2c_list->i2c_settings.data_type,
			i2c_list->i2c_settings.reg_setting[i].delay);

			CAM_DBG(MI_PARKLENS, "[OISParklensLog] poll  addr: %x  data: %x delay %d",
			i2c_list->i2c_settings.reg_setting[i].reg_addr,
			i2c_list->i2c_settings.reg_setting[i].reg_data,
			i2c_list->i2c_settings.reg_setting[i].delay);

			if (rc < 0) {
				CAM_ERR(MI_PARKLENS, "[OISParklensLog] i2c poll apply setting Fail");
				return rc;
			}
		}
	}
	return rc;
}

static int32_t ois_parklens_thread_func(void *arg)
{
    struct cam_ois_parklens_ctrl_t *parklens_ctrl = NULL;
	struct cam_ois_soc_private  *soc_private = NULL;
	struct cam_sensor_power_ctrl_t *power_info = NULL;
	struct cam_ois_ctrl_t *o_ctrl = NULL;
	struct i2c_settings_array *i2c_set  = NULL;
	struct i2c_settings_list *i2c_list = NULL;
	struct i2c_settings_list *i2c_list_last = NULL;
	int    ois_parklens_time_with_ms = 100, delaytimes = 0, i=0;
	int32_t parklens_opcode = 0, rc = 0;

	if (!arg) {
		rc = -EINVAL;
		parklens_atomic_set(&(parklens_ctrl->exit_result),
			PARKLENS_EXIT_CREATE_WAKELOCK_FAILED);
		CAM_ERR(MI_PARKLENS, "[OISParklensLog] ois_parklens_thread_func arg is NULL");
	} else {
		o_ctrl = (struct cam_ois_ctrl_t *)arg;
		parklens_ctrl = &(o_ctrl->parklens_ctrl);
		soc_private = (struct cam_ois_soc_private *)o_ctrl->soc_info.soc_private;
		i2c_set     = (struct i2c_settings_array *)(&(o_ctrl->i2c_parklens_data));
		power_info = &soc_private->power_info;
	}

	parklens_event_set(&(parklens_ctrl->start_event));
	if (rc < 0)
		goto exit_without_powerdown;

	CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens thread start up");

	while (ois_parklens_time_with_ms > 0) {
		parklens_opcode = parklens_atomic_read(&(parklens_ctrl->parklens_opcode));
		if (parklens_opcode >= EXIT_PARKLENS_WITH_POWERDOWN) {
			if (parklens_opcode == EXIT_PARKLENS_WITHOUT_POWERDOWN) {
				CAM_DBG(MI_PARKLENS, "[OISParklensLog] exit parklens thread no need power off");
				parklens_atomic_set(
					&(parklens_ctrl->exit_result),
					PARKLENS_EXIT_WITHOUT_POWERDOWN);
				goto exit_without_powerdown;
			} else if (parklens_opcode == EXIT_PARKLENS_WITH_POWERDOWN) {
				CAM_DBG(MI_PARKLENS, "[OISParklensLog] exit parklens thread no need power off");
				parklens_atomic_set(
					&(parklens_ctrl->exit_result),
					PARKLENS_EXIT_WITH_POWERDOWN);
				goto exit_with_powerdown;
			} else {
				CAM_WARN(MI_PARKLENS, "[OISParklensLog] Invalid opcode for parklens %d", parklens_opcode);
				continue;
			}
		}

		if (i2c_set == NULL) {
			CAM_ERR(MI_PARKLENS, "[OISParklensLog] parklens Invalid parklens settings");
			parklens_atomic_set(
				&(parklens_ctrl->exit_result),
				PARKLENS_EXIT_WITHOUT_PARKLENS);
			goto exit_with_powerdown;
		} else if (i2c_set->is_settings_valid != 1) {
			CAM_ERR(MI_PARKLENS, "[OISParklensLog] parklens settings is not valid");
			parklens_atomic_set(
				&(parklens_ctrl->exit_result),
				PARKLENS_EXIT_WITHOUT_PARKLENS);
			goto exit_with_powerdown;
		} else {
			if(!delaytimes){
				i2c_list_last = i2c_list;

				if (i2c_list == NULL)
					i2c_list = list_first_entry(&(i2c_set->list_head), typeof(*i2c_list), list);
				else
					i2c_list = list_next_entry(i2c_list, list);

				if ((i2c_list_last != NULL) && list_entry_is_head(i2c_list, &(i2c_set->list_head), list)) {
					CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens settings execute done");
					parklens_atomic_set(
						&(parklens_ctrl->exit_result),
						PARKLENS_EXIT_WITH_POWERDOWN);
					goto exit_with_powerdown;
				} else {
					delaytimes = i2c_list->i2c_settings.delay;
					i2c_list->i2c_settings.delay = 0;

					for (i = 0; i < (i2c_list->i2c_settings.size); i++) {
						CAM_DBG(MI_PARKLENS, "[OISParklensLog] Address 0x%x data 0x%x delay time %d ms",
							i2c_list->i2c_settings.reg_setting[i].reg_addr,
							i2c_list->i2c_settings.reg_setting[i].reg_data,
							delaytimes);
					}

					if (false == cam_check_cci_is_error(&o_ctrl->cci_err_info)) {
						rc = cam_ois_i2c_modes_util(
							&(o_ctrl->io_master_info),
							i2c_list);
					}
					else {
						rc = - ENODEV;
						CAM_ERR(MI_PARKLENS, "damage count:%d IO fail count:%d skip parklens!",
							cam_cci_read_damage_count(&o_ctrl->cci_err_info),
							cam_cci_read_io_fail_count(&o_ctrl->cci_err_info));
					}

					if (rc < 0) {
						CAM_ERR(MI_PARKLENS, "[OISParklensLog] parklens step failed, %d", rc);
						parklens_atomic_set(
							&(parklens_ctrl->exit_result),
							PARKLENS_EXIT_CCI_ERROR);
						goto exit_with_powerdown;
					} else {
						CAM_DBG(MI_PARKLENS, "[OISParklensLog] ois_parklens_thread_func arg time(ms)/state %d/%d",
							delaytimes,
							parklens_ctrl->parklens_state);
					}

				}
			}
			else{
				--delaytimes;
				usleep_range(990,1010);
				CAM_DBG(MI_PARKLENS, "[OISParklensLog] delay time %d ms",
						delaytimes);
			}
		}
		ois_parklens_time_with_ms--;
		if (0 == ois_parklens_time_with_ms){
			CAM_INFO(MI_PARKLENS, "[OISParklensLog] ois parklens time exceed! power down");
				parklens_atomic_set(
					&(parklens_ctrl->exit_result),
					PARKLENS_EXIT_WITH_POWERDOWN);
		}
	}

exit_with_powerdown:
	CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens thread exit with power down");
	CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens thread remaining time(ms)/result %d/%d",
		ois_parklens_time_with_ms,
		parklens_atomic_read(&(parklens_ctrl->exit_result)));
	lock_power_sync_mutex(o_ctrl->io_master_info.cci_client, o_ctrl->cci_i2c_master);
	rc = cam_ois_power_down(o_ctrl);
	unlock_power_sync_mutex(o_ctrl->io_master_info.cci_client, o_ctrl->cci_i2c_master);
	if (rc < 0) {
		CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens power down failed rc: %d", rc);
		parklens_atomic_set(
			&(parklens_ctrl->exit_result),
			PARKLENS_EXIT_WITH_POWERDOWN_FAILED);
	}
	else
		CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens thread  power down success");

	kfree(power_info->power_setting);
	kfree(power_info->power_down_setting);
	power_info->power_setting = NULL;
	power_info->power_down_setting = NULL;
	power_info->power_setting_size = 0;
	power_info->power_down_setting_size = 0;

exit_without_powerdown:
	if (parklens_atomic_read(&(parklens_ctrl->exit_result)) <=
		PARKLENS_EXIT_WITHOUT_POWERDOWN) {
		CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens thread exit without power down");
		CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens thread exit time(ms)/result %d/%d",
			ois_parklens_time_with_ms,
			parklens_atomic_read(&(parklens_ctrl->exit_result)));
	}

	CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens thread delete request");
	rc = delete_request(&(o_ctrl->i2c_parklens_data));
	if (rc < 0)
		CAM_ERR(MI_PARKLENS, "[OISParklensLog] delete parklens request failed rc: %d", rc);

	parklens_event_set(&(parklens_ctrl->shutdown_event));
	parklens_exit_thread(true);

	return 0;
}
//xiaomi add end

static int cam_ois_slaveInfo_pkt_parser(struct cam_ois_ctrl_t *o_ctrl,
	uint32_t *cmd_buf, size_t len)
{
	int32_t rc = 0;
	struct cam_cmd_ois_info *ois_info;
	int32_t i=0;

	if (!o_ctrl || !cmd_buf || len < sizeof(struct cam_cmd_ois_info)) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	ois_info = (struct cam_cmd_ois_info *)cmd_buf;
	if (o_ctrl->io_master_info.master_type == CCI_MASTER) {
		o_ctrl->io_master_info.cci_client->i2c_freq_mode =
			ois_info->i2c_freq_mode;
		o_ctrl->io_master_info.cci_client->sid =
			ois_info->slave_addr >> 1;
		o_ctrl->ois_fw_flag = ois_info->ois_fw_flag;
		o_ctrl->is_ois_calib = ois_info->is_ois_calib;
		memcpy(o_ctrl->ois_name, ois_info->ois_name, OIS_NAME_LEN);
		o_ctrl->ois_name[OIS_NAME_LEN - 1] = '\0';
		o_ctrl->io_master_info.cci_client->retries = 3;
		o_ctrl->io_master_info.cci_client->id_map = 0;
		memcpy(&(o_ctrl->opcode), &(ois_info->opcode),
			sizeof(struct cam_ois_opcode));
		CAM_DBG(CAM_OIS, "Slave addr: 0x%x Freq Mode: %d",
			ois_info->slave_addr, ois_info->i2c_freq_mode);
	} else if (o_ctrl->io_master_info.master_type == I2C_MASTER) {
		o_ctrl->io_master_info.client->addr = ois_info->slave_addr;
		CAM_DBG(CAM_OIS, "Slave addr: 0x%x", ois_info->slave_addr);
	} else {
		CAM_ERR(CAM_OIS, "Invalid Master type : %d",
			o_ctrl->io_master_info.master_type);
		rc = -EINVAL;
	}

	if ((true == o_ctrl->opcode.fw_mem_store) && (0 == (o_ctrl->fw_store.fw_count)))
	{
		for(i = 0 ; i < MAX_FW_COUNT ; i++){
			o_ctrl->fw_store.fw_name[i] = kzalloc(sizeof(char)*OIS_NAME_LEN, GFP_KERNEL);
			if (!o_ctrl->fw_store.fw_name[i])
				return -ENOMEM;
		}
		CAM_INFO(CAM_OIS, "o_ctrl->ois_name %s kzalloc name", o_ctrl->ois_name);
	}
	return rc;
}

static const struct firmware *cam_fw_mem_find(struct cam_ois_ctrl_t *o_ctrl, const char *fw_name)
{
	int32_t rc = 0;
	uint32_t index;
	const char **string = NULL;

	if (o_ctrl->fw_store.fw_count)
	{
		string = (const char **)(o_ctrl->fw_store.fw_name);
		rc = cam_common_util_get_string_index(string,
			o_ctrl->fw_store.fw_count, fw_name, &index);

		if ((rc == 0) && (index < MAX_FW_COUNT)) {
			CAM_DBG(CAM_OIS, "find fw %s %p",fw_name,o_ctrl->fw_store.fw[index]);
			return o_ctrl->fw_store.fw[index];
		}
	}

	return NULL;
}

static int cam_fw_mem_store(struct cam_ois_ctrl_t *o_ctrl)
{
	int32_t         rc = 0, j = 0, fw_num=0;
	const char      *fw_name                                  = NULL;
	const char      fw_name_exten[MAX_FW_COUNT][OIS_NAME_LEN] ={{"%s.coeff"},{"%s.prog"},{"%s.mem"},{"%s.pher"}};
	struct device   *dev                                      = &(o_ctrl->pdev->dev);

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}
	//store ois fw name in cam
	if (!(o_ctrl->fw_store.fw_count)) {
		CAM_INFO(CAM_OIS, "begin store fw_index, MAX_FW_COUNT %d , so store it", MAX_FW_COUNT);

		for(fw_num = 0, j = 0;fw_num < MAX_FW_COUNT;fw_num++,j++) {
			snprintf(o_ctrl->fw_store.fw_name[j], OIS_NAME_LEN, fw_name_exten[fw_num], o_ctrl->ois_name);
			fw_name = o_ctrl->fw_store.fw_name[j];
			/* Load FW */
			rc = request_firmware((const struct firmware**)&(o_ctrl->fw_store.fw[j]), fw_name, dev);
			if (rc) {
				CAM_WARN(CAM_OIS, "Failed to locate %s", fw_name_exten[fw_num]);
				o_ctrl->fw_store.fw[j] = NULL;
				j--;
			} else {
				CAM_INFO(CAM_OIS, "o_ctrl->ois_name %s fw_name %s store", o_ctrl->ois_name, fw_name);
			}
		}
		o_ctrl->fw_store.fw_count = j;
	} else {
		for(fw_num = 0;fw_num < o_ctrl->fw_store.fw_count;fw_num++) {
			if (NULL != (o_ctrl->fw_store.fw[fw_num]))
				CAM_DBG(CAM_OIS, "fw_num %d fw_name %s size %d", fw_num, o_ctrl->fw_store.fw_name[fw_num], (o_ctrl->fw_store.fw[fw_num])->size);
			else
				CAM_DBG(CAM_OIS, "fw_num %d fw_name %s  is NULL", fw_num, o_ctrl->fw_store.fw_name[fw_num]);
		}
	}
	return rc;
}

static int cam_ois_parse_fw_setting(uint8_t *cmd_buf, uint32_t size,
	struct i2c_settings_array *reg_settings)
{
	int32_t                 rc = 0;
	uint32_t                byte_cnt = 0;
	struct common_header   *cmm_hdr;
	uint16_t                op_code;
	uint32_t                j = 0;
	struct list_head       *list = NULL;

	while (byte_cnt < size) {
		if ((size - byte_cnt) < sizeof(struct common_header)) {
			CAM_ERR(CAM_OIS, "Not enough buffer");
			rc = -EINVAL;
			goto end;
		}
		cmm_hdr = (struct common_header *)cmd_buf;
		op_code = cmm_hdr->fifth_byte;
		CAM_DBG(CAM_OIS, "Command Type:%d, Op code:%d",
				 cmm_hdr->cmd_type, op_code);

		switch (cmm_hdr->cmd_type) {
		case CAMERA_SENSOR_CMD_TYPE_I2C_RNDM_WR: {
			uint32_t cmd_length_in_bytes = 0;
			struct cam_cmd_i2c_random_wr
			*cam_cmd_i2c_random_wr =
			(struct cam_cmd_i2c_random_wr *)cmd_buf;

			if ((size - byte_cnt) < sizeof(struct cam_cmd_i2c_random_wr)) {
				CAM_ERR(CAM_OIS,
					"Not enough buffer provided,size %d,byte_cnt %d",
					size, byte_cnt);
				rc = -EINVAL;
				goto end;
			}

			rc = cam_sensor_handle_random_write(
				cam_cmd_i2c_random_wr,
				reg_settings,
				&cmd_length_in_bytes, &j, &list);
			if (rc < 0) {
				CAM_ERR(CAM_OIS,
				"Failed in random write %d", rc);
				goto end;
			}

			byte_cnt += sizeof(struct cam_cmd_i2c_random_wr);
			cmd_buf += sizeof(struct cam_cmd_i2c_random_wr);

			break;
		}
		case CAMERA_SENSOR_CMD_TYPE_I2C_CONT_WR: {
			uint32_t cmd_length_in_bytes = 0;
			struct cam_cmd_i2c_continuous_wr
			*cam_cmd_i2c_continuous_wr =
			(struct cam_cmd_i2c_continuous_wr *)cmd_buf;

			if ((size - byte_cnt) < sizeof(struct cam_cmd_i2c_continuous_wr)) {
				CAM_ERR(CAM_OIS,
					"Not enough buffer provided,size %d,byte_cnt %d",
					size, byte_cnt);
				rc = -EINVAL;
				goto end;
			}

			rc = cam_sensor_handle_continuous_write(
				cam_cmd_i2c_continuous_wr,
				reg_settings,
				&cmd_length_in_bytes, &j, &list);
			if (rc < 0) {
				CAM_ERR(CAM_OIS,
				"Failed in continuous write %d", rc);
				goto end;
			}

			byte_cnt += sizeof(struct cam_cmd_i2c_continuous_wr);
			cmd_buf += sizeof(struct cam_cmd_i2c_continuous_wr);

			break;
		}
		case CAMERA_SENSOR_CMD_TYPE_WAIT: {
			if (op_code == CAMERA_SENSOR_WAIT_OP_HW_UCND ||
				op_code == CAMERA_SENSOR_WAIT_OP_SW_UCND) {
				if ((size - byte_cnt) <
					sizeof(struct cam_cmd_unconditional_wait)) {
					CAM_ERR(CAM_OIS,
						"Not enough buffer provided,size %d,byte_cnt %d",
						size, byte_cnt);
					rc = -EINVAL;
					goto end;
				}

				rc = cam_sensor_handle_delay(
					(uint32_t **)(&cmd_buf), op_code,
					reg_settings, j, &byte_cnt,
					list);
				if (rc < 0) {
					CAM_ERR(CAM_OIS,
						"delay hdl failed: %d",
						rc);
					goto end;
				}
			} else if (op_code == CAMERA_SENSOR_WAIT_OP_COND) {
				if ((size - byte_cnt) <
					sizeof(struct cam_cmd_conditional_wait)) {
					CAM_ERR(CAM_OIS,
						"Not enough buffer provided,size %d,byte_cnt %d",
						size, byte_cnt);
					rc = -EINVAL;
					goto end;
				}
				rc = cam_sensor_handle_poll(
					(uint32_t **)(&cmd_buf), reg_settings,
					&byte_cnt, &j, &list);
				if (rc < 0) {
					CAM_ERR(CAM_OIS,
						"parsing POLL fail: %d",
						rc);
					goto end;
				}
			} else {
				CAM_ERR(CAM_OIS,
					"Wrong Wait Command: %d",
					op_code);
				rc = -EINVAL;
				goto end;
			}
			break;
		}
		case CAMERA_SENSOR_CMD_TYPE_I2C_RNDM_RD: {
			uint16_t cmd_length_in_bytes = 0;
			struct cam_cmd_i2c_random_rd *i2c_random_rd =
			(struct cam_cmd_i2c_random_rd *)cmd_buf;

			if ((size - byte_cnt) < sizeof(struct cam_cmd_i2c_random_rd)) {
				CAM_ERR(CAM_OIS,
					"Not enough buffer provided,size %d,byte_cnt %d",
					size, byte_cnt);
				rc = -EINVAL;
				goto end;
			}

			rc = cam_sensor_handle_random_read(
				i2c_random_rd,
				reg_settings,
				&cmd_length_in_bytes, &j, &list,
				NULL);
			if (rc < 0) {
				CAM_ERR(CAM_OIS,
				"Failed in random read %d", rc);
				goto end;
			}

			byte_cnt += sizeof(struct cam_cmd_i2c_random_rd);
			cmd_buf += sizeof(struct cam_cmd_i2c_random_rd);

			break;
		}
		case CAMERA_SENSOR_CMD_TYPE_I2C_CONT_RD: {
			uint16_t cmd_length_in_bytes = 0;
			struct cam_cmd_i2c_continuous_rd
			*i2c_continuous_rd =
			(struct cam_cmd_i2c_continuous_rd *)cmd_buf;

			if ((size - byte_cnt) < sizeof(struct cam_cmd_i2c_continuous_rd)) {
				CAM_ERR(CAM_OIS,
					"Not enough buffer provided,size %d,byte_cnt %d",
					size, byte_cnt);
				rc = -EINVAL;
				goto end;
			}

			rc = cam_sensor_handle_continuous_read(
				i2c_continuous_rd,
				reg_settings,
				&cmd_length_in_bytes, &j, &list,
				NULL);
			if (rc < 0) {
				CAM_ERR(CAM_OIS,
				"Failed in continuous read %d", rc);
				goto end;
			}

			byte_cnt += sizeof(struct cam_cmd_i2c_continuous_rd);
			cmd_buf += sizeof(struct cam_cmd_i2c_continuous_rd);

			break;
		}
		default:
			CAM_ERR(CAM_OIS, "Invalid Command Type:%d",
				 cmm_hdr->cmd_type);
			rc = -EINVAL;
			goto end;
		}
	}

end:
	return rc;
}

static int cam_ois_fw_info_pkt_parser(struct cam_ois_ctrl_t *o_ctrl,
	uint32_t *cmd_buf, size_t len)
{
	int32_t                         rc = 0;
	struct cam_cmd_ois_fw_info     *ois_fw_info;
	uint8_t                        *pSettingData = NULL;
	uint32_t                        size = 0;
	uint32_t                        version_size = 0;
	struct i2c_settings_array      *reg_settings = NULL;
	uint8_t                         count = 0;
	uint32_t                        idx;

	if (!o_ctrl || !cmd_buf || len < sizeof(struct cam_cmd_ois_fw_info)) {
		CAM_ERR(CAM_OIS, "Invalid Args,o_ctrl %p,cmd_buf %p,len %d",
			o_ctrl, cmd_buf, len);
		return -EINVAL;
	}

	ois_fw_info = (struct cam_cmd_ois_fw_info *)cmd_buf;
	CAM_DBG(CAM_OIS, "endianness %d, fw_count %d",
		ois_fw_info->endianness, ois_fw_info->fw_count);

	if (ois_fw_info->fw_count <= MAX_OIS_FW_COUNT) {
		memcpy(&o_ctrl->fw_info, ois_fw_info, sizeof(struct cam_cmd_ois_fw_info));
		pSettingData = (uint8_t *)cmd_buf + sizeof(struct cam_cmd_ois_fw_info);

		if ((ois_fw_info->param_mask & CAM_OIS_FW_VERSION_CHECK_MASK) == 0x1) {
			version_size = ois_fw_info->params[0];
			CAM_DBG(CAM_OIS, "versionSize: %d", version_size);
		}

		if ((version_size != 0) && (o_ctrl->i2c_fw_version_data.is_settings_valid == 0)) {
			reg_settings = &o_ctrl->i2c_fw_version_data;
			reg_settings->is_settings_valid = 1;
			rc = cam_ois_parse_fw_setting(pSettingData, version_size, reg_settings);
			if (rc) {
				CAM_ERR(CAM_OIS, "Failed to parse fw version settings");
				return rc;
			}

			pSettingData += version_size;
		}

		for (count = 0; count < ois_fw_info->fw_count*2; count++) {
			idx = count / 2;
			/* init settings */
			if ((count & 0x1) == 0) {
				size = ois_fw_info->fw_param[idx].fw_init_size;
				reg_settings = &o_ctrl->i2c_fw_init_data[idx];
				CAM_DBG(CAM_OIS, "init size %d", size);
			/* finalize settings */
			} else if ((count & 0x1) == 1) {
				size = ois_fw_info->fw_param[idx].fw_finalize_size;
				reg_settings = &o_ctrl->i2c_fw_finalize_data[idx];
				CAM_DBG(CAM_OIS, "finalize size %d", size);
			} else {
				size = 0;
				CAM_DBG(CAM_OIS, "Unsupported case");
				return -EINVAL;
			}

			if (size != 0) {
				reg_settings->is_settings_valid = 1;
				rc = cam_ois_parse_fw_setting(pSettingData, size, reg_settings);
			}

			if (rc) {
				CAM_ERR(CAM_OIS, "Failed to parse fw setting");
				return rc;
			}

			pSettingData += size;
		}
	} else {
		CAM_ERR(CAM_OIS, "Exceed max fw count");
	}

	return rc;
}

static int cam_ois_fw_download_chip(
	struct cam_ois_ctrl_t *o_ctrl,
	const char* fw_name,
	uint32_t fw_addr)
{
	uint16_t                           total_bytes = 0;
	uint8_t                           *ptr = NULL;
	uint32_t                           fw_size;
	const struct firmware             *fw = NULL;
	int32_t                            rc = 0, cnt, i, j;
	struct device                     *dev = &(o_ctrl->pdev->dev);
	struct cam_sensor_i2c_reg_setting  i2c_reg_setting;
	void                              *vaddr = NULL;
	bool                               is_need_reload = false;

	if (true == o_ctrl->opcode.fw_mem_store)
	{
		fw = cam_fw_mem_find(o_ctrl, fw_name);
		if (NULL == fw)
		{
			CAM_DBG(CAM_OIS, "Failed to find %s",fw_name);
			/* Load FW */
			rc = request_firmware(&fw, fw_name, dev);
			if (rc) {
				CAM_ERR(CAM_OIS, "Failed to locate %s", fw_name);
				return rc;
			}
			is_need_reload = true;
		}
	}
	else {
		/* Load FW */
		rc = request_firmware(&fw, fw_name, dev);
		if (rc) {
			CAM_ERR(CAM_OIS, "Failed to locate %s", fw_name);
			return rc;
		}
	}

	total_bytes = fw->size;
	i2c_reg_setting.addr_type = o_ctrl->opcode.fw_addr_type;
	i2c_reg_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	i2c_reg_setting.size = total_bytes;
	i2c_reg_setting.delay = 0;
	fw_size = (sizeof(struct cam_sensor_i2c_reg_array) * total_bytes);
	vaddr = vmalloc(fw_size);
	if (!vaddr) {
		CAM_ERR(CAM_OIS,
			"Failed in allocating i2c_array: fw_size: %u", fw_size);
		if ((false == is_need_reload) && (true == o_ctrl->opcode.fw_mem_store)) {
			CAM_DBG(CAM_OIS, "FW no need release");
		}
		else {
			release_firmware(fw);
		}
		return -ENOMEM;
	}

	CAM_DBG(CAM_OIS, "FW %s size:%d.", fw_name, total_bytes);

	i2c_reg_setting.reg_setting = (struct cam_sensor_i2c_reg_array *) (
		vaddr);

	for (i = 0, ptr = (uint8_t *)fw->data, j = 0; i < total_bytes;) {
		for (cnt = 0; cnt < OIS_TRANS_SIZE && i < total_bytes;
			cnt++, ptr++, i++) {
				i2c_reg_setting.reg_setting[cnt].reg_addr =
					fw_addr + j * OIS_TRANS_SIZE;
				i2c_reg_setting.reg_setting[cnt].reg_data = *ptr;
				i2c_reg_setting.reg_setting[cnt].delay = 0;
				i2c_reg_setting.reg_setting[cnt].data_mask = 0;
		}
		i2c_reg_setting.size = cnt;

		if (o_ctrl->opcode.is_addr_increase) {
			j++;
		}

		rc = camera_io_dev_write_continuous(&(o_ctrl->io_master_info),
			&i2c_reg_setting, CAM_SENSOR_I2C_WRITE_BURST);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "OIS FW(prog) size(%d) download failed. %d",
				total_bytes, rc);
		}
	}

	vfree(vaddr);
	vaddr = NULL;
	if ((false == is_need_reload) && (true == o_ctrl->opcode.fw_mem_store)) {
		CAM_DBG(CAM_OIS, "FW is_need_reload is false not release");
	}
	else {
		release_firmware(fw);
	}
	return rc;
}

static int cam_ois_fw_download(struct cam_ois_ctrl_t *o_ctrl)
{
	int32_t         rc = 0, i;
	const char      *fw_name[MAX_FW_COUNT] = {NULL};
	char            name_prog[OIS_NAME_LEN] = {0};
	char            name_coeff[OIS_NAME_LEN] = {0};
	char            name_mem[OIS_NAME_LEN] = {0};
	char            name_pher[OIS_NAME_LEN] = {0};
	uint32_t        write_addr[MAX_FW_COUNT] = {0};

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	snprintf(name_coeff, OIS_NAME_LEN, "%s.coeff", o_ctrl->ois_name);

	snprintf(name_prog, OIS_NAME_LEN, "%s.prog", o_ctrl->ois_name);

	snprintf(name_mem, OIS_NAME_LEN, "%s.mem", o_ctrl->ois_name);

	snprintf(name_pher, OIS_NAME_LEN, "%s.pher", o_ctrl->ois_name);

	/* cast pointer as const pointer*/
	fw_name[0] = name_prog;
	fw_name[1] = name_coeff;
	fw_name[2] = name_mem;
	fw_name[3] = name_pher;

	write_addr[0] = o_ctrl->opcode.prog;
	write_addr[1] = o_ctrl->opcode.coeff;
	write_addr[2] = o_ctrl->opcode.memory;
	write_addr[3] = o_ctrl->opcode.pheripheral;

	CAM_INFO(CAM_OIS,"%s FW down Bebin",o_ctrl->ois_name);

	for (i=0; i<MAX_FW_COUNT; i++)
	{
		if (INVALID_FW_ADDR == write_addr[i])
		{
			CAM_DBG(CAM_OIS, "OIS FW(%s) addr:0x%x ,skip it",
				 fw_name[i], write_addr[i]);
			continue;
		}

		rc = cam_ois_fw_download_chip(o_ctrl, fw_name[i], write_addr[i]);
		if (rc < 0)
		{
			CAM_ERR(CAM_OIS, "OIS FW(%s) download failed rc: %d",
				 fw_name[i], rc);
			break;
		}
	}

	CAM_INFO(CAM_OIS,"%s FW down End,rc=%d",o_ctrl->ois_name, rc);

	return rc;
}

static int write_ois_fw(uint8_t *fw_data, enum cam_endianness_type endianness,
	struct cam_cmd_ois_fw_param *fw_param, struct camera_io_master io_master_info,
	uint8_t i2c_operation)
{
	int32_t                             rc = 0;
	struct cam_sensor_i2c_reg_setting   setting;
	uint8_t                            *ptr = fw_data;
	int32_t                             cnt = 0, wcnt = 0;
	void                               *vaddr = NULL;
	uint16_t                            data_type = fw_param->fw_data_type;
	uint16_t                            len_per_write = fw_param->fw_len_per_write /
								fw_param->fw_data_type;

	vaddr = vmalloc((sizeof(struct cam_sensor_i2c_reg_array) * len_per_write));
	if (!vaddr) {
		CAM_ERR(CAM_OIS,
			"Failed in allocating i2c_array: size: %u",
			(sizeof(struct cam_sensor_i2c_reg_array) * len_per_write));
		return -ENOMEM;
	}

	setting.reg_setting = (struct cam_sensor_i2c_reg_array *) (vaddr);
	setting.addr_type   = fw_param->fw_addr_type;
	setting.data_type   = fw_param->fw_data_type;
	setting.size        = len_per_write;
	setting.delay       = fw_param->fw_delayUs;

	for (wcnt = 0; wcnt < (fw_param->fw_size/data_type); wcnt += len_per_write) {
		for (cnt = 0; cnt < len_per_write; cnt++, ptr += data_type) {
			setting.reg_setting[cnt].reg_addr =
				fw_param->fw_reg_addr + wcnt + cnt;
			/* Big */
			if (endianness == CAM_ENDIANNESS_BIG) {
				setting.reg_setting[cnt].reg_data =
					(uint32_t)swap_high_byte_and_low_byte(ptr, data_type);
			/* Little */
			} else if (endianness == CAM_ENDIANNESS_LITTLE) {
				switch (data_type) {
				case CAMERA_SENSOR_I2C_TYPE_BYTE:
					setting.reg_setting[cnt].reg_data = *((uint8_t *)ptr);
					break;
				case CAMERA_SENSOR_I2C_TYPE_WORD:
					setting.reg_setting[cnt].reg_data = *((uint16_t *)ptr);
					break;
				default:
					CAM_ERR(CAM_OIS,
						"Unsupported data type");
					rc = -EINVAL;
					goto End;
				}
			}

			setting.reg_setting[cnt].delay = fw_param->fw_delayUs;
			setting.reg_setting[cnt].data_mask = 0;
		}

		if (i2c_operation == CAM_SENSOR_I2C_WRITE_RANDOM) {
			rc = camera_io_dev_write(&(io_master_info),
				&setting);
		} else if (i2c_operation == CAM_SENSOR_I2C_WRITE_BURST ||
			i2c_operation == CAM_SENSOR_I2C_WRITE_SEQ) {
			rc = camera_io_dev_write_continuous(&io_master_info,
				&setting, i2c_operation);
		}

		if (rc < 0) {
			CAM_ERR(CAM_OIS,
				"Failed in Applying i2c wrt settings");
			break;
		}
	}

End:
	vfree(vaddr);
	vaddr = NULL;

	return rc;
}

static int cam_ois_fw_download_v2(struct cam_ois_ctrl_t *o_ctrl)
{
	int32_t                             rc = 0;
	struct cam_cmd_ois_fw_param        *fw_param = NULL;
	uint32_t                            fw_size;
	uint16_t                            len_per_write = 0;
	uint8_t                            *ptr = NULL;
	const struct firmware              *fw = NULL;
	struct device                      *dev = &(o_ctrl->pdev->dev);
	uint8_t                             count = 0;
	uint8_t                             cont_wr_flag = 0;

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	if (o_ctrl->i2c_fw_version_data.is_settings_valid == 1) {
		CAM_DBG(CAM_OIS, "check version to decide FW download");
		rc = cam_ois_apply_settings(o_ctrl, &o_ctrl->i2c_fw_version_data);
		if ((rc == -EAGAIN) &&
			(o_ctrl->io_master_info.master_type == CCI_MASTER)) {
			CAM_WARN(CAM_OIS,
			"CCI HW is resetting: Reapplying FW init settings");
			usleep_range(1000, 1010);
			rc = cam_ois_apply_settings(o_ctrl,
				&o_ctrl->i2c_fw_version_data);
		}

		if (delete_request(&o_ctrl->i2c_fw_version_data) < 0)
			CAM_WARN(CAM_OIS,
				"Fail deleting i2c_fw_version_data: rc: %d", rc);

		if (rc == I2C_COMPARE_MATCH) {
			CAM_INFO(CAM_OIS,
				"OIS FW version matched, skipping FW download");
			return rc;
		} else if (rc == I2C_COMPARE_MISMATCH) {
			CAM_INFO(CAM_OIS, "OIS FW version not matched, load FW");
		} else {
			CAM_WARN(CAM_OIS, "OIS FW version check failed,rc=%d", rc);
		}
	}

	for (count = 0; count < o_ctrl->fw_info.fw_count; count++) {
		fw_param      = &o_ctrl->fw_info.fw_param[count];
		fw_size       = fw_param->fw_size;
		len_per_write = fw_param->fw_len_per_write / fw_param->fw_data_type;

		CAM_DBG(CAM_OIS, "count: %d, fw_size: %d, data_type: %d, len_per_write: %d",
			count, fw_size, fw_param->fw_data_type, len_per_write);

		/* Load FW */
		rc = request_firmware(&fw, fw_param->fw_name, dev);
		if (rc) {
			CAM_ERR(CAM_OIS, "Failed to locate %s", fw_param->fw_name);
			return rc;
		}

		if (0 == rc && NULL != fw &&
			(fw_size <= fw->size - fw_param->fw_start_pos)) {

			/* fw init */
			CAM_DBG(CAM_OIS, "fw init");
			if (o_ctrl->i2c_fw_init_data[count].is_settings_valid == 1) {
				rc = cam_ois_apply_settings(o_ctrl,
					&o_ctrl->i2c_fw_init_data[count]);
				if ((rc == -EAGAIN) &&
					(o_ctrl->io_master_info.master_type == CCI_MASTER)) {
					CAM_WARN(CAM_OIS,
					"CCI HW is resetting: Reapplying FW init settings");
					usleep_range(1000, 1010);
					rc = cam_ois_apply_settings(o_ctrl,
						&o_ctrl->i2c_fw_init_data[count]);
				}
				if (rc) {
					CAM_ERR(CAM_OIS,
						"Cannot apply FW init settings %d",
						rc);
					goto release_firmware;
				} else {
					CAM_DBG(CAM_OIS, "OIS FW init settings success");
				}
			}

			/* send fw */
			CAM_DBG(CAM_OIS, "send fw, operation %d", fw_param->fw_operation);

			ptr = (uint8_t *)(fw->data + fw_param->fw_start_pos);
			if (fw_param->fw_operation == CAMERA_SENSOR_I2C_OP_RNDM_WR)
				cont_wr_flag = CAM_SENSOR_I2C_WRITE_RANDOM;
			else if (fw_param->fw_operation == CAMERA_SENSOR_I2C_OP_CONT_WR_BRST)
				cont_wr_flag = CAM_SENSOR_I2C_WRITE_BURST;
			else if (fw_param->fw_operation == CAMERA_SENSOR_I2C_OP_CONT_WR_SEQN)
				cont_wr_flag = CAM_SENSOR_I2C_WRITE_SEQ;

			write_ois_fw(ptr, (o_ctrl->fw_info.endianness & OIS_ENDIANNESS_MASK_FW),
					fw_param, o_ctrl->io_master_info, cont_wr_flag);

			/* fw finalize */
			CAM_DBG(CAM_OIS, "fw finalize");
			if (o_ctrl->i2c_fw_finalize_data[count].is_settings_valid == 1) {
				rc = cam_ois_apply_settings(o_ctrl,
					&o_ctrl->i2c_fw_finalize_data[count]);
				if ((rc == -EAGAIN) &&
					(o_ctrl->io_master_info.master_type == CCI_MASTER)) {
					CAM_WARN(CAM_OIS,
					"CCI HW is resetting: Reapplying FW finalize settings");
					usleep_range(1000, 1010);
					rc = cam_ois_apply_settings(o_ctrl,
						&o_ctrl->i2c_fw_finalize_data[count]);
				}
				if (rc) {
					CAM_ERR(CAM_OIS,
						"Cannot apply FW finalize settings %d",
						rc);
					goto release_firmware;
				} else {
					CAM_DBG(CAM_OIS, "OIS FW finalize settings success");
				}
			}
		}

		if (fw != NULL) {
			release_firmware(fw);
			fw = NULL;
		}
	}

release_firmware:
	if (fw != NULL) {
		release_firmware(fw);
		fw = NULL;
	}

	return rc;
}

/**
 * cam_ois_pkt_parse - Parse csl packet
 * @o_ctrl:     ctrl structure
 * @arg:        Camera control command argument
 *
 * Returns success or failure
 */
static int cam_ois_pkt_parse(struct cam_ois_ctrl_t *o_ctrl, void *arg)
{
	int32_t                         rc = 0;
	int32_t                         i = 0;
	uint32_t                        total_cmd_buf_in_bytes = 0;
	struct common_header           *cmm_hdr = NULL;
	uintptr_t                       generic_ptr;
	struct cam_control             *ioctl_ctrl = NULL;
	struct cam_config_dev_cmd       dev_config;
	struct i2c_settings_array      *i2c_reg_settings = NULL;
	struct cam_cmd_buf_desc        *cmd_desc = NULL;
	uintptr_t                       generic_pkt_addr;
	size_t                          pkt_len;
	size_t                          remain_len = 0;
	struct cam_packet              *csl_packet = NULL;
	size_t                          len_of_buff = 0;
	uint32_t                       *offset = NULL, *cmd_buf;
	struct cam_ois_soc_private     *soc_private =
		(struct cam_ois_soc_private *)o_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t  *power_info = &soc_private->power_info;
	const struct flash_ois_function *ps = pflash_ois;
	uint8_t                          config_flag = 0;
	int32_t                          j = 0;
	bool    parklens_power_down = true; //xiaomi add
	int32_t parklens_state      = 0;    //xiaomi add
	struct timespec64                ts1, ts2; // xiaomi add
	long                             microsec = 0; // xiaomi add
	struct i2c_data_settings        *i2c_data = NULL; // xiaomi add


	ioctl_ctrl = (struct cam_control *)arg;
	if (copy_from_user(&dev_config,
		u64_to_user_ptr(ioctl_ctrl->handle),
		sizeof(dev_config)))
		return -EFAULT;
	rc = cam_mem_get_cpu_buf(dev_config.packet_handle,
		&generic_pkt_addr, &pkt_len);
	if (rc) {
		CAM_ERR(CAM_OIS,
			"error in converting command Handle Error: %d", rc);
		return rc;
	}

	remain_len = pkt_len;
	if ((sizeof(struct cam_packet) > pkt_len) ||
		((size_t)dev_config.offset >= pkt_len -
		sizeof(struct cam_packet))) {
		CAM_ERR(CAM_OIS,
			"Inval cam_packet strut size: %zu, len_of_buff: %zu",
			 sizeof(struct cam_packet), pkt_len);
		cam_mem_put_cpu_buf(dev_config.packet_handle);
		return -EINVAL;
	}

	remain_len -= (size_t)dev_config.offset;
	csl_packet = (struct cam_packet *)
		(generic_pkt_addr + (uint32_t)dev_config.offset);

	if (cam_packet_util_validate_packet(csl_packet,
		remain_len)) {
		CAM_ERR(CAM_OIS, "Invalid packet params");
		cam_mem_put_cpu_buf(dev_config.packet_handle);
		return -EINVAL;
	}

	// xiaomi add begin
	CAM_DBG(CAM_OIS, "[CRM-OIS] op_code: %d, cmd buf: %d last_flush_req %llu, request_id %llu",
		(csl_packet->header.op_code & 0xFFFFFF),
		csl_packet->num_cmd_buf,
		o_ctrl->last_flush_req,
		csl_packet->header.request_id);

	if (((csl_packet->header.op_code & 0xFFFFFF) != CAM_OIS_PACKET_OPCODE_INIT) &&
		((csl_packet->header.op_code & 0xFFFFFF) != CAM_OIS_PACKET_OPCODE_READ) &&
		csl_packet->header.request_id <= o_ctrl->last_flush_req
		&& o_ctrl->last_flush_req != 0) {
		CAM_ERR(CAM_OIS,
			"[CRM-OIS] reject request %llu, last request to flush %llu opcode %d",
			csl_packet->header.request_id, o_ctrl->last_flush_req,
			(csl_packet->header.op_code & 0xFFFFFF));
		rc = -EBADR;
		cam_mem_put_cpu_buf(dev_config.packet_handle);
		return rc;
	}

	if (csl_packet->header.request_id > o_ctrl->last_flush_req){
		o_ctrl->last_flush_req = 0;
	}
	// xiaomi add end

	switch (csl_packet->header.op_code & 0xFFFFFF) {
	case CAM_OIS_PACKET_OPCODE_INIT:
		CAM_DBG(CAM_OIS, "CAM_OIS_PACKET_OPCODE_INIT,num_cmd_buf %d",
			csl_packet->num_cmd_buf);

		offset = (uint32_t *)&csl_packet->payload;
		offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);

		/* Loop through multiple command buffers */
		for (i = 0; i < csl_packet->num_cmd_buf; i++) {
			rc = cam_packet_util_validate_cmd_desc(&cmd_desc[i]);
			if (rc)
				return rc;

			total_cmd_buf_in_bytes = cmd_desc[i].length;
			if (!total_cmd_buf_in_bytes)
				continue;

			rc = cam_mem_get_cpu_buf(cmd_desc[i].mem_handle,
				&generic_ptr, &len_of_buff);
			if (rc < 0) {
				CAM_ERR(CAM_OIS, "Failed to get cpu buf : 0x%x",
					cmd_desc[i].mem_handle);
				cam_mem_put_cpu_buf(dev_config.packet_handle);
				return rc;
			}
			cmd_buf = (uint32_t *)generic_ptr;
			if (!cmd_buf) {
				CAM_ERR(CAM_OIS, "invalid cmd buf");
				cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
				cam_mem_put_cpu_buf(dev_config.packet_handle);
				return -EINVAL;
			}

			if ((len_of_buff < sizeof(struct common_header)) ||
				(cmd_desc[i].offset > (len_of_buff -
				sizeof(struct common_header)))) {
				CAM_ERR(CAM_OIS,
					"Invalid length for sensor cmd");
				cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
				cam_mem_put_cpu_buf(dev_config.packet_handle);
				return -EINVAL;
			}
			remain_len = len_of_buff - cmd_desc[i].offset;
			cmd_buf += cmd_desc[i].offset / sizeof(uint32_t);
			cmm_hdr = (struct common_header *)cmd_buf;

			CAM_DBG(CAM_OIS,
					"cmm_hdr->cmd_type: %d", cmm_hdr->cmd_type);
			switch (cmm_hdr->cmd_type) {
			case CAMERA_SENSOR_CMD_TYPE_I2C_INFO:
				rc = cam_ois_slaveInfo_pkt_parser(
					o_ctrl, cmd_buf, remain_len);
				if (rc < 0) {
					CAM_ERR(CAM_OIS,
					"Failed in parsing slave info");
					cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
					cam_mem_put_cpu_buf(dev_config.packet_handle);
					return rc;
				}
				break;
			case CAMERA_SENSOR_CMD_TYPE_PWR_UP:
			case CAMERA_SENSOR_CMD_TYPE_PWR_DOWN:
				CAM_DBG(CAM_OIS,
					"Received power settings buffer");
				// xiaomi modify begin
				if (PARKLENS_INVALID != parklens_atomic_read(&(o_ctrl->parklens_ctrl.parklens_state))) {
					ois_parklens_thread_stop(o_ctrl, EXIT_PARKLENS_WITHOUT_POWERDOWN);
					parklens_power_down = ois_parklens_power_down(o_ctrl);

					// Set the last few Settings, Ensure that the ois state is normal
					if(parklens_power_down == false){
						CAM_INFO(MI_PARKLENS, "[OISParklensLog] May be need reset OIS");
					}
					ois_deinit_parklens_info(o_ctrl);
					CAM_DBG(MI_PARKLENS, "[OISParklensLog] stop parklens thread, powerdown:%d",
						parklens_power_down);
				} else {
					CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens thread is invalid, powerdown:%d",
						parklens_power_down);
				}

				if(parklens_power_down == true) {
				 	CAM_DBG(MI_PARKLENS, "[OISParklensLog] need power up again");
					rc = cam_sensor_update_power_settings(
							cmd_buf,
							total_cmd_buf_in_bytes,
							power_info, remain_len);
					CAM_DBG(MI_PARKLENS, "[OISParklensLog] power up again successed");
					if (rc) {
						CAM_ERR(MI_PARKLENS, "[OISParklensLog] Failed:parse power settings: %d",
							rc);
						cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
						cam_mem_put_cpu_buf(dev_config.packet_handle);
						return rc;
					}
				} else {
					CAM_INFO(MI_PARKLENS, "[OISParklensLog] no need repower up again");
				}
				// xiaomi modify end
				break;
			case CAMERA_SENSOR_OIS_CMD_TYPE_FW_INFO:
				CAM_DBG(CAM_OIS,
					"Received fwInfo buffer,total_cmd_buf_in_bytes: %d",
					total_cmd_buf_in_bytes);
				rc = cam_ois_fw_info_pkt_parser(
					o_ctrl, cmd_buf, total_cmd_buf_in_bytes);
				if (rc) {
					CAM_ERR(CAM_OIS,
					"Failed: parse fw info settings");
					cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
					cam_mem_put_cpu_buf(dev_config.packet_handle);
					return rc;
				}
				break;
			default:
			if (o_ctrl->i2c_init_data.is_settings_valid == 0) {
				CAM_DBG(CAM_OIS,
				"Received init/config settings");
				i2c_reg_settings =
					&(o_ctrl->i2c_init_data);
				i2c_reg_settings->is_settings_valid = 1;
				i2c_reg_settings->request_id = 0;
				rc = cam_sensor_i2c_command_parser(
					&o_ctrl->io_master_info,
					i2c_reg_settings,
					&cmd_desc[i], 1, NULL);
				if (rc < 0) {
					CAM_ERR(CAM_OIS,
					"init parsing failed: %d", rc);
					cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
					cam_mem_put_cpu_buf(dev_config.packet_handle);
					return rc;
				}
			} else if ((o_ctrl->is_ois_calib != 0) &&
				(o_ctrl->i2c_calib_data.is_settings_valid ==
				0)) {
				CAM_DBG(CAM_OIS,
					"Received calib settings");
				i2c_reg_settings = &(o_ctrl->i2c_calib_data);
				i2c_reg_settings->is_settings_valid = 1;
				i2c_reg_settings->request_id = 0;
				rc = cam_sensor_i2c_command_parser(
					&o_ctrl->io_master_info,
					i2c_reg_settings,
					&cmd_desc[i], 1, NULL);
				if (rc < 0) {
					CAM_ERR(CAM_OIS,
						"Calib parsing failed: %d", rc);
					cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
					cam_mem_put_cpu_buf(dev_config.packet_handle);
					return rc;
				}
			} else if (o_ctrl->i2c_fwinit_data.is_settings_valid == 0) {
				CAM_DBG(CAM_OIS, "received fwinit settings");
				i2c_reg_settings =
					&(o_ctrl->i2c_fwinit_data);
				i2c_reg_settings->is_settings_valid = 1;
				i2c_reg_settings->request_id = 0;
				rc = cam_sensor_i2c_command_parser(
					&o_ctrl->io_master_info,
					i2c_reg_settings,
					&cmd_desc[i], 1, NULL);
				if (rc < 0) {
					CAM_DBG(CAM_OIS,
					"fw init parsing failed: %d", rc);
				}
			} else if (o_ctrl->i2c_postinit_data.is_settings_valid == 0) {
				CAM_DBG(CAM_OIS, "received postinit settings");
				i2c_reg_settings =
					&(o_ctrl->i2c_postinit_data);
				i2c_reg_settings->is_settings_valid = 1;
				i2c_reg_settings->request_id = 0;
				rc = cam_sensor_i2c_command_parser(
					&o_ctrl->io_master_info,
					i2c_reg_settings,
					&cmd_desc[i], 1, NULL);
				if (rc < 0) {
					CAM_ERR(CAM_OIS,
					"post init parsing failed: %d", rc);
					cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
					cam_mem_put_cpu_buf(dev_config.packet_handle);
					return rc;
				}
			}
			break;
			}
			cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
		}

		if (o_ctrl->cam_ois_state != CAM_OIS_CONFIG) {
			// xiaomi modify begin
			if (parklens_power_down == true) {
				CAM_DBG(MI_PARKLENS, "[OISParklensLog] OIS Power up start");
				rc = cam_ois_power_up(o_ctrl);
				if (rc) {
					CAM_ERR(MI_PARKLENS, "[OISParklensLog] OIS Power up failed");
					cam_mem_put_cpu_buf(dev_config.packet_handle);
					return rc;
				}
			}
			// xiaomi modify end
		}

		CAM_DBG(CAM_OIS, "ois_fw_flag: %d, customized_ois_flag %d",
				o_ctrl->ois_fw_flag, o_ctrl->opcode.customized_ois_flag);
		/* xiaomi add begin, interface for different ois add by xiaomi*/
		if (o_ctrl->opcode.customized_ois_flag) {
			for(j = 0; j < sizeof(pflash_ois) / sizeof(struct flash_ois_function) ; j++){
				if(ps[j].flag == o_ctrl->opcode.customized_ois_flag){
					config_flag++;
					if (true == cam_check_cci_is_error(&o_ctrl->cci_err_info)){
						CAM_ERR(CAM_OIS, "damage count:%d IO fail count:%d skip parklens!",
							cam_cci_read_damage_count(&o_ctrl->cci_err_info),
							cam_cci_read_io_fail_count(&o_ctrl->cci_err_info));
						rc = - ENODEV;
						goto pwr_dwn;
					}
					rc = ps[j].mi_ois_pkt_download(o_ctrl);
					if (rc) {
						CAM_ERR(CAM_OIS, "Failed OIS Customer Pkt Download");
						goto pwr_dwn;
					}
					o_ctrl->cam_ois_state = CAM_OIS_CONFIG;
					CAM_INFO(CAM_OIS, "%s use flash_ois_function %d",
							o_ctrl->ois_name, j);
					break;
				}
			}
			if (1 != config_flag) {
				CAM_ERR(CAM_OIS, "ERROR! need  pkt function, flag %d", config_flag);
			}
		/* xiaomi add end*/
		} else {
			if (o_ctrl->ois_fw_flag) {
				CAM_DBG(CAM_OIS, "fw_count: %d", o_ctrl->fw_info.fw_count);
				if (o_ctrl->fw_info.fw_count != 0) {
					/* xiaomi add begin */
					CAM_GET_TIMESTAMP(ts1);
					CAM_DBG(MI_PERF, "%s start firmware download", o_ctrl->ois_name);
					/* xiaomi add end */
					rc = cam_ois_fw_download_v2(o_ctrl);
					if (rc) {
						CAM_ERR(CAM_OIS, "Failed OIS FW Download v2");
						goto pwr_dwn;
					}
					/* xiaomi add begin */
					CAM_GET_TIMESTAMP(ts2);
					CAM_GET_TIMESTAMP_DIFF_IN_MICRO(ts1, ts2, microsec);
					CAM_DBG(MI_PERF, "%s end firmware download, occupy time is: %ld ms",
						o_ctrl->ois_name, microsec/1000);
					/* xiaomi add end */
				} else {
					if (o_ctrl->i2c_fwinit_data.is_settings_valid == 1) {
						rc = cam_ois_apply_settings(o_ctrl,
							&o_ctrl->i2c_fwinit_data);
						if ((rc == -EAGAIN) &&
							(o_ctrl->io_master_info.master_type ==
								CCI_MASTER)) {
							CAM_WARN(CAM_OIS,
								"Reapplying fwinit settings");
							usleep_range(1000, 1010);
							rc = cam_ois_apply_settings(o_ctrl,
								&o_ctrl->i2c_fwinit_data);
						}
						if (rc) {
							CAM_ERR(CAM_OIS,
								"Cannot apply fwinit data %d",
								rc);
							goto pwr_dwn;
						} else {
							CAM_DBG(CAM_OIS, "OIS fwinit settings success");
						}
						/* xiaomi add begin */
						CAM_GET_TIMESTAMP(ts1);
						CAM_DBG(MI_PERF, "%s start firmware download", o_ctrl->ois_name);
						/* xiaomi add end */
						rc = cam_ois_fw_download(o_ctrl);
						if (rc) {
							CAM_ERR(CAM_OIS, "Failed OIS FW Download");
							goto pwr_dwn;
						}
						/* xiaomi add begin */
						CAM_GET_TIMESTAMP(ts2);
						CAM_GET_TIMESTAMP_DIFF_IN_MICRO(ts1, ts2, microsec);
						CAM_DBG(MI_PERF, "%s end firmware download, occupy time is: %ld ms",
							o_ctrl->ois_name, microsec/1000);
						/* xiaomi add end */
					}
				}
			}

			rc = cam_ois_apply_settings(o_ctrl, &o_ctrl->i2c_init_data);
			if ((rc == -EAGAIN) &&
				(o_ctrl->io_master_info.master_type == CCI_MASTER)) {
				CAM_WARN(CAM_OIS,
					"CCI HW is restting: Reapplying INIT settings");
				usleep_range(1000, 1010);
				rc = cam_ois_apply_settings(o_ctrl,
					&o_ctrl->i2c_init_data);
			}

			if (rc < 0) {
				CAM_ERR(CAM_OIS,
					"Cannot apply Init settings: rc = %d",
					rc);
				goto pwr_dwn;
			} else {
				CAM_DBG(CAM_OIS, "apply Init settings success");
			}

			if (o_ctrl->is_ois_calib) {
				rc = cam_ois_apply_settings(o_ctrl,
					&o_ctrl->i2c_calib_data);
				if ((rc == -EAGAIN) &&
					(o_ctrl->io_master_info.master_type == CCI_MASTER)) {
					CAM_WARN(CAM_OIS,
						"CCI HW is restting: Reapplying calib settings");
					usleep_range(1000, 1010);
					rc = cam_ois_apply_settings(o_ctrl,
						&o_ctrl->i2c_calib_data);
				}
				if (rc) {
					CAM_ERR(CAM_OIS, "Cannot apply calib data");
					goto pwr_dwn;
				} else {
					CAM_DBG(CAM_OIS, "apply calib data settings success");
				}
			}

			o_ctrl->cam_ois_state = CAM_OIS_CONFIG;

			for (i = 0; i < MAX_OIS_FW_COUNT; i++) {
				if (o_ctrl->i2c_fw_init_data[i].is_settings_valid == 1) {
					rc = delete_request(&o_ctrl->i2c_fw_init_data[i]);
					if (rc < 0) {
						CAM_WARN(CAM_OIS,
							"Fail deleting i2c_fw_init_data: rc: %d", rc);
						rc = 0;
					}
				}
				if (o_ctrl->i2c_fw_finalize_data[i].is_settings_valid == 1) {
					rc = delete_request(&o_ctrl->i2c_fw_finalize_data[i]);
					if (rc < 0) {
						CAM_WARN(CAM_OIS,
							"Fail deleting i2c_fw_finalize_data: rc: %d", rc);
						rc = 0;
					}
				}
			}

			// xiaomi add begin
			if (o_ctrl->i2c_postinit_data.is_settings_valid == 1) {
				rc = cam_ois_apply_settings(o_ctrl,
					&o_ctrl->i2c_postinit_data);
				if ((rc == -EAGAIN) &&
					(o_ctrl->io_master_info.master_type == CCI_MASTER)) {
					CAM_WARN(CAM_OIS,
						"CCI HW is restting: Reapplying postinit settings");
					usleep_range(1000, 1010);
					rc = cam_ois_apply_settings(o_ctrl,
						&o_ctrl->i2c_postinit_data);
				}
				if (rc) {
					CAM_ERR(CAM_OIS,
						"Cannot apply postinit data %d",
						rc);
					goto pwr_dwn;
				} else {
					CAM_DBG(CAM_OIS, "OIS postinit settings success");
				}
			}

			rc = delete_request(&o_ctrl->i2c_postinit_data);
			if (rc < 0) {
				CAM_WARN(CAM_OIS,
					"Fail deleting postinit data: rc: %d", rc);
				rc = 0;
			}
			// xiaomi add end
		}
		rc = delete_request(&o_ctrl->i2c_fwinit_data);
		if (rc < 0) {
			CAM_WARN(CAM_OIS,
				"Fail deleting fwinit data: rc: %d", rc);
			rc = 0;
		}
		rc = delete_request(&o_ctrl->i2c_init_data);
		if (rc < 0) {
			CAM_WARN(CAM_OIS,
				"Fail deleting Init data: rc: %d", rc);
			rc = 0;
		}
		rc = delete_request(&o_ctrl->i2c_calib_data);
		if (rc < 0) {
			CAM_WARN(CAM_OIS,
				"Fail deleting Calibration data: rc: %d", rc);
			rc = 0;
		}

		cam_cci_clear_init_fail(&o_ctrl->cci_err_info);
		break;
	case CAM_OIS_PACKET_OPCODE_OIS_CONTROL:
		CAM_DBG(CAM_OIS, "CAM_OIS_PACKET_OPCODE_OIS_CONTROL");
		if (o_ctrl->cam_ois_state < CAM_OIS_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_OIS,
				"Not in right state to control OIS: %d",
				o_ctrl->cam_ois_state);
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}
		offset = (uint32_t *)&csl_packet->payload;
		offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		i2c_reg_settings = &(o_ctrl->i2c_mode_data);
		i2c_reg_settings->is_settings_valid = 1;
		i2c_reg_settings->request_id = 0;
		rc = cam_sensor_i2c_command_parser(&o_ctrl->io_master_info,
			i2c_reg_settings,
			cmd_desc, 1, NULL);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "OIS pkt parsing failed: %d", rc);
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		rc = cam_ois_apply_settings(o_ctrl, i2c_reg_settings);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "Cannot apply mode settings");
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		rc = delete_request(i2c_reg_settings);
		if (rc < 0) {
			CAM_ERR(CAM_OIS,
				"Fail deleting Mode data: rc: %d", rc);
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}
		break;
	case CAM_OIS_PACKET_OPCODE_READ: {
		uint64_t qtime_ns;
		struct cam_buf_io_cfg *io_cfg;
		struct i2c_settings_array i2c_read_settings;

		CAM_DBG(CAM_OIS, "CAM_OIS_PACKET_OPCODE_READ");

		if (o_ctrl->cam_ois_state < CAM_OIS_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_OIS,
				"Not in right state to read OIS: %d",
				o_ctrl->cam_ois_state);
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}
		CAM_DBG(CAM_OIS, "number of I/O configs: %d:",
			csl_packet->num_io_configs);
		if (csl_packet->num_io_configs == 0) {
			CAM_ERR(CAM_OIS, "No I/O configs to process");
			rc = -EINVAL;
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		INIT_LIST_HEAD(&(i2c_read_settings.list_head));

		io_cfg = (struct cam_buf_io_cfg *) ((uint8_t *)
			&csl_packet->payload +
			csl_packet->io_configs_offset);

		/* validate read data io config */
		if (io_cfg == NULL) {
			CAM_ERR(CAM_OIS, "I/O config is invalid(NULL)");
			rc = -EINVAL;
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		offset = (uint32_t *)&csl_packet->payload;
		offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		i2c_read_settings.is_settings_valid = 1;
		i2c_read_settings.request_id = 0;
		rc = cam_sensor_i2c_command_parser(&o_ctrl->io_master_info,
			&i2c_read_settings,
			cmd_desc, 1, &io_cfg[0]);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "OIS read pkt parsing failed: %d", rc);
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		rc = cam_sensor_util_get_current_qtimer_ns(&qtime_ns);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "failed to get qtimer rc:%d");
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		// xiaomi change begin
		/*rc = cam_sensor_i2c_read_data(
			&i2c_read_settings,
			&o_ctrl->io_master_info);*/
		if (false == cam_check_cci_is_error(&o_ctrl->cci_err_info)) {
			rc = cam_sensor_i2c_read_write_ois_data(
				&i2c_read_settings,
				&o_ctrl->io_master_info);
		}
		else {
			CAM_ERR(CAM_OIS, "damage count:%d IO fail count:%d skip apply read data!",
				cam_cci_read_damage_count(&o_ctrl->cci_err_info),
				cam_cci_read_io_fail_count(&o_ctrl->cci_err_info));
		}

		// xiaomi change end
		if (rc < 0) {
			cam_cci_add_io_fail_count(&o_ctrl->cci_err_info);
			CAM_ERR(CAM_OIS, "cannot read data rc: %d, IO fail count:%d",
				cam_cci_read_io_fail_count(&o_ctrl->cci_err_info));
			delete_request(&i2c_read_settings);
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		if (csl_packet->num_io_configs > 1) {
			rc = cam_sensor_util_write_qtimer_to_io_buffer(
				qtime_ns, &io_cfg[1]);
			if (rc < 0) {
				CAM_ERR(CAM_OIS,
					"write qtimer failed rc: %d", rc);
				delete_request(&i2c_read_settings);
				cam_mem_put_cpu_buf(dev_config.packet_handle);
				return rc;
			}
		}

		rc = delete_request(&i2c_read_settings);
		if (rc < 0 ||
				true == cam_check_cci_is_error(&o_ctrl->cci_err_info)) {
			CAM_ERR(CAM_OIS,
				"Failed in deleting the read settings");
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}
		break;
	}
	case CAM_OIS_PACKET_OPCODE_WRITE_TIME: {
		CAM_DBG(CAM_OIS,
				"CAM_OIS_PACKET_OPCODE_WRITE_TIME");
		if (o_ctrl->cam_ois_state < CAM_OIS_CONFIG) {
			rc = -EINVAL;
			CAM_ERR(CAM_OIS,
				"Not in right state to write time to OIS: %d",
				o_ctrl->cam_ois_state);
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}
		offset = (uint32_t *)&csl_packet->payload;
		offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		i2c_reg_settings = &(o_ctrl->i2c_time_data);
		i2c_reg_settings->is_settings_valid = 1;
		i2c_reg_settings->request_id = 0;
		rc = cam_sensor_i2c_command_parser(&o_ctrl->io_master_info,
			i2c_reg_settings,
			cmd_desc, 1, NULL);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "OIS pkt parsing failed: %d", rc);
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		if (o_ctrl->fw_info.fw_count > 0) {
			uint8_t ois_endianness =
				(o_ctrl->fw_info.endianness & OIS_ENDIANNESS_MASK_INPUTPARAM) >> 4;
			rc = cam_ois_update_time(i2c_reg_settings, ois_endianness);
		} else
			rc = cam_ois_update_time(i2c_reg_settings, CAM_ENDIANNESS_LITTLE);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "Cannot update time");
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		rc = cam_ois_apply_settings(o_ctrl, i2c_reg_settings);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "Cannot apply mode settings");
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		rc = delete_request(i2c_reg_settings);
		if (rc < 0) {
			CAM_ERR(CAM_OIS,
				"Fail deleting Mode data: rc: %d", rc);
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}
		break;
	}

	// xiaomi modify begin
	case CAM_OIS_PACKET_OPCODE_OIS_PARKLENS:{
		int32_t exit_result = parklens_atomic_read(&(o_ctrl->parklens_ctrl.exit_result));

		parklens_state = parklens_atomic_read(
							&(o_ctrl->parklens_ctrl.parklens_state));
		CAM_INFO(MI_PARKLENS, "[OISParklensLog] Received parklens buffer parklens_state %d",
			parklens_state);
		if ((parklens_state != PARKLENS_INVALID) ||
			(o_ctrl->cam_ois_state < CAM_OIS_CONFIG)) {
			rc = -EINVAL;
			CAM_WARN(MI_PARKLENS,
				"[OISParklensLog] Not in right state to do parklens: %d/%d, exit result %d release power control right",
				o_ctrl->cam_ois_state,
				parklens_state, exit_result);
			ois_parklens_thread_stop(o_ctrl, EXIT_PARKLENS_WITH_POWERDOWN);
			ois_deinit_parklens_info(o_ctrl);
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		i2c_reg_settings = &(o_ctrl->i2c_parklens_data);

		i2c_reg_settings->request_id = 0;
		i2c_reg_settings->is_settings_valid = 1;
		offset   = (uint32_t *)&csl_packet->payload;
		offset  += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);

		rc = cam_sensor_i2c_command_parser(
				&o_ctrl->io_master_info,
				i2c_reg_settings,
				cmd_desc, 1, NULL);

		if (rc < 0) {
			CAM_ERR(MI_PARKLENS, "[OISParklensLog] Failed:parse parklens settings: %d",
				rc);
			delete_request(i2c_reg_settings);
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		ois_parklens_thread_trigger(o_ctrl);
		break;
	}

	case CAM_OIS_PACKET_OPCODE_OIS_CHANGE_PWM:

		CAM_INFO(CAM_OIS, "[CRM-OIS] Received PWM mode packets");

		if (o_ctrl->cam_ois_state < CAM_OIS_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_OIS,
				"[CRM-OIS] Not in right state to control OIS: %d",
				o_ctrl->cam_ois_state);
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		CAM_DBG(CAM_OIS, "[CRM-OIS] request_id %llu, i2c_data %p &i2c_data->per_frame %p",
			csl_packet->header.request_id,
			&(o_ctrl->i2c_data),
			&i2c_data->per_frame);

		i2c_data =  &(o_ctrl->i2c_data);
		i2c_reg_settings = &i2c_data->per_frame[
			csl_packet->header.request_id % MAX_PER_FRAME_ARRAY];
		i2c_reg_settings->request_id =
			csl_packet->header.request_id;
		i2c_reg_settings->is_settings_valid = 1;
		offset = (uint32_t *)&csl_packet->payload;
		offset += csl_packet->cmd_buf_offset / sizeof(uint32_t);
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);
		rc = cam_sensor_i2c_command_parser(
			&o_ctrl->io_master_info,
			i2c_reg_settings,
			cmd_desc, 1, NULL);
		if (rc < 0) {
			CAM_ERR(CAM_OIS,
				"[CRM-OIS] CRM OIS parsing failed: %d", rc);
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}

		rc = cam_ois_update_req_mgr(o_ctrl, csl_packet);
		if (rc) {
			CAM_ERR(CAM_OIS,
				"[CRM-OIS] Failed in adding request to request manager");
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}
		break;

	case CAM_PKT_NOP_OPCODE:
		if (o_ctrl->cam_ois_state < CAM_OIS_CONFIG) {
			CAM_ERR(CAM_OIS,
				"[CRM-OIS] Received NOP packets in invalid state: %d",
				o_ctrl->cam_ois_state);
			rc = -EINVAL;
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}
		rc = cam_ois_update_req_mgr(o_ctrl, csl_packet);
		if (rc) {
			CAM_ERR(CAM_OIS,
				"[CRM-OIS] Failed in adding request to request manager");
			cam_mem_put_cpu_buf(dev_config.packet_handle);
			return rc;
		}
		break;

	// xiaomi modify end
	default:
		CAM_ERR(MI_PARKLENS, "[OISParklensLog] Invalid Opcode: %d",
			(csl_packet->header.op_code & 0xFFFFFF));
		cam_mem_put_cpu_buf(dev_config.packet_handle);
		return -EINVAL;
	}

	if (!rc) {
		cam_mem_put_cpu_buf(dev_config.packet_handle);
		return rc;
	}
pwr_dwn:
	cam_cci_set_init_fail(&o_ctrl->cci_err_info);
	cam_mem_put_cpu_buf(dev_config.packet_handle);
	cam_ois_power_down(o_ctrl);
	return rc;
}

void cam_ois_shutdown(struct cam_ois_ctrl_t *o_ctrl)
{
	int rc = 0, i = 0;
	struct cam_ois_soc_private *soc_private =
		(struct cam_ois_soc_private *)o_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t *power_info = &soc_private->power_info;

	/* xiaomi add begin */
	if (PARKLENS_INVALID !=
		parklens_atomic_read(&(o_ctrl->parklens_ctrl.parklens_state))) {
		ois_parklens_thread_stop(o_ctrl, EXIT_PARKLENS_WITH_POWERDOWN);
		CAM_INFO(MI_PARKLENS,
			"[OISParklensLog] exit parklens with powerdown in shutdown");
	} else {
		CAM_INFO(MI_PARKLENS,
			"[OISParklensLog] parklens is invalid in shutdown");
	}
	/* xiaomi add end */

	if (o_ctrl->cam_ois_state == CAM_OIS_INIT)
		return;
	/* xiaomi add begin */
	if (o_ctrl->cam_ois_state >= CAM_OIS_CONFIG) {
		if(false == ois_parklens_power_down(o_ctrl)) {
			rc = cam_ois_power_down(o_ctrl);
			if (rc < 0)
				CAM_ERR(CAM_OIS, "OIS Power down failed");
			CAM_INFO(MI_PARKLENS, "[OISParklensLog] parklens isn't powerdown");
		} else {
			CAM_INFO(MI_PARKLENS, "[OISParklensLog] parklens has been powerdown");
		}
	} else {
		CAM_INFO(MI_PARKLENS,
			"[OISParklensLog] shut down but not in CONFIG, parklens powerdown: %d",
			ois_parklens_power_down(o_ctrl));
	}
	/* xiaomi add end*/
	if (o_ctrl->cam_ois_state >= CAM_OIS_ACQUIRE) {
		rc = cam_destroy_device_hdl(o_ctrl->bridge_intf.device_hdl);
		if (rc < 0)
			CAM_ERR(CAM_OIS, "destroying the device hdl");
		o_ctrl->bridge_intf.device_hdl = -1;
		o_ctrl->bridge_intf.link_hdl = -1;
		o_ctrl->bridge_intf.session_hdl = -1;
	}

	if (o_ctrl->i2c_fwinit_data.is_settings_valid == 1)
		delete_request(&o_ctrl->i2c_fwinit_data);

	for (i = 0; i < MAX_OIS_FW_COUNT; i++) {
		if (o_ctrl->i2c_fw_init_data[i].is_settings_valid == 1) {
			rc = delete_request(&o_ctrl->i2c_fw_init_data[i]);
			if (rc < 0) {
				CAM_WARN(CAM_OIS,
					"Fail deleting i2c_fw_init_data: rc: %d", rc);
				rc = 0;
			}
		}
		if (o_ctrl->i2c_fw_finalize_data[i].is_settings_valid == 1) {
			rc = delete_request(&o_ctrl->i2c_fw_finalize_data[i]);
			if (rc < 0) {
				CAM_WARN(CAM_OIS,
					"Fail deleting i2c_fw_finalize_data: rc: %d", rc);
				rc = 0;
			}
		}
	}

	if (o_ctrl->i2c_fw_version_data.is_settings_valid == 1) {
		rc = delete_request(&o_ctrl->i2c_fw_version_data);
		if (rc < 0) {
			CAM_WARN(CAM_OIS,
				"Fail deleting i2c_fw_version_data: rc: %d", rc);
			rc = 0;
		}
	}

	if (o_ctrl->i2c_mode_data.is_settings_valid == 1)
		delete_request(&o_ctrl->i2c_mode_data);

	if (o_ctrl->i2c_calib_data.is_settings_valid == 1)
		delete_request(&o_ctrl->i2c_calib_data);

	if (o_ctrl->i2c_init_data.is_settings_valid == 1)
		delete_request(&o_ctrl->i2c_init_data);

	// xiaomi add begin
	if (o_ctrl->i2c_postinit_data.is_settings_valid == 1)
		delete_request(&o_ctrl->i2c_postinit_data);

	if (PARKLENS_INVALID != parklens_atomic_read(&(o_ctrl->parklens_ctrl.parklens_state))) {
		ois_deinit_parklens_info(o_ctrl);
		CAM_INFO(MI_PARKLENS,
			"[OISParklensLog] parklens is not valid, deinit parklens info");
	}
	// xiaomi add end

	kfree(power_info->power_setting);
	kfree(power_info->power_down_setting);
	power_info->power_setting = NULL;
	power_info->power_down_setting = NULL;
	power_info->power_down_setting_size = 0;
	power_info->power_setting_size = 0;
	o_ctrl->last_flush_req = 0; // xiaomi add

	o_ctrl->cam_ois_state = CAM_OIS_INIT;
}

/**
 * cam_ois_driver_cmd - Handle ois cmds
 * @e_ctrl:     ctrl structure
 * @arg:        Camera control command argument
 *
 * Returns success or failure
 */
int cam_ois_driver_cmd(struct cam_ois_ctrl_t *o_ctrl, void *arg)
{
	int                              rc = 0, i = 0;
	struct cam_ois_query_cap_t       ois_cap = {0};
	struct cam_control              *cmd = (struct cam_control *)arg;
	struct cam_ois_soc_private      *soc_private = NULL;
	struct cam_sensor_power_ctrl_t  *power_info = NULL;
	bool                            parklens_power_down = true;   //xiaomi add

	if (!o_ctrl || !cmd) {
		CAM_ERR(CAM_OIS, "Invalid arguments");
		return -EINVAL;
	}

	if (cmd->handle_type != CAM_HANDLE_USER_POINTER) {
		CAM_ERR(CAM_OIS, "Invalid handle type: %d",
			cmd->handle_type);
		return -EINVAL;
	}

	soc_private =
		(struct cam_ois_soc_private *)o_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;

	mutex_lock(&(o_ctrl->ois_mutex));
	switch (cmd->op_code) {
	case CAM_QUERY_CAP:
		ois_cap.slot_info = o_ctrl->soc_info.index;

		if (copy_to_user(u64_to_user_ptr(cmd->handle),
			&ois_cap,
			sizeof(struct cam_ois_query_cap_t))) {
			CAM_ERR(CAM_OIS, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}
		CAM_DBG(CAM_OIS, "ois_cap: ID: %d", ois_cap.slot_info);
		break;
	case CAM_ACQUIRE_DEV:
		rc = cam_ois_get_dev_handle(o_ctrl, arg);
		if (rc) {
			CAM_ERR(CAM_OIS, "Failed to acquire dev");
			goto release_mutex;
		}

		cam_cci_clear_init_fail(&o_ctrl->cci_err_info);
		cam_cci_reset_io_fail_count(&o_ctrl->cci_err_info);
		o_ctrl->cam_ois_state = CAM_OIS_ACQUIRE;
		break;
	case CAM_START_DEV:
		if (o_ctrl->cam_ois_state != CAM_OIS_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_OIS,
			"Not in right state for start : %d",
			o_ctrl->cam_ois_state);
			goto release_mutex;
		}
		o_ctrl->cam_ois_state = CAM_OIS_START;
		o_ctrl->last_flush_req = 0;  // xiaomi add
		break;
	case CAM_CONFIG_DEV:
		rc = cam_ois_pkt_parse(o_ctrl, arg);
		if (rc) {
			CAM_ERR(CAM_OIS, "Failed in ois pkt Parsing");
			goto release_mutex;
		}
		break;
	case CAM_RELEASE_DEV:{
		struct i2c_settings_array *i2c_set = NULL; // xiaomi add

		if (o_ctrl->cam_ois_state == CAM_OIS_START) {
			rc = -EINVAL;
			CAM_WARN(CAM_OIS,
				"Cant release ois: in start state");
			goto release_mutex;
		}

		//xiaomi modify begin
		parklens_power_down = ois_parklens_power_down(o_ctrl);
		CAM_INFO(MI_PARKLENS, "[OISParklensLog] CAM_RELEASE_DEV is_parklens_power_down status %d/%d",
			parklens_power_down,
			o_ctrl->cam_ois_state);
		if (o_ctrl->cam_ois_state == CAM_OIS_CONFIG) {
			if(parklens_power_down == false){
				CAM_DBG(CAM_OIS, "cam_ois_power_down");
				rc = cam_ois_power_down(o_ctrl);
				if (rc < 0) {
					CAM_ERR(CAM_OIS, "OIS Power down failed");
					goto release_mutex;
				}
			}
		}
		//xiaomi modify end

		if (o_ctrl->bridge_intf.device_hdl == -1) {
			CAM_ERR(CAM_OIS, "link hdl: %d device hdl: %d",
				o_ctrl->bridge_intf.device_hdl,
				o_ctrl->bridge_intf.link_hdl);
			rc = -EINVAL;
			goto release_mutex;
		}
		rc = cam_destroy_device_hdl(o_ctrl->bridge_intf.device_hdl);
		if (rc < 0)
			CAM_ERR(CAM_OIS, "destroying the device hdl");
		o_ctrl->bridge_intf.device_hdl = -1;
		o_ctrl->bridge_intf.link_hdl = -1;
		o_ctrl->bridge_intf.session_hdl = -1;
		o_ctrl->cam_ois_state = CAM_OIS_INIT;
		o_ctrl->last_flush_req = 0; // xiaomi add

		//xiaomi modify begin
		if (parklens_power_down == false) {
			kfree(power_info->power_setting);
			kfree(power_info->power_down_setting);
			power_info->power_setting = NULL;
			power_info->power_down_setting = NULL;
			power_info->power_down_setting_size = 0;
			power_info->power_setting_size = 0;
		}
		//xiaomi modify end

		if (o_ctrl->i2c_mode_data.is_settings_valid == 1)
			delete_request(&o_ctrl->i2c_mode_data);

		if (o_ctrl->i2c_calib_data.is_settings_valid == 1)
			delete_request(&o_ctrl->i2c_calib_data);

		if (o_ctrl->i2c_init_data.is_settings_valid == 1)
			delete_request(&o_ctrl->i2c_init_data);

		if (o_ctrl->i2c_fwinit_data.is_settings_valid == 1)
			delete_request(&o_ctrl->i2c_fwinit_data);

		// xiaomi add begin
		if (o_ctrl->i2c_postinit_data.is_settings_valid == 1)
			delete_request(&o_ctrl->i2c_postinit_data);
		// xiaomi add end

		for (i = 0; i < MAX_OIS_FW_COUNT; i++) {
			if (o_ctrl->i2c_fw_init_data[i].is_settings_valid == 1) {
				rc = delete_request(&o_ctrl->i2c_fw_init_data[i]);
				if (rc < 0) {
					CAM_WARN(CAM_OIS,
						"Fail deleting i2c_fw_init_data: rc: %d", rc);
					rc = 0;
				}
			}
			if (o_ctrl->i2c_fw_finalize_data[i].is_settings_valid == 1) {
				rc = delete_request(&o_ctrl->i2c_fw_finalize_data[i]);
				if (rc < 0) {
					CAM_WARN(CAM_OIS,
						"Fail deleting i2c_fw_finalize_data: rc: %d", rc);
					rc = 0;
				}
			}
		}

		if (true == o_ctrl->opcode.fw_mem_store)
		{
			cam_fw_mem_store(o_ctrl);
		}
		// xiaomi add begin
		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			i2c_set = &(o_ctrl->i2c_data.per_frame[i]);
			if (i2c_set->is_settings_valid == 1) {
				rc = delete_request(i2c_set);
				if (rc < 0)
					CAM_ERR(CAM_SENSOR,
						"[CRM-OIS] delete request: %lld rc: %d",
						i2c_set->request_id, rc);
			}
		}

		if (true == cam_check_cci_is_damage(&o_ctrl->cci_err_info))
			CAM_ERR(CAM_OIS, "dev may damaged!, damage count:%d",
				cam_cci_read_damage_count(&o_ctrl->cci_err_info));
		// xiaomi add end
		break;
	}
	case CAM_STOP_DEV:
		if (o_ctrl->cam_ois_state != CAM_OIS_START) {
			rc = -EINVAL;
			CAM_WARN(CAM_OIS,
				"Not in right state for stop : %d",
				o_ctrl->cam_ois_state);
			goto release_mutex;
		}
		o_ctrl->last_flush_req = 0; // xiaomi add
		o_ctrl->cam_ois_state = CAM_OIS_CONFIG;

		break;
	case CAM_FLUSH_REQ:
		CAM_DBG(CAM_OIS, "Flush recveived");
		break;
	default:
		CAM_ERR(CAM_OIS, "invalid opcode: %d", cmd->op_code);
		goto release_mutex;
	}
release_mutex:
	mutex_unlock(&(o_ctrl->ois_mutex));
	return rc;
}

// xiaomi add begin
int32_t init_ois_parklens_info(struct cam_ois_ctrl_t *o_ctrl)
{
	int32_t rc = 0;
	struct cam_ois_parklens_ctrl_t *parklens_ctrl;

	if(!o_ctrl) {
		CAM_ERR(MI_PARKLENS, "[OISParklensLog] failed: o_ctrl %pK", o_ctrl);
		return -EINVAL;
	}

	parklens_ctrl = &(o_ctrl->parklens_ctrl);

	CAM_DBG(MI_PARKLENS, "[OISParklensLog] init parklens: %s %s", o_ctrl->device_name,o_ctrl->ois_name);

	parklens_atomic_set(&(parklens_ctrl->parklens_opcode),
		ENTER_PARKLENS_WITH_POWERDOWN);
	parklens_atomic_set(&(parklens_ctrl->exit_result),
		PARKLENS_ENTER);
	parklens_atomic_set(&(parklens_ctrl->parklens_state),
		PARKLENS_INVALID);

	rc = parklens_event_create(&(parklens_ctrl->start_event));
	if (rc < 0) {
		CAM_ERR(MI_PARKLENS,
			"[OISParklensLog] failed to create start event for parklens");
		return rc;
	}


	rc = parklens_event_create(&(parklens_ctrl->shutdown_event));
	if (rc < 0) {
		CAM_ERR(MI_PARKLENS,
			"[OISParklensLog] failed to create shutdown event for parklens");
		return rc;
	}

	parklens_ctrl->parklens_thread = NULL;
	parklens_atomic_set(&(parklens_ctrl->parklens_state), PARKLENS_STOP);

	return 0;
}

int32_t ois_deinit_parklens_info(struct cam_ois_ctrl_t *o_ctrl)
{
	int32_t rc = 0;
	struct cam_ois_parklens_ctrl_t *parklens_ctrl;

	if(!o_ctrl) {
		CAM_ERR(MI_PARKLENS, "[OISParklensLog] failed: o_ctrl %pK", o_ctrl);
		return -EINVAL;
	}

	CAM_DBG(MI_PARKLENS, "[OISParklensLog] deinit parklens: %s", o_ctrl->device_name);

	parklens_ctrl = &(o_ctrl->parklens_ctrl);

	if (PARKLENS_STOP !=
		parklens_atomic_read(&(parklens_ctrl->parklens_state))) {
		CAM_ERR(MI_PARKLENS, "[OISParklensLog] deinit parklens in wrong state");
		return -EINVAL;
	}

	rc = parklens_event_destroy(&(parklens_ctrl->start_event));
	if (rc < 0)
		CAM_ERR(MI_PARKLENS,
			"[OISParklensLog] failed to destroy start event for parklens");

	rc = parklens_event_destroy(&(parklens_ctrl->shutdown_event));
	if (rc < 0)
		CAM_ERR(MI_PARKLENS,
			"[OISParklensLog] failed to destroy shutdown event for parklens");

	parklens_atomic_set(&(parklens_ctrl->parklens_opcode),
		ENTER_PARKLENS_WITH_POWERDOWN);
	parklens_atomic_set(&(parklens_ctrl->exit_result),
		PARKLENS_ENTER);
	parklens_atomic_set(&(parklens_ctrl->parklens_state),
		PARKLENS_INVALID);

	return 0;
}

bool ois_parklens_power_down(struct cam_ois_ctrl_t *o_ctrl)
{
	bool    is_power_down = false;
	int32_t parklens_state = 0;
	struct  cam_ois_parklens_ctrl_t *parklens_ctrl;

	if(!o_ctrl) {
		CAM_ERR(MI_PARKLENS, "[OISParklensLog] failed: o_ctrl %pK", o_ctrl);
		return false;
	}

	parklens_ctrl = &(o_ctrl->parklens_ctrl);
	parklens_state = parklens_atomic_read(&(parklens_ctrl->parklens_state));

	switch (parklens_state) {
	case PARKLENS_RUNNING: {
		CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens is running, no need powerdown");
		is_power_down = true;
	}
		break;

	case PARKLENS_STOP: {
		if (parklens_atomic_read(&(parklens_ctrl->exit_result)) <= PARKLENS_EXIT_WITHOUT_POWERDOWN) {
			CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens stop without power down");
			is_power_down = false;
		} else {
			is_power_down = true;
		}
	}
		break;

	case PARKLENS_INVALID: {
		CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens is not created, power down");
		is_power_down = false;
	}
		break;

	default:
		CAM_ERR(MI_PARKLENS, "[OISParklensLog] Invalid parklens_state %d", parklens_state);
	}

	return is_power_down;
}

int32_t ois_parklens_thread_trigger(struct cam_ois_ctrl_t *o_ctrl)
{
	int32_t rc = 0;
	struct cam_ois_parklens_ctrl_t *parklens_ctrl;

	if(!o_ctrl) {
		CAM_ERR(MI_PARKLENS, "[OISParklensLog] failed: o_ctrl %pK", o_ctrl);
		return -EINVAL;;
	}

	rc = init_ois_parklens_info(o_ctrl);
	if(rc < 0){
		CAM_ERR(MI_PARKLENS, "[OISParklensLog] failed to init parklens info %d", rc);
		return rc;
	}
	parklens_ctrl = &(o_ctrl->parklens_ctrl);

	/* intialize parameters of parklens*/
	parklens_atomic_set(&(parklens_ctrl->parklens_opcode),
		ENTER_PARKLENS_WITH_POWERDOWN);
	parklens_atomic_set(&(parklens_ctrl->exit_result),
		PARKLENS_ENTER);
	parklens_atomic_set(&(parklens_ctrl->parklens_state),
		PARKLENS_STOP);

	parklens_ctrl->parklens_thread =
					parklens_thread_run(ois_parklens_thread_func,
										o_ctrl,
										"parklens-thread");

	if (!(parklens_ctrl->parklens_thread)) {
		CAM_ERR(MI_PARKLENS, "[OISParklensLog] parklens_thread create failed");
		rc = -ENOMEM;
		goto deinit_parklens;
	} else {
		CAM_DBG(MI_PARKLENS, "[OISParklensLog] %s trigger parklens thread",o_ctrl->ois_name);
		parklens_wait_single_event(&(parklens_ctrl->start_event),0);

		rc = parklens_atomic_read(&(parklens_ctrl->exit_result));
		if (rc > PARKLENS_ENTER) {
			CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens thread execute failed %d", rc);
			goto clear_thread;
		} else
			parklens_atomic_set(&(parklens_ctrl->parklens_state),
								PARKLENS_RUNNING);
	}

	return rc;

clear_thread:
	parklens_wait_single_event(&(parklens_ctrl->shutdown_event),0);
	rc = parklens_atomic_read(&(parklens_ctrl->exit_result));
	CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens thread exit: %d", rc);

	parklens_atomic_set(&(parklens_ctrl->parklens_state),PARKLENS_STOP);
	parklens_ctrl->parklens_thread = NULL;
	return rc;

deinit_parklens:
	ois_deinit_parklens_info(o_ctrl);
	return rc;

}


int32_t ois_parklens_thread_stop(
	struct cam_ois_ctrl_t *o_ctrl,
	enum parklens_opcodes opcode)
{
	int32_t exit_result = 0;
	int32_t parklens_state = 0;
	struct cam_ois_parklens_ctrl_t *parklens_ctrl;

	if(!o_ctrl) {
		CAM_ERR(MI_PARKLENS, "[OISParklensLog] failed: o_ctrl %pK", o_ctrl);
		return -EINVAL;
	}

	parklens_ctrl = &(o_ctrl->parklens_ctrl);

	parklens_state = parklens_atomic_read(&(parklens_ctrl->parklens_state)) ;
	exit_result = parklens_atomic_read(&(parklens_ctrl->exit_result));

	if (parklens_state != PARKLENS_RUNNING) {
		CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens thread is in other state: %d", parklens_state);
		return -EINVAL;
	} else {
		if(exit_result != PARKLENS_ENTER) {
			CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens thread is already end: %d", exit_result);
		} else {
			if ((opcode == EXIT_PARKLENS_WITH_POWERDOWN) ||
				(opcode == EXIT_PARKLENS_WITHOUT_POWERDOWN)) {
				parklens_atomic_set(&(parklens_ctrl->parklens_opcode), opcode);
			} else {
				CAM_DBG(MI_PARKLENS, "[OISParklensLog] Invalid stop opcode, stop parklens with power down");
				parklens_atomic_set(&(parklens_ctrl->parklens_opcode),
									EXIT_PARKLENS_WITH_POWERDOWN);
			}
			CAM_DBG(MI_PARKLENS, "[OISParklensLog] wake up parklens thread to stop it");
		}

		parklens_wait_single_event(&(parklens_ctrl->shutdown_event), 0);

		parklens_atomic_set(&(parklens_ctrl->parklens_state), PARKLENS_STOP);
		parklens_ctrl->parklens_thread = NULL;

		exit_result = parklens_atomic_read(&(parklens_ctrl->exit_result));
		if(exit_result) {
			CAM_INFO(MI_PARKLENS, "[OISParklensLog] parklens thread exit status: %d", exit_result);
		} else
			CAM_DBG(MI_PARKLENS, "[OISParklensLog] parklens thread exit failed ! ! !");
	}

	return exit_result;
}

void ois_fw_version_set(uint64_t fw_version, char* name)
{
	bool rc = false;
	int i;

	for (i = 0; i < MAX_OIS_DEV; i++)
	{
		if (false == ois_firmware[i].is_valid ||
			0 == strcmp(ois_firmware[i].name, name))
		{
			strcpy(ois_firmware[i].name, name);
			ois_firmware[i].fw_version = fw_version;
			ois_firmware[i].is_valid = true;
			rc = true;
			break;
		}
	}

	CAM_INFO(MI_DEBUG, "index:%d cach version:%s, name:%s version:0x%x",
		i, (rc == true)?"Y":"N", name, fw_version);
}

static int cam_ois_fw_set(const char *kmessage,
	const struct kernel_param *kp)
{
	return 0;
}

static int cam_ois_fw_get(char *buffer,
	const struct kernel_param *kp)
{
	uint16_t buff_max_size = CAM_OIS_FW_VERSION_MAX_LENGTH;
	uint16_t ret = 0, i;

	for (i=0; (ois_firmware[i].is_valid == true && i < MAX_OIS_DEV); i++)
	{
		ret += scnprintf(buffer+ret, buff_max_size, "%s : 0x%x\n",
					ois_firmware[i].name,
					ois_firmware[i].fw_version);
	}
	return ret;
}

static const struct kernel_param_ops cam_ois_fw_ops = {
	.set = cam_ois_fw_set,
	.get = cam_ois_fw_get
};

module_param_cb(cam_ois_fw, &cam_ois_fw_ops, NULL, 0644);

/**
 * cam_ois_publish_dev_info - publish device information
 * @info:        camera req manager device information
 *
 * Returns success or failure
 */
int cam_ois_publish_dev_info(struct cam_req_mgr_device_info *info)
{
	if (!info) {
		CAM_ERR(CAM_OIS, "[CRM-OIS] Invalid Args");
		return -EINVAL;
	}

	info->dev_id = CAM_REQ_MGR_DEVICE_OIS;
	strlcpy(info->name, CAM_OIS_NAME, sizeof(info->name));
	info->p_delay = 1;
	info->trigger = CAM_TRIGGER_POINT_SOF;

	return 0;
}

/**
 * cam_ois_establish_link - camera ois establish link
 * @link:        camera request manager core device link setup
 *
 * Returns success or failure
 */
int32_t cam_ois_establish_link(
	struct cam_req_mgr_core_dev_link_setup *link)
{
	struct cam_ois_ctrl_t *o_ctrl = NULL;

	if (!link) {
		CAM_ERR(CAM_OIS, "[CRM-OIS] Invalid Args");
		return -EINVAL;
	}

	o_ctrl = (struct cam_ois_ctrl_t *)
		cam_get_device_priv(link->dev_hdl);
	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "[CRM-OIS] Device data is NULL");
		return -EINVAL;
	}

	mutex_lock(&(o_ctrl->ois_mutex));
	if (link->link_enable) {
		o_ctrl->bridge_intf.link_hdl = link->link_hdl;
		o_ctrl->bridge_intf.crm_cb = link->crm_cb;
	} else {
		o_ctrl->bridge_intf.link_hdl = -1;
		o_ctrl->bridge_intf.crm_cb = NULL;
	}
	mutex_unlock(&(o_ctrl->ois_mutex));

	return 0;
}

/**
 * cam_ois_update_req_mgr - camera ois update reg manager
 * @o_ctrl:        camera ois controller
 * @csl_packet:    camera packet
 *
 * Returns success or failure
 */
int cam_ois_update_req_mgr(
	struct cam_ois_ctrl_t *o_ctrl,
	struct cam_packet *csl_packet)
{
	int rc = 0;
	struct cam_req_mgr_add_request add_req;

	memset(&add_req, 0, sizeof(add_req));
	add_req.link_hdl = o_ctrl->bridge_intf.link_hdl;
	add_req.req_id = csl_packet->header.request_id;
	add_req.dev_hdl = o_ctrl->bridge_intf.device_hdl;
	if ((csl_packet->header.op_code & 0xFFFFFF) ==
		CAM_OIS_PACKET_OPCODE_OIS_CHANGE_PWM) {
		add_req.trigger_eof = true;
		add_req.skip_at_sof = 0;
		add_req.skip_at_eof = 0;
	}
	CAM_DBG(CAM_OIS, "[CRM-OIS] Adding request: %llu link_hdl %d, dev_hdl %d",
					csl_packet->header.request_id, add_req.link_hdl, add_req.dev_hdl);
	if (o_ctrl->bridge_intf.crm_cb &&
		o_ctrl->bridge_intf.crm_cb->add_req) {
		rc = o_ctrl->bridge_intf.crm_cb->add_req(&add_req);
		if (rc) {
			if (rc == -EBADR)
				CAM_INFO(CAM_OIS,
					"[CRM-OIS] Adding request: %llu failed: rc: %d, it has been flushed",
					csl_packet->header.request_id, rc);
			else
				CAM_ERR(CAM_OIS,
					"[CRM-OIS] Adding request: %llu failed: rc: %d",
					csl_packet->header.request_id, rc);
			return rc;
		}
		CAM_DBG(CAM_OIS, "[CRM-OIS] Request Id: %lld added to CRM",
			add_req.req_id);
	} else {
		CAM_ERR(CAM_OIS, "[CRM-OIS] Can't add Request ID: %lld to CRM",
			csl_packet->header.request_id);
		rc = -EINVAL;
	}

	return rc;
}

int32_t cam_ois_flush_request(struct cam_req_mgr_flush_request *flush_req)
{
	int32_t rc = 0, i;
	uint32_t cancel_req_id_found = 0;
	struct cam_ois_ctrl_t *o_ctrl = NULL;
	struct i2c_settings_array *i2c_set = NULL;

	if (!flush_req)
		return -EINVAL;

	o_ctrl = (struct cam_ois_ctrl_t *)
		cam_get_device_priv(flush_req->dev_hdl);
	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "[CRM-OIS] Device data is NULL");
		return -EINVAL;
	}

	if (o_ctrl->i2c_data.per_frame == NULL) {
		CAM_ERR(CAM_OIS, "[CRM-OIS] I2C frame data is NULL");
		return -EINVAL;
	}

	mutex_lock(&(o_ctrl->ois_mutex));
	if (flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_ALL) {
		o_ctrl->last_flush_req = flush_req->req_id;
		CAM_DBG(CAM_OIS, "[CRM-OIS] Last reqest to flush is %lld",
			flush_req->req_id);
	}

	for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
		i2c_set = &(o_ctrl->i2c_data.per_frame[i]);

		if ((flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ)
				&& (i2c_set->request_id != flush_req->req_id))
			continue;

		if (i2c_set->is_settings_valid == 1) {
			rc = delete_request(i2c_set);
			if (rc < 0)
				CAM_ERR(CAM_OIS,
					"[CRM-OIS] Delete request: %lld rc: %d",
					i2c_set->request_id, rc);

			if (flush_req->type ==
				CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ) {
				cancel_req_id_found = 1;
				break;
			}
		}
	}

	if (flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ &&
		!cancel_req_id_found)
		CAM_ERR(CAM_OIS,
			"[CRM-OIS] Flush request id:%lld not found in the pending list",
			flush_req->req_id);

	mutex_unlock(&(o_ctrl->ois_mutex));

	return rc;
}

/**
 * cam_ois_apply_request - apply ois request
 * @apply:        camera request manager apply request
 *
 * Returns success or failure
 */
int32_t cam_ois_apply_request(struct cam_req_mgr_apply_request *apply)
{
	int32_t rc = 0, request_id, del_req_id;
	struct cam_ois_ctrl_t *o_ctrl = NULL;

	if (!apply) {
		CAM_ERR(CAM_OIS, "[CRM-OIS] Invalid Input Args");
		return -EINVAL;
	}

	CAM_DBG(CAM_OIS, "[CRM-OIS] OIS apply request:%llu", apply->request_id);
	o_ctrl = (struct cam_ois_ctrl_t *)
		cam_get_device_priv(apply->dev_hdl);

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "[CRM-OIS] Device data is NULL");
		return -EINVAL;
	}

	request_id = apply->request_id % MAX_PER_FRAME_ARRAY;

	trace_cam_apply_req("OIS", o_ctrl->soc_info.index, apply->request_id, apply->link_hdl);

	mutex_lock(&(o_ctrl->ois_mutex));
	if ((apply->request_id ==
		o_ctrl->i2c_data.per_frame[request_id].request_id) &&
		(o_ctrl->i2c_data.per_frame[request_id].is_settings_valid)
		== 1) {
		rc = cam_ois_apply_settings(o_ctrl,
			&o_ctrl->i2c_data.per_frame[request_id]);
		if (rc < 0) {
			CAM_ERR(CAM_OIS,
				"[CRM-OIS] Failed in applying the request: %llu\n",
				apply->request_id);
			goto release_mutex;
		}
	}
	del_req_id = (request_id +
		MAX_PER_FRAME_ARRAY - MAX_SYSTEM_PIPELINE_DELAY) %
		MAX_PER_FRAME_ARRAY;

	if (apply->request_id >
		o_ctrl->i2c_data.per_frame[del_req_id].request_id) {
		o_ctrl->i2c_data.per_frame[del_req_id].request_id = 0;
		rc = delete_request(&o_ctrl->i2c_data.per_frame[del_req_id]);
		if (rc < 0) {
			CAM_ERR(CAM_OIS,
				"[CRM-OIS] Fail deleting the req: %d err: %d\n",
				del_req_id, rc);
			goto release_mutex;
		}
	} else {
		CAM_INFO(CAM_OIS, "[CRM-OIS] No Valid Req to clean Up");
	}

release_mutex:
	mutex_unlock(&(o_ctrl->ois_mutex));
	return rc;
}
// xiaomi add end
