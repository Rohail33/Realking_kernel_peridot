#include <linux/module.h>
#include <cam_sensor_cmn_header.h>
#include "cam_aperture_core.h"
#include "cam_sensor_util.h"
#include "cam_trace.h"
#include "cam_common_util.h"
#include "cam_packet_util.h"

#define MAX_RETRY_TIMES 3  //xiaomi add

int32_t cam_aperture_construct_default_power_setting(
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

static int32_t cam_aperture_power_up(struct cam_aperture_ctrl_t *a_ctrl)
{
	int rc = 0;
	struct cam_hw_soc_info                 *soc_info = &a_ctrl->soc_info;
	struct cam_aperture_soc_private        *soc_private;
	struct cam_sensor_power_ctrl_t         *power_info;
	struct completion                      *i3c_probe_completion = NULL;
	struct timespec64                       ts1, ts2;
	long                                    microsec = 0;

	CAM_GET_TIMESTAMP(ts1);
	CAM_DBG(CAM_APERTURE, "%s start power_up", a_ctrl->device_name);

	soc_private =
		(struct cam_aperture_soc_private *)a_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;

	if ((power_info->power_setting == NULL) &&
		(power_info->power_down_setting == NULL)) {
		CAM_INFO(CAM_APERTURE,
			"Using default power settings");
		rc = cam_aperture_construct_default_power_setting(power_info);
		if (rc < 0) {
			CAM_ERR(CAM_APERTURE,
				"Construct default aperture power setting failed.");
			return rc;
		}
	}

	/* Parse and fill vreg params for power up settings */
	rc = msm_camera_fill_vreg_params(
		&a_ctrl->soc_info,
		power_info->power_setting,
		power_info->power_setting_size);
	if (rc) {
		CAM_ERR(CAM_APERTURE,
			"failed to fill vreg params for power up rc:%d", rc);
		return rc;
	}

	/* Parse and fill vreg params for power down settings*/
	rc = msm_camera_fill_vreg_params(
		&a_ctrl->soc_info,
		power_info->power_down_setting,
		power_info->power_down_setting_size);
	if (rc) {
		CAM_ERR(CAM_APERTURE,
			"failed to fill vreg params power down rc:%d", rc);
		return rc;
	}

	power_info->dev = soc_info->dev;

	if (a_ctrl->io_master_info.master_type == I3C_MASTER)
		i3c_probe_completion = cam_aperture_get_i3c_completion(a_ctrl->soc_info.index);

	rc = cam_sensor_core_power_up(power_info, soc_info, i3c_probe_completion);
	if (rc) {
		CAM_ERR(CAM_APERTURE,
			"failed in aperture power up rc %d", rc);
		return rc;
	}

	rc = camera_io_init(&a_ctrl->io_master_info);
	if (rc < 0) {
		CAM_ERR(CAM_APERTURE, "cci init failed: rc: %d", rc);
		goto cci_failure;
	}

	CAM_GET_TIMESTAMP(ts2);
	CAM_GET_TIMESTAMP_DIFF_IN_MICRO(ts1, ts2, microsec);
	CAM_DBG(CAM_APERTURE, "%s end power_up, occupy time is: %ld ms",
		a_ctrl->device_name, microsec/1000);

	return rc;
cci_failure:
	if (cam_sensor_util_power_down(power_info, soc_info))
		CAM_ERR(CAM_APERTURE, "Power down failure");

	return rc;
}

static int32_t cam_aperture_power_down(struct cam_aperture_ctrl_t *a_ctrl)
{
	int32_t rc = 0;
	struct cam_sensor_power_ctrl_t *power_info;
	struct cam_hw_soc_info *soc_info = &a_ctrl->soc_info;
	struct cam_aperture_soc_private  *soc_private;
	struct timespec64 ts1, ts2;
	long microsec = 0;

	CAM_GET_TIMESTAMP(ts1);
	CAM_DBG(CAM_APERTURE, "%s start power_down", a_ctrl->device_name);

	if (!a_ctrl) {
		CAM_ERR(CAM_APERTURE, "failed: a_ctrl %pK", a_ctrl);
		return -EINVAL;
	}

	soc_private =
		(struct cam_aperture_soc_private *)a_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;
	soc_info = &a_ctrl->soc_info;

	if (!power_info) {
		CAM_ERR(CAM_APERTURE, "failed: power_info %pK", power_info);
		return -EINVAL;
	}
	rc = cam_sensor_util_power_down(power_info, soc_info);
	if (rc) {
		CAM_ERR(CAM_APERTURE, "power down the core is failed:%d", rc);
		return rc;
	}

	camera_io_release(&a_ctrl->io_master_info);

	CAM_GET_TIMESTAMP(ts2);
	CAM_GET_TIMESTAMP_DIFF_IN_MICRO(ts1, ts2, microsec);
	CAM_DBG(CAM_APERTURE, "%s end power_down, occupy time is: %ld ms",
		a_ctrl->device_name, microsec/1000);

	return rc;
}

static int32_t cam_aperture_i2c_modes_util(
	struct camera_io_master *io_master_info,
	struct i2c_settings_list *i2c_list)
{
	int32_t rc = 0;
	uint32_t i, size;

	if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_RANDOM) {
		rc = camera_io_dev_write(io_master_info,
			&(i2c_list->i2c_settings));
		if (rc < 0) {
			CAM_ERR(CAM_APERTURE,
				"Failed to random write I2C settings: %d",
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_SEQ) {
		rc = camera_io_dev_write_continuous(
			io_master_info,
			&(i2c_list->i2c_settings),
			CAM_SENSOR_I2C_WRITE_SEQ);
		if (rc < 0) {
			CAM_ERR(CAM_APERTURE,
				"Failed to seq write I2C settings: %d",
				rc);
			return rc;
			}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_BURST) {
		rc = camera_io_dev_write_continuous(
			io_master_info,
			&(i2c_list->i2c_settings),
			CAM_SENSOR_I2C_WRITE_BURST);
		if (rc < 0) {
			CAM_ERR(CAM_APERTURE,
				"Failed to burst write I2C settings: %d",
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_POLL) {
		size = i2c_list->i2c_settings.size;
		for (i = 0; i < size; i++) {
			rc = camera_io_dev_poll(
			io_master_info,
			i2c_list->i2c_settings.reg_setting[i].reg_addr,
			i2c_list->i2c_settings.reg_setting[i].reg_data,
			i2c_list->i2c_settings.reg_setting[i].data_mask,
			i2c_list->i2c_settings.addr_type,
			i2c_list->i2c_settings.data_type,
			i2c_list->i2c_settings.reg_setting[i].delay);
			if (rc < 0) {
				CAM_ERR(CAM_APERTURE,
					"i2c poll apply setting Fail: %d", rc);
				return rc;
			}
		}
	}

	return rc;
}

int32_t cam_aperture_slaveInfo_pkt_parser(struct cam_aperture_ctrl_t *a_ctrl,
	uint32_t *cmd_buf, size_t len)
{
	int32_t rc = 0;
	struct cam_cmd_i2c_info *i2c_info;

	if (!a_ctrl || !cmd_buf || (len < sizeof(struct cam_cmd_i2c_info))) {
		CAM_ERR(CAM_APERTURE, "Invalid Args");
		return -EINVAL;
	}

	i2c_info = (struct cam_cmd_i2c_info *)cmd_buf;
	if (a_ctrl->io_master_info.master_type == CCI_MASTER) {
		a_ctrl->io_master_info.cci_client->cci_i2c_master =
			a_ctrl->cci_i2c_master;
		a_ctrl->io_master_info.cci_client->i2c_freq_mode =
			i2c_info->i2c_freq_mode;
		a_ctrl->io_master_info.cci_client->sid =
			i2c_info->slave_addr >> 1;
		CAM_DBG(CAM_APERTURE, "Slave addr: 0x%x Freq Mode: %d",
			i2c_info->slave_addr, i2c_info->i2c_freq_mode);
	} else if (a_ctrl->io_master_info.master_type == I2C_MASTER) {
		a_ctrl->io_master_info.client->addr = i2c_info->slave_addr;
		CAM_DBG(CAM_APERTURE, "Slave addr: 0x%x", i2c_info->slave_addr);
	} else {
		CAM_ERR(CAM_APERTURE, "Invalid Master type: %d",
			a_ctrl->io_master_info.master_type);
		 rc = -EINVAL;
	}

	return rc;
}

int32_t cam_aperture_apply_settings(struct cam_aperture_ctrl_t *a_ctrl,
	struct i2c_settings_array *i2c_set)
{
	struct i2c_settings_list *i2c_list;
	int32_t rc = 0;
	int32_t j = 0, i = 0;

	if (a_ctrl == NULL || i2c_set == NULL) {
		CAM_ERR(CAM_APERTURE, "Invalid Args");
		return -EINVAL;
	}

	if (i2c_set->is_settings_valid != 1) {
		CAM_ERR(CAM_APERTURE, " Invalid settings");
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
				trace_cam_i2c_write_log_event("[APERTURESETTINGS]", a_ctrl->device_name,
					i2c_set->request_id, j, "WRITE", i2c_list->i2c_settings.reg_setting[j].reg_addr,
					i2c_list->i2c_settings.reg_setting[j].reg_data);
			}
			break;
		}
		case CAM_SENSOR_I2C_READ_RANDOM:
		case CAM_SENSOR_I2C_READ_SEQ: {
			for (j = 0;j < i2c_list->i2c_settings.size;j++) {
				trace_cam_i2c_write_log_event("[APERTURESETTINGS]", a_ctrl->device_name,
					i2c_set->request_id, j, "READ", i2c_list->i2c_settings.reg_setting[j].reg_addr,
					i2c_list->i2c_settings.reg_setting[j].reg_data);
			}
			break;
		}
		case CAM_SENSOR_I2C_POLL:{
			for (j = 0;j < i2c_list->i2c_settings.size;j++) {
				trace_cam_i2c_write_log_event("[APERTURESETTINGS]", a_ctrl->device_name,
					i2c_set->request_id, j, "POLL", i2c_list->i2c_settings.reg_setting[j].reg_addr,
					i2c_list->i2c_settings.reg_setting[j].reg_data);
			}
			break;
		}
		default:
			break;
		}

		if (false == cam_check_cci_is_error(&a_ctrl->cci_err_info)) {
			rc = cam_aperture_i2c_modes_util(
				&(a_ctrl->io_master_info),
				i2c_list);
			if (rc < 0) {
				CAM_WARN(CAM_APERTURE,
					"Failed to apply settings: %d",
					rc);
				/* xiaomi add to ignore the apply setting fail - begin */
				for (i = 0; i < MAX_RETRY_TIMES; i++) {
					usleep_range(1000, 1010);
					rc = cam_aperture_i2c_modes_util(
						&(a_ctrl->io_master_info),
						i2c_list);
					if(rc < 0){
						CAM_WARN(CAM_APERTURE,
						"Failed to apply settings: %d times:%d",rc,i);
					}else{
						break;
					}
				}

				if (rc < 0) {
					cam_cci_add_io_fail_count(&a_ctrl->cci_err_info);
					CAM_ERR(CAM_APERTURE,
						"Failed to re-apply settings: %d, skip!  IO fail count:%d",
						rc, cam_cci_read_io_fail_count(&a_ctrl->cci_err_info));
					break;
				}
			}
			else {
				cam_cci_reset_io_fail_count(&a_ctrl->cci_err_info);
				CAM_DBG(CAM_APERTURE,
					"Success:request ID: %d",
					i2c_set->request_id);
			}
		}
		else{
			CAM_ERR(CAM_APERTURE, "damage count:%d IO fail count:%d skip apply setting!",
				cam_cci_read_damage_count(&a_ctrl->cci_err_info),
				cam_cci_read_io_fail_count(&a_ctrl->cci_err_info));
		}
	}
	return rc;
}

int32_t cam_aperture_apply_request(struct cam_req_mgr_apply_request *apply)
{
	int32_t rc = 0, request_id, del_req_id;
	struct cam_aperture_ctrl_t *a_ctrl = NULL;

	if (!apply) {
		CAM_ERR(CAM_APERTURE, "Invalid Input Args");
		return -EINVAL;
	}

	a_ctrl = (struct cam_aperture_ctrl_t *)
		cam_get_device_priv(apply->dev_hdl);
	if (!a_ctrl) {
		CAM_ERR(CAM_APERTURE, "Device data is NULL");
		return -EINVAL;
	}
	request_id = apply->request_id % MAX_PER_FRAME_ARRAY;

	trace_cam_apply_req("Aperture", a_ctrl->soc_info.index, apply->request_id, apply->link_hdl);

	CAM_DBG(CAM_APERTURE, "Request Id: %lld", apply->request_id);
	mutex_lock(&(a_ctrl->aperture_mutex));
	if ((apply->request_id ==
		a_ctrl->i2c_data.per_frame[request_id].request_id) &&
		(a_ctrl->i2c_data.per_frame[request_id].is_settings_valid)
		== 1) {
		rc = cam_aperture_apply_settings(a_ctrl,
			&a_ctrl->i2c_data.per_frame[request_id]);
		if (rc < 0) {
			CAM_ERR(CAM_APERTURE,
				"Failed in applying the request: %lld\n",
				apply->request_id);
			goto release_mutex;
		} else {
			a_ctrl->i2c_data.per_frame[request_id].is_settings_valid = 0;
		}
	}

	if (rc < 0)
		goto release_mutex;

	del_req_id = (request_id +
		MAX_PER_FRAME_ARRAY - MAX_SYSTEM_PIPELINE_DELAY) %
		MAX_PER_FRAME_ARRAY;

	if (apply->request_id >
		a_ctrl->i2c_data.per_frame[del_req_id].request_id) {
		a_ctrl->i2c_data.per_frame[del_req_id].request_id = 0;
		rc = delete_request(&a_ctrl->i2c_data.per_frame[del_req_id]);
		if (rc < 0) {
			CAM_ERR(CAM_APERTURE,
				"Fail deleting the req: %d err: %d\n",
				del_req_id, rc);
			goto release_mutex;
		}
	} else {
		CAM_DBG(CAM_APERTURE, "No Valid Req to clean Up");
	}

release_mutex:
	mutex_unlock(&(a_ctrl->aperture_mutex));
	return rc;
}

int32_t cam_aperture_establish_link(
	struct cam_req_mgr_core_dev_link_setup *link)
{
	struct cam_aperture_ctrl_t *a_ctrl = NULL;

	if (!link) {
		CAM_ERR(CAM_APERTURE, "Invalid Args");
		return -EINVAL;
	}

	a_ctrl = (struct cam_aperture_ctrl_t *)
		cam_get_device_priv(link->dev_hdl);
	if (!a_ctrl) {
		CAM_ERR(CAM_APERTURE, "Device data is NULL");
		return -EINVAL;
	}

	mutex_lock(&(a_ctrl->aperture_mutex));
	if (link->link_enable) {
		a_ctrl->bridge_intf.link_hdl = link->link_hdl;
		a_ctrl->bridge_intf.crm_cb = link->crm_cb;
	} else {
		a_ctrl->bridge_intf.link_hdl = -1;
		a_ctrl->bridge_intf.crm_cb = NULL;
	}
	mutex_unlock(&(a_ctrl->aperture_mutex));

	return 0;
}

static int cam_aperture_update_req_mgr(
	struct cam_aperture_ctrl_t *a_ctrl,
	struct cam_packet *csl_packet,
	struct skip_frame  skip_info)
{
	int rc = 0;
	struct cam_req_mgr_add_request add_req;

	memset(&add_req, 0, sizeof(add_req));
	add_req.link_hdl = a_ctrl->bridge_intf.link_hdl;
	add_req.req_id = csl_packet->header.request_id;
	add_req.dev_hdl = a_ctrl->bridge_intf.device_hdl;

	if ((csl_packet->header.op_code & 0xFFFFFF) ==
		CAM_APERTURE_PACKET_SWITCH) {
		if(skip_info.trigger_eof) {
			add_req.trigger_eof = true;
			add_req.skip_at_sof = skip_info.skip_num;
		}
		else {
			add_req.trigger_eof = false;
		}
	}

	if (a_ctrl->bridge_intf.crm_cb &&
		a_ctrl->bridge_intf.crm_cb->add_req) {
		rc = a_ctrl->bridge_intf.crm_cb->add_req(&add_req);
		if (rc) {
			if (rc == -EBADR)
				CAM_INFO(CAM_APERTURE,
					"Adding request: %llu failed: rc: %d, it has been flushed",
					csl_packet->header.request_id, rc);
			else
				CAM_ERR(CAM_APERTURE,
					"Adding request: %llu failed: rc: %d",
					csl_packet->header.request_id, rc);
			return rc;
		}
		CAM_DBG(CAM_APERTURE, "Request Id: %lld added to CRM",
			add_req.req_id);
	} else {
		CAM_ERR(CAM_APERTURE, "Can't add Request ID: %lld to CRM",
			csl_packet->header.request_id);
		rc = -EINVAL;
	}

	return rc;
}

int32_t cam_aperture_publish_dev_info(struct cam_req_mgr_device_info *info)
{
	if (!info) {
		CAM_ERR(CAM_APERTURE, "Invalid Args");
		return -EINVAL;
	}

	info->dev_id = CAM_REQ_MGR_DEVICE_APERTURE;
	strlcpy(info->name, CAM_APERTURE_NAME, sizeof(info->name));
	info->p_delay = 1;
	info->trigger = CAM_TRIGGER_POINT_SOF;

	return 0;
}

int32_t cam_aperture_i2c_pkt_parse(struct cam_aperture_ctrl_t *a_ctrl,
	void *arg)
{
	int32_t  rc = 0;
	int32_t  i = 0;
	uint32_t total_cmd_buf_in_bytes = 0;
	size_t   len_of_buff = 0;
	size_t   remain_len  = 0;
	size_t   tot_size    = 0;
	uint32_t byte_cnt    = 0;
	uint32_t *offset     = NULL;
	uint32_t *cmd_buf    = NULL;
	uintptr_t generic_ptr;
	uintptr_t generic_pkt_ptr;
	uint16_t                  generic_op_code;
	struct common_header      *cmm_hdr = NULL;
	struct cam_control        *ioctl_ctrl = NULL;
	struct cam_packet         *csl_packet = NULL;
	struct cam_config_dev_cmd config;
	struct i2c_data_settings  *i2c_data = NULL;
	struct i2c_settings_array *i2c_reg_settings = NULL;
	struct cam_cmd_buf_desc   *cmd_desc = NULL;
	struct cam_aperture_soc_private *soc_private = NULL;
	struct cam_sensor_power_ctrl_t  *power_info = NULL;
	struct skip_frame   *skip_frame_msg = NULL;
	struct skip_frame    skip_info = {0};

	uint32_t                  j = 0;
	struct list_head          *list = NULL;

	if (!a_ctrl || !arg) {
		CAM_ERR(CAM_APERTURE, "Invalid Args");
		return -EINVAL;
	}

	soc_private =
		(struct cam_aperture_soc_private *)a_ctrl->soc_info.soc_private;

	power_info = &soc_private->power_info;

	ioctl_ctrl = (struct cam_control *)arg;
	if (copy_from_user(&config,
		u64_to_user_ptr(ioctl_ctrl->handle),
		sizeof(config)))
		return -EFAULT;
	rc = cam_mem_get_cpu_buf(config.packet_handle,
		&generic_pkt_ptr, &len_of_buff);
	if (rc < 0) {
		CAM_ERR(CAM_APERTURE, "Error in converting command Handle %d",
			rc);
		return rc;
	}

	remain_len = len_of_buff;
	if ((sizeof(struct cam_packet) > len_of_buff) ||
		((size_t)config.offset >= len_of_buff -
		sizeof(struct cam_packet))) {
		CAM_ERR(CAM_APERTURE,
			"Inval cam_packet strut size: %zu, len_of_buff: %zu",
			 sizeof(struct cam_packet), len_of_buff);
		rc = -EINVAL;
		goto end;
	}

	remain_len -= (size_t)config.offset;
	csl_packet = (struct cam_packet *)
			(generic_pkt_ptr + (uint32_t)config.offset);

	if (cam_packet_util_validate_packet(csl_packet,
		remain_len)) {
		CAM_ERR(CAM_APERTURE, "Invalid packet params");
		rc = -EINVAL;
		goto end;
	}

	if ((csl_packet->header.op_code & 0xFFFFFF) !=
		CAM_APERTURE_PACKET_OPCODE_INIT &&
		(csl_packet->header.op_code & 0xFFFFFF) !=
		CAM_APERTURE_PACKET_OPCODE_READ &&
		csl_packet->header.request_id <= a_ctrl->last_flush_req
		&& a_ctrl->last_flush_req != 0) {
		CAM_DBG(CAM_APERTURE,
			"reject request %lld, last request to flush %lld",
			csl_packet->header.request_id, a_ctrl->last_flush_req);
		rc = -EBADR;
		goto end;
	}

	if (csl_packet->header.request_id > a_ctrl->last_flush_req)
		a_ctrl->last_flush_req = 0;

	switch (csl_packet->header.op_code & 0xFFFFFF) {
	case CAM_APERTURE_PACKET_OPCODE_INIT:
		offset = (uint32_t *)&csl_packet->payload;
		offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);

		/* Loop through multiple command buffers */
		for (i = 0; i < csl_packet->num_cmd_buf; i++) {
			total_cmd_buf_in_bytes = cmd_desc[i].length;
			if (!total_cmd_buf_in_bytes)
				continue;
			rc = cam_mem_get_cpu_buf(cmd_desc[i].mem_handle,
					&generic_ptr, &len_of_buff);
			if (rc < 0) {
				CAM_ERR(CAM_APERTURE, "Failed to get cpu buf");
				goto end;
			}
			cmd_buf = (uint32_t *)generic_ptr;
			if (!cmd_buf) {
				CAM_ERR(CAM_APERTURE, "invalid cmd buf");
				rc = -EINVAL;
				goto end;
			}
			if ((len_of_buff < sizeof(struct common_header)) ||
				(cmd_desc[i].offset > (len_of_buff -
				sizeof(struct common_header)))) {
				CAM_ERR(CAM_APERTURE,
					"Invalid length for sensor cmd");
				rc = -EINVAL;
				goto end;
			}
			remain_len = len_of_buff - cmd_desc[i].offset;
			cmd_buf += cmd_desc[i].offset / sizeof(uint32_t);
			cmm_hdr = (struct common_header *)cmd_buf;

			switch (cmm_hdr->cmd_type) {
			case CAMERA_SENSOR_CMD_TYPE_I2C_INFO:
				CAM_DBG(CAM_APERTURE,
					"Received slave info buffer");
				rc = cam_aperture_slaveInfo_pkt_parser(
					a_ctrl, cmd_buf, remain_len);
				if (rc < 0) {
					CAM_ERR(CAM_APERTURE,
					"Failed to parse slave info: %d", rc);
					goto end;
				}
				break;
			case CAMERA_SENSOR_CMD_TYPE_PWR_UP:
			case CAMERA_SENSOR_CMD_TYPE_PWR_DOWN:
				CAM_DBG(CAM_APERTURE,
					"Received power settings buffer");
				rc = cam_sensor_update_power_settings(
						cmd_buf,
						total_cmd_buf_in_bytes,
						power_info, remain_len);
				if (rc) {
					CAM_ERR(CAM_APERTURE,
						"Failed:parse power settings: %d",
						rc);
					goto end;
				}
				break;
			default:
				CAM_DBG(CAM_APERTURE,
					"Received initSettings buffer");
				i2c_data = &(a_ctrl->i2c_data);
				i2c_reg_settings =
					&i2c_data->init_settings;

				i2c_reg_settings->request_id = 0;
				i2c_reg_settings->is_settings_valid = 1;
				rc = cam_sensor_i2c_command_parser(
					&a_ctrl->io_master_info,
					i2c_reg_settings,
					&cmd_desc[i], 1, NULL);
				if (rc < 0) {
					CAM_ERR(CAM_APERTURE,
					"Failed:parse init settings: %d",
					rc);
					goto end;
				}
				break;
			}
			cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
		}

		if (a_ctrl->cam_act_state == CAM_APERTURE_ACQUIRE) {
				rc = cam_aperture_power_up(a_ctrl);
				if (rc < 0) {
					CAM_ERR(CAM_APERTURE,
						" Aperture Power up failed");
					goto end;
				}
			a_ctrl->cam_act_state = CAM_APERTURE_CONFIG;
		}

		rc = cam_aperture_apply_settings(a_ctrl,
			&a_ctrl->i2c_data.init_settings);
		if (rc < 0) {
			cam_cci_set_init_fail(&a_ctrl->cci_err_info);
			CAM_ERR(CAM_APERTURE, "Cannot apply Init settings");
			goto end;
		}

		/* Delete the request even if the apply is failed */
		rc = delete_request(&a_ctrl->i2c_data.init_settings);
		if (rc < 0) {
			CAM_WARN(CAM_APERTURE,
				"Fail in deleting the Init settings");
		}

		if (true == cam_check_cci_is_error(&a_ctrl->cci_err_info)) {
			cam_cci_set_init_fail(&a_ctrl->cci_err_info);
			rc = - ENODEV;
			CAM_ERR(CAM_APERTURE,
				"Device is damaged! rc:%d", rc);
		}
		break;
	case CAM_APERTURE_PACKET_SWITCH:
		if (a_ctrl->cam_act_state < CAM_APERTURE_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_APERTURE,
				"Not in right state to move lens: %d",
				a_ctrl->cam_act_state);
			goto end;
		}
		a_ctrl->setting_apply_state = APT_APPLY_SETTINGS_LATER;

		i2c_data = &(a_ctrl->i2c_data);
		i2c_reg_settings = &i2c_data->per_frame[
			csl_packet->header.request_id % MAX_PER_FRAME_ARRAY];

		i2c_reg_settings->request_id =
			csl_packet->header.request_id;
		i2c_reg_settings->is_settings_valid = 1;
		offset = (uint32_t *)&csl_packet->payload;
		offset += csl_packet->cmd_buf_offset / sizeof(uint32_t);
		cmd_desc = (struct cam_cmd_buf_desc *)(offset);

		total_cmd_buf_in_bytes = cmd_desc[0].length;

		CAM_DBG(CAM_APERTURE, "[ApeSkipFrame] total_cmd_buf_in_bytes %d",
			total_cmd_buf_in_bytes);

		rc = cam_mem_get_cpu_buf(cmd_desc[0].mem_handle,
			&generic_ptr, &len_of_buff);

		if (rc < 0) {
			CAM_ERR(CAM_APERTURE, "[ApeSkipFrame] Failed to get cpu buf : 0x%x",
				cmd_desc[0].mem_handle);
			rc = -EINVAL;
			goto end;
		}

		cmd_buf = (uint32_t *)generic_ptr;
		if (!cmd_buf) {
			CAM_ERR(CAM_APERTURE, "[ApeSkipFrame] invalid cmd buf");
			rc = -EINVAL;
			goto end;
		}

		cmd_buf += cmd_desc[0].offset / sizeof(uint32_t);
		skip_frame_msg = ((struct skip_frame*)cmd_buf);

		memcpy(&skip_info, skip_frame_msg, sizeof(struct skip_frame));

		CAM_INFO(CAM_APERTURE, "[ApeSkipFrame] skip reqId: %llu, skip num: %llu "
				"trigger_eof: %d",
				skip_info.req_id,
				skip_info.skip_num,
				skip_info.trigger_eof);

		total_cmd_buf_in_bytes -= sizeof(struct skip_frame);

	    remain_len = len_of_buff;
		cmd_buf    = (uint32_t *)(skip_frame_msg+1);

		remain_len -= cmd_desc[0].offset;
		if (remain_len < total_cmd_buf_in_bytes) {
			CAM_ERR(CAM_APERTURE, "buffer provided too small");
			rc = -EINVAL;
			goto end;
		}

		while (byte_cnt < total_cmd_buf_in_bytes) {
			if ((remain_len - byte_cnt) <
				sizeof(struct common_header)) {
				CAM_ERR(CAM_APERTURE, "Not enough buffer");
				rc = -EINVAL;
				goto end;
			}
			cmm_hdr = (struct common_header *)cmd_buf;
			generic_op_code = cmm_hdr->fifth_byte;

			switch (cmm_hdr->cmd_type) {
				case CAMERA_SENSOR_CMD_TYPE_I2C_RNDM_WR: {
					uint32_t cmd_length_in_bytes   = 0;
					struct cam_cmd_i2c_random_wr
						*cam_cmd_i2c_random_wr =
						(struct cam_cmd_i2c_random_wr *)cmd_buf;

					if ((remain_len - byte_cnt) <
						sizeof(struct cam_cmd_i2c_random_wr)) {
						CAM_ERR(CAM_APERTURE,
							"Not enough buffer provided");
						rc = -EINVAL;
						goto end;
					}
					tot_size = sizeof(struct i2c_rdwr_header) +
						(sizeof(struct i2c_random_wr_payload) *
						cam_cmd_i2c_random_wr->header.count);

					if (tot_size > (remain_len - byte_cnt)) {
						CAM_ERR(CAM_APERTURE,
							"Not enough buffer provided");
						rc = -EINVAL;
						goto end;
					}

					rc = cam_sensor_handle_random_write(
						cam_cmd_i2c_random_wr,
						i2c_reg_settings,
						&cmd_length_in_bytes, &j, &list);
					if (rc < 0) {
						CAM_ERR(CAM_APERTURE,
						"Failed in random write %d", rc);
						rc = -EINVAL;
						goto end;
					}

					cmd_buf  += cmd_length_in_bytes /
						sizeof(uint32_t);
					byte_cnt += cmd_length_in_bytes;
					break;
				}
				case CAMERA_SENSOR_CMD_TYPE_I2C_CONT_WR: {
					uint32_t cmd_length_in_bytes   = 0;
					struct cam_cmd_i2c_continuous_wr
					*cam_cmd_i2c_continuous_wr =
					(struct cam_cmd_i2c_continuous_wr *)
					cmd_buf;

					if ((remain_len - byte_cnt) <
					sizeof(struct cam_cmd_i2c_continuous_wr)) {
						CAM_ERR(CAM_APERTURE,
							"Not enough buffer provided");
						rc = -EINVAL;
						goto end;
					}

					tot_size = sizeof(struct i2c_rdwr_header) +
					sizeof(cam_cmd_i2c_continuous_wr->reg_addr) +
					(sizeof(struct cam_cmd_read) *
					cam_cmd_i2c_continuous_wr->header.count);

					if (tot_size > (remain_len - byte_cnt)) {
						CAM_ERR(CAM_APERTURE,
							"Not enough buffer provided");
						rc = -EINVAL;
						goto end;
					}

					rc = cam_sensor_handle_continuous_write(
						cam_cmd_i2c_continuous_wr,
						i2c_reg_settings,
						&cmd_length_in_bytes, &j, &list);
					if (rc < 0) {
						CAM_ERR(CAM_APERTURE,
						"Failed in continuous write %d", rc);
						goto end;
					}

					cmd_buf += cmd_length_in_bytes /
						sizeof(uint32_t);
					byte_cnt += cmd_length_in_bytes;

					break;
				}
				case CAMERA_SENSOR_CMD_TYPE_WAIT: {
					if ((((generic_op_code == CAMERA_SENSOR_WAIT_OP_HW_UCND) ||
						(generic_op_code == CAMERA_SENSOR_WAIT_OP_SW_UCND)) &&
						((remain_len - byte_cnt) <
						sizeof(struct cam_cmd_unconditional_wait))) ||
						((generic_op_code == CAMERA_SENSOR_WAIT_OP_COND) &&
						((remain_len - byte_cnt) <
						sizeof(struct cam_cmd_conditional_wait)))) {
						CAM_ERR(CAM_APERTURE,
							"Not enough buffer space");
						rc = -EINVAL;
						goto end;
					}

					if (generic_op_code ==
						CAMERA_SENSOR_WAIT_OP_HW_UCND ||
						generic_op_code ==
							CAMERA_SENSOR_WAIT_OP_SW_UCND) {
						rc = cam_sensor_handle_delay(
							&cmd_buf, generic_op_code,
							i2c_reg_settings, j, &byte_cnt,
							list);
						if (rc < 0) {
							CAM_ERR(CAM_APERTURE,
								"delay hdl failed: %d",
								rc);
							goto end;
						}

					} else if (generic_op_code ==
						CAMERA_SENSOR_WAIT_OP_COND) {
						rc = cam_sensor_handle_poll(
							&cmd_buf, i2c_reg_settings,
							&byte_cnt, &j, &list);
						if (rc < 0) {
							CAM_ERR(CAM_APERTURE,
								"Random read fail: %d",
								rc);
							goto end;
						}
					} else {
						CAM_ERR(CAM_APERTURE,
							"Wrong Wait Command: %d",
							generic_op_code);
						rc = -EINVAL;
						goto end;
					}

					break;
				}
			}
		}

		rc = cam_aperture_update_req_mgr(a_ctrl, csl_packet, skip_info);
		if (rc) {
			CAM_ERR(CAM_APERTURE,
				"Failed in adding request to request manager");
			goto end;
		}
		cam_mem_put_cpu_buf(cmd_desc[0].mem_handle);
		break;
	case CAM_PKT_NOP_OPCODE:
		if (a_ctrl->cam_act_state < CAM_APERTURE_CONFIG) {
			CAM_WARN(CAM_APERTURE,
				"Received NOP packets in invalid state: %d",
				a_ctrl->cam_act_state);
			rc = -EINVAL;
			goto end;
		}
		rc = cam_aperture_update_req_mgr(a_ctrl, csl_packet, skip_info);
		if (rc) {
			CAM_ERR(CAM_APERTURE,
				"Failed in adding request to request manager");
			goto end;
		}
		break;

	case CAM_APERTURE_PACKET_OPCODE_READ: {
			uint64_t qtime_ns;
			struct cam_buf_io_cfg *io_cfg;
			struct i2c_settings_array i2c_read_settings;

			if (a_ctrl->cam_act_state < CAM_APERTURE_CONFIG) {
				rc = -EINVAL;
				CAM_WARN(CAM_APERTURE,
					"Not in right state to read aperture: %d",
					a_ctrl->cam_act_state);
				goto end;
			}
			CAM_DBG(CAM_APERTURE, "number of I/O configs: %d:",
				csl_packet->num_io_configs);
			if (csl_packet->num_io_configs == 0) {
				CAM_ERR(CAM_APERTURE, "No I/O configs to process");
				rc = -EINVAL;
				goto end;
			}

			INIT_LIST_HEAD(&(i2c_read_settings.list_head));

			io_cfg = (struct cam_buf_io_cfg *) ((uint8_t *)
				&csl_packet->payload +
				csl_packet->io_configs_offset);

			if (io_cfg == NULL) {
				CAM_ERR(CAM_APERTURE, "I/O config is invalid(NULL)");
				rc = -EINVAL;
				goto end;
			}

			offset = (uint32_t *)&csl_packet->payload;
			offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
			cmd_desc = (struct cam_cmd_buf_desc *)(offset);
			i2c_read_settings.is_settings_valid = 1;
			i2c_read_settings.request_id = 0;
			rc = cam_sensor_i2c_command_parser(&a_ctrl->io_master_info,
				&i2c_read_settings,
				cmd_desc, 1, &io_cfg[0]);
			if (rc < 0) {
				CAM_ERR(CAM_APERTURE,
					"aperture read pkt parsing failed: %d", rc);
				goto end;
			}

			rc = cam_sensor_util_get_current_qtimer_ns(&qtime_ns);
			if (rc < 0) {
				CAM_ERR(CAM_APERTURE, "failed to get qtimer rc:%d");
				goto end;
			}

			if (false == cam_check_cci_is_error(&a_ctrl->cci_err_info)) {
				rc = cam_sensor_i2c_read_data(
					&i2c_read_settings,
					&a_ctrl->io_master_info);
				if (rc < 0) {
					CAM_ERR(CAM_APERTURE, "cannot read data, rc:%d", rc);
					delete_request(&i2c_read_settings);
					goto end;
				}
			}
			else {
				CAM_ERR(CAM_APERTURE, "damage count:%d IO fail count:%d skip apply read data!",
					cam_cci_read_damage_count(&a_ctrl->cci_err_info),
					cam_cci_read_io_fail_count(&a_ctrl->cci_err_info));
			}

			if (csl_packet->num_io_configs > 1) {
				rc = cam_sensor_util_write_qtimer_to_io_buffer(
					qtime_ns, &io_cfg[1]);
				if (rc < 0) {
					CAM_ERR(CAM_APERTURE,
						"write qtimer failed rc: %d", rc);
					delete_request(&i2c_read_settings);
					goto end;
				}
			}

			rc = delete_request(&i2c_read_settings);
			if (rc < 0 ||
					true == cam_check_cci_is_error(&a_ctrl->cci_err_info)) {
				CAM_ERR(CAM_APERTURE,
					"Failed in deleting the read settings, rc=%d or cci error", rc);
				goto end;
			}
			break;
		}
	default:
		CAM_ERR(CAM_APERTURE, "Wrong Opcode: %d",
			csl_packet->header.op_code & 0xFFFFFF);
		rc = -EINVAL;
		goto end;
	}

end:
	cam_mem_put_cpu_buf(config.packet_handle);
	return rc;
}

void cam_aperture_shutdown(struct cam_aperture_ctrl_t *a_ctrl)
{
	int rc = 0;
	struct cam_aperture_soc_private  *soc_private =
		(struct cam_aperture_soc_private *)a_ctrl->soc_info.soc_private;
	struct cam_sensor_power_ctrl_t *power_info =
		&soc_private->power_info;

	if (a_ctrl->cam_act_state == CAM_APERTURE_INIT)
		return;

	if (a_ctrl->cam_act_state >= CAM_APERTURE_CONFIG) {
		rc = cam_aperture_power_down(a_ctrl);
		if (rc < 0)
			CAM_ERR(CAM_APERTURE, "Aperture Power down failed");
		a_ctrl->cam_act_state = CAM_APERTURE_ACQUIRE;
	}

	if (a_ctrl->cam_act_state >= CAM_APERTURE_ACQUIRE) {
		rc = cam_destroy_device_hdl(a_ctrl->bridge_intf.device_hdl);
		if (rc < 0)
			CAM_ERR(CAM_APERTURE, "destroying  dhdl failed");
		a_ctrl->bridge_intf.device_hdl = -1;
		a_ctrl->bridge_intf.link_hdl = -1;
		a_ctrl->bridge_intf.session_hdl = -1;
	}

	kfree(power_info->power_setting);
	kfree(power_info->power_down_setting);
	power_info->power_setting = NULL;
	power_info->power_down_setting = NULL;
	power_info->power_setting_size = 0;
	power_info->power_down_setting_size = 0;
	a_ctrl->last_flush_req = 0;

	a_ctrl->cam_act_state = CAM_APERTURE_INIT;
}

int32_t cam_aperture_driver_cmd(struct cam_aperture_ctrl_t *a_ctrl,
	void *arg)
{
	int rc = 0;
	struct cam_control *cmd = (struct cam_control *)arg;
	struct cam_aperture_soc_private *soc_private = NULL;
	struct cam_sensor_power_ctrl_t  *power_info = NULL;

	if (!a_ctrl || !cmd) {
		CAM_ERR(CAM_APERTURE, "Invalid Args");
		return -EINVAL;
	}

	soc_private =
		(struct cam_aperture_soc_private *)a_ctrl->soc_info.soc_private;

	power_info = &soc_private->power_info;

	if (cmd->handle_type != CAM_HANDLE_USER_POINTER) {
		CAM_ERR(CAM_APERTURE, "Invalid handle type: %d",
			cmd->handle_type);
		return -EINVAL;
	}

	mutex_lock(&(a_ctrl->aperture_mutex));
	switch (cmd->op_code) {
	case CAM_ACQUIRE_DEV: {
		struct cam_sensor_acquire_dev aperture_acq_dev;
		struct cam_create_dev_hdl bridge_params;

		if (a_ctrl->bridge_intf.device_hdl != -1) {
			CAM_ERR(CAM_APERTURE, "Device is already acquired");
			rc = -EINVAL;
			goto release_mutex;
		}
		rc = copy_from_user(&aperture_acq_dev,
			u64_to_user_ptr(cmd->handle),
			sizeof(aperture_acq_dev));
		if (rc < 0) {
			CAM_ERR(CAM_APERTURE, "Failed Copying from user\n");
			goto release_mutex;
		}

		bridge_params.session_hdl = aperture_acq_dev.session_handle;
		bridge_params.ops = &a_ctrl->bridge_intf.ops;
		bridge_params.v4l2_sub_dev_flag = 0;
		bridge_params.media_entity_flag = 0;
		bridge_params.priv = a_ctrl;
		bridge_params.dev_id = CAM_APERTURE;

		aperture_acq_dev.device_handle =
			cam_create_device_hdl(&bridge_params);
		if (aperture_acq_dev.device_handle <= 0) {
			rc = -EFAULT;
			CAM_ERR(CAM_APERTURE, "Can not create device handle");
			goto release_mutex;
		}
		a_ctrl->bridge_intf.device_hdl = aperture_acq_dev.device_handle;
		a_ctrl->bridge_intf.session_hdl =
			aperture_acq_dev.session_handle;

		CAM_INFO(CAM_APERTURE, "CAM_ACQUIRE_DEV Device Handle: %d",
			aperture_acq_dev.device_handle);
		if (copy_to_user(u64_to_user_ptr(cmd->handle),
			&aperture_acq_dev,
			sizeof(struct cam_sensor_acquire_dev))) {
			CAM_ERR(CAM_APERTURE, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}
		cam_cci_clear_init_fail(&a_ctrl->cci_err_info);
		cam_cci_reset_io_fail_count(&a_ctrl->cci_err_info);
		a_ctrl->cam_act_state = CAM_APERTURE_ACQUIRE;
	}
		break;
	case CAM_RELEASE_DEV: {
		struct i2c_settings_array *i2c_set = NULL;
		int i;

		CAM_INFO(CAM_APERTURE, "CAM_RELEASE_DEV Device");

		if (a_ctrl->cam_act_state == CAM_APERTURE_START) {
			rc = -EINVAL;
			CAM_WARN(CAM_APERTURE,
				"Cant release aperture: in start state");
			goto release_mutex;
		}

		if (a_ctrl->cam_act_state == CAM_APERTURE_CONFIG) {
			rc = cam_aperture_power_down(a_ctrl);
			if (rc < 0) {
				CAM_ERR(CAM_APERTURE,
					"Aperture Power Down Failed");
				goto release_mutex;
			}
		}

		if (a_ctrl->bridge_intf.device_hdl == -1) {
			CAM_ERR(CAM_APERTURE, "link hdl: %d device hdl: %d",
				a_ctrl->bridge_intf.device_hdl,
				a_ctrl->bridge_intf.link_hdl);
			rc = -EINVAL;
			goto release_mutex;
		}

		if (a_ctrl->bridge_intf.link_hdl != -1) {
			CAM_ERR(CAM_APERTURE,
				"Device [%d] still active on link 0x%x",
				a_ctrl->cam_act_state,
				a_ctrl->bridge_intf.link_hdl);
			rc = -EAGAIN;
			goto release_mutex;
		}

		rc = cam_destroy_device_hdl(a_ctrl->bridge_intf.device_hdl);
		if (rc < 0)
			CAM_ERR(CAM_APERTURE, "destroying the device hdl");
		a_ctrl->bridge_intf.device_hdl = -1;
		a_ctrl->bridge_intf.link_hdl = -1;
		a_ctrl->bridge_intf.session_hdl = -1;
		a_ctrl->cam_act_state = CAM_APERTURE_INIT;
		a_ctrl->last_flush_req = 0;

		kfree(power_info->power_setting);
		kfree(power_info->power_down_setting);
		power_info->power_setting = NULL;
		power_info->power_down_setting = NULL;
		power_info->power_down_setting_size = 0;
		power_info->power_setting_size = 0;


		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			i2c_set = &(a_ctrl->i2c_data.per_frame[i]);

			if (i2c_set->is_settings_valid == 1) {
				rc = delete_request(i2c_set);
				if (rc < 0)
					CAM_ERR(CAM_APERTURE,
						"[CRM-Aperture] delete request: %lld rc: %d",
						i2c_set->request_id, rc);
			}
		}

		if (true == cam_check_cci_is_damage(&a_ctrl->cci_err_info))
			CAM_ERR(CAM_APERTURE, "dev may damaged!, damage count:%d",
				cam_cci_read_damage_count(&a_ctrl->cci_err_info));
	}
		break;
	case CAM_QUERY_CAP: {
		struct cam_aperture_query_cap aperture_cap = {0};

		CAM_DBG(CAM_APERTURE, "aperture Query Cap");
		aperture_cap.slot_info = a_ctrl->soc_info.index;
		if (copy_to_user(u64_to_user_ptr(cmd->handle),
			&aperture_cap,
			sizeof(struct cam_aperture_query_cap))) {
			CAM_ERR(CAM_APERTURE, "Failed Copy to User");
			rc = -EFAULT;
			goto release_mutex;
		}
	}
		break;
	case CAM_START_DEV: {

		CAM_INFO(CAM_APERTURE, "CAM_START_DEV Device");

		if (a_ctrl->cam_act_state != CAM_APERTURE_CONFIG) {
			rc = -EINVAL;
			CAM_WARN(CAM_APERTURE,
			"Not in right state to start : %d",
			a_ctrl->cam_act_state);
			goto release_mutex;
		}
		a_ctrl->cam_act_state = CAM_APERTURE_START;
		a_ctrl->last_flush_req = 0;
	}
		break;
	case CAM_STOP_DEV: {
		struct i2c_settings_array *i2c_set = NULL;
		int i;

		CAM_INFO(CAM_APERTURE, "CAM_STOP_DEV Device");

		if (a_ctrl->cam_act_state != CAM_APERTURE_START) {
			rc = -EINVAL;
			CAM_WARN(CAM_APERTURE,
			"Not in right state to stop : %d",
			a_ctrl->cam_act_state);
			goto release_mutex;
		}

		for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
			i2c_set = &(a_ctrl->i2c_data.per_frame[i]);

			if (i2c_set->is_settings_valid == 1) {
				rc = delete_request(i2c_set);
				if (rc < 0)
					CAM_ERR(CAM_APERTURE,
						"delete request: %lld rc: %d",
						i2c_set->request_id, rc);
			}
		}
		a_ctrl->last_flush_req = 0;
		a_ctrl->cam_act_state = CAM_APERTURE_CONFIG;

	}
		break;
	case CAM_CONFIG_DEV: {
		a_ctrl->setting_apply_state =
			APT_APPLY_SETTINGS_LATER;
		rc = cam_aperture_i2c_pkt_parse(a_ctrl, arg);
		if (rc < 0) {
			if (rc == -EBADR)
				CAM_INFO(CAM_APERTURE,
					"Failed in aperture Parsing, it has been flushed");
			else
				CAM_ERR(CAM_APERTURE,
					"Failed in aperture Parsing");
			goto release_mutex;
		}

		if (a_ctrl->setting_apply_state ==
			APT_APPLY_SETTINGS_NOW) {
			rc = cam_aperture_apply_settings(a_ctrl,
				&a_ctrl->i2c_data.init_settings);
			if ((rc == -EAGAIN) &&
			(a_ctrl->io_master_info.master_type == CCI_MASTER)) {
				CAM_WARN(CAM_APERTURE,
					"CCI HW is in resetting mode:: Reapplying Init settings");
				usleep_range(1000, 1010);
				cam_cci_reset_io_fail_count(&a_ctrl->cci_err_info);
				rc = cam_aperture_apply_settings(a_ctrl,
					&a_ctrl->i2c_data.init_settings);
			}

			if (rc < 0) {
				cam_cci_set_init_fail(&a_ctrl->cci_err_info);
				CAM_ERR(CAM_APERTURE,
					"Failed to apply Init settings: rc = %d",
					rc);
			}

			/* Delete the request even if the apply is failed */
			rc = delete_request(&a_ctrl->i2c_data.init_settings);
			if (rc < 0) {
				CAM_ERR(CAM_APERTURE,
					"Failed in Deleting the Init Pkt: %d",
					rc);
				goto release_mutex;
			}

			if (true == cam_check_cci_is_error(&a_ctrl->cci_err_info)) {
				cam_cci_set_init_fail(&a_ctrl->cci_err_info);
				rc = - ENODEV;
				CAM_ERR(CAM_APERTURE,
					"Device is damaged rc: %d",
					rc);
			}
		}
	}
		break;
	default:
		CAM_ERR(CAM_APERTURE, "Invalid Opcode %d", cmd->op_code);
	}

release_mutex:
	mutex_unlock(&(a_ctrl->aperture_mutex));

	return rc;
}

int32_t cam_aperture_flush_request(struct cam_req_mgr_flush_request *flush_req)
{
	int32_t rc = 0, i;
	uint32_t cancel_req_id_found = 0;
	struct cam_aperture_ctrl_t *a_ctrl = NULL;
	struct i2c_settings_array *i2c_set = NULL;

	if (!flush_req)
		return -EINVAL;

	a_ctrl = (struct cam_aperture_ctrl_t *)
		cam_get_device_priv(flush_req->dev_hdl);
	if (!a_ctrl) {
		CAM_ERR(CAM_APERTURE, "Device data is NULL");
		return -EINVAL;
	}

	if (a_ctrl->i2c_data.per_frame == NULL) {
		CAM_ERR(CAM_APERTURE, "i2c frame data is NULL");
		return -EINVAL;
	}

	mutex_lock(&(a_ctrl->aperture_mutex));
	if (flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_ALL) {
		a_ctrl->last_flush_req = flush_req->req_id;
		CAM_DBG(CAM_APERTURE, "last reqest to flush is %lld",
			flush_req->req_id);
	}

	for (i = 0; i < MAX_PER_FRAME_ARRAY; i++) {
		i2c_set = &(a_ctrl->i2c_data.per_frame[i]);

		if ((flush_req->type == CAM_REQ_MGR_FLUSH_TYPE_CANCEL_REQ)
				&& (i2c_set->request_id != flush_req->req_id))
			continue;

		if (i2c_set->is_settings_valid == 1) {
			rc = delete_request(i2c_set);
			if (rc < 0)
				CAM_ERR(CAM_APERTURE,
					"delete request: %lld rc: %d",
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
		CAM_DBG(CAM_APERTURE,
			"Flush request id:%lld not found in the pending list",
			flush_req->req_id);

	mutex_unlock(&(a_ctrl->aperture_mutex));
	return rc;
}
