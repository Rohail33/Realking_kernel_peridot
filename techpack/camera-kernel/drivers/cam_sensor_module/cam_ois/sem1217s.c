#include <linux/module.h>
#include <linux/firmware.h>
#include <cam_sensor_cmn_header.h>
#include "cam_ois_core.h"
#include "cam_ois_soc.h"
#include "cam_sensor_util.h"
#include "cam_debug_util.h"
#include "cam_res_mgr_api.h"
#include "cam_common_util.h"
#include "cam_packet_util.h"
#include "sem1217s.h"

static int semco_ois_fw_update_op = 0;
module_param(semco_ois_fw_update_op, int, 0644);

int sem1217s_ois_apply_settings(struct cam_ois_ctrl_t *o_ctrl,
		struct i2c_settings_array *i2c_set)
{
	struct i2c_settings_list *i2c_list;
	int32_t rc = 0;
	uint32_t i, size;
	int32_t j = 0;

	list_for_each_entry(i2c_list, &(i2c_set->list_head), list) {
		switch (i2c_list->op_code) {
			case CAM_SENSOR_I2C_WRITE_RANDOM:
			case CAM_SENSOR_I2C_WRITE_BURST:
			case CAM_SENSOR_I2C_WRITE_SEQ: {
				for (j = 0;j < i2c_list->i2c_settings.size;j++) {
					CAM_DBG(CAM_OIS,"[sem1217s] ois name %s, request id %d, j=[%d], reg_addr 0x%x, 0x%x", o_ctrl->ois_name,
						i2c_set->request_id, j, i2c_list->i2c_settings.reg_setting[j].reg_addr,
						i2c_list->i2c_settings.reg_setting[j].reg_data);
					trace_cam_i2c_write_log_event("[sem1217s]", o_ctrl->ois_name,
						i2c_set->request_id, j, "WRITE", i2c_list->i2c_settings.reg_setting[j].reg_addr,
						i2c_list->i2c_settings.reg_setting[j].reg_data);
				}
				break;
			}
			case CAM_SENSOR_I2C_READ_RANDOM:
			case CAM_SENSOR_I2C_READ_SEQ: {
				for (j = 0;j < i2c_list->i2c_settings.size;j++) {
					CAM_DBG(CAM_OIS, "[sem1217s] ois name  %s, request id %d, j=[%d], reg_addr 0x%x, 0x%x",o_ctrl->ois_name,
						i2c_set->request_id, j,i2c_list->i2c_settings.reg_setting[j].reg_addr,
						i2c_list->i2c_settings.reg_setting[j].reg_data);
					trace_cam_i2c_write_log_event("[sem1217s]", o_ctrl->ois_name,
						i2c_set->request_id, j, "READ", i2c_list->i2c_settings.reg_setting[j].reg_addr,
						i2c_list->i2c_settings.reg_setting[j].reg_data);
				}
				break;
			}
			case CAM_SENSOR_I2C_POLL: {
				for (j = 0;j < i2c_list->i2c_settings.size;j++) {
					trace_cam_i2c_write_log_event("[sem1217s]", o_ctrl->ois_name,
						i2c_set->request_id, j, "POLL", i2c_list->i2c_settings.reg_setting[j].reg_addr,
						i2c_list->i2c_settings.reg_setting[j].reg_data);
				}
				break;
			}
			default:
				break;
		}
		if (i2c_list->op_code ==  CAM_SENSOR_I2C_WRITE_RANDOM) {
			rc = camera_io_dev_write(&(o_ctrl->io_master_info),
				&(i2c_list->i2c_settings));
			if (rc < 0) {
				CAM_ERR(CAM_OIS,
					"[sem1217s] Failed in Applying i2c wrt settings");
				return rc;
			}
		} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_SEQ) {
			rc = camera_io_dev_write_continuous(
				&(o_ctrl->io_master_info),
				&(i2c_list->i2c_settings),
				CAM_SENSOR_I2C_WRITE_SEQ);
			if (rc < 0) {
				CAM_ERR(CAM_OIS,
					"[sem1217s] Failed to seq write I2C settings: %d",
					rc);
				return rc;
			}
		} else if (i2c_list->op_code == CAM_SENSOR_I2C_POLL) {
			size = i2c_list->i2c_settings.size;
			for (i = 0; i < size; i++) {
				rc = camera_io_dev_poll(
				&(o_ctrl->io_master_info),
				i2c_list->i2c_settings.reg_setting[i].reg_addr,
				i2c_list->i2c_settings.reg_setting[i].reg_data,
				i2c_list->i2c_settings.reg_setting[i].data_mask,
				i2c_list->i2c_settings.addr_type,
				i2c_list->i2c_settings.data_type,
				i2c_list->i2c_settings.reg_setting[i].delay);
				if (rc < 0) {
					CAM_ERR(CAM_OIS,
						"[sem1217s] i2c poll apply setting Fail");
					return rc;
				}
			}
        }
	}
	return rc;
}

static int32_t i2c_read_data(
	struct cam_ois_ctrl_t *o_ctrl, uint32_t addr, uint32_t length, uint8_t *data)
{
	int32_t rc = 0;
	int32_t i = 0;

	rc = camera_io_dev_read_seq(&o_ctrl->io_master_info,
		addr, data,
		CAMERA_SENSOR_I2C_TYPE_WORD,
		CAMERA_SENSOR_I2C_TYPE_BYTE,
		length);

	for (i = 0; i < length; i++) {
		CAM_DBG(CAM_OIS, "[SEM1217S] read addr 0x%04x[%d] = 0x%02x", addr, i, data[i]);
	}

	if (rc) {
		CAM_ERR(CAM_OIS, "[SEM1217S] Failed to read, rc: %d", rc);
	}

	return rc;
}

static int32_t i2c_write_data(
	struct cam_ois_ctrl_t *o_ctrl, uint32_t addr, uint32_t length, uint8_t* data, uint32_t delay)
{
	static struct cam_sensor_i2c_reg_array w_data[256] = { {0} };
	struct cam_sensor_i2c_reg_setting write_setting;
	uint32_t i = 0;
	int32_t rc = 0;

	if (!data || !o_ctrl || (length < 1)) {
		CAM_ERR(CAM_OIS, "[SEM1217S] Invalid Args");
		rc = -EINVAL;
		return rc;
	}

	for (i = 0; i < length && i < 256; i++) {
		w_data[i].reg_addr = addr;
		w_data[i].reg_data = data[i];
		w_data[i].delay = 0;
		w_data[i].data_mask = 0;
	}

	write_setting.size = length;
	write_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
	write_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	write_setting.delay = delay;
	write_setting.reg_setting = w_data;

	rc = camera_io_dev_write_continuous(&(o_ctrl->io_master_info),
		&write_setting, CAM_SENSOR_I2C_WRITE_SEQ);

	if (rc < 0) {
		CAM_ERR(CAM_OIS, "[SEM1217S] OIS i2c_write_data write failed, rc: %d", rc);
	}

	for (i = 0; i < length && i < 256; i+=4) {
		CAM_DBG(CAM_OIS, "[SEM1217S] Write addr 0x%04x = 0x%02x 0x%02x 0x%02x 0x%02x",
			w_data[i].reg_addr, data[i], data[i+1], data[i+2], data[i+3]);
	}

	return rc;
}

static int32_t load_fw_buff(
	struct cam_ois_ctrl_t *o_ctrl,
	char* firmware_name,
	uint8_t *read_data,
	uint32_t read_length)
{
	uint16_t                            total_bytes = 0;
	uint8_t                             *ptr = NULL;
	int32_t                             rc = 0, i;
	const struct firmware               *fw = NULL;
	const char                          *fw_name = NULL;
	struct device                       *dev = &(o_ctrl->pdev->dev);

	fw_name = firmware_name;
	rc = request_firmware(&fw, fw_name, dev);
	if (rc) {
		CAM_ERR(CAM_OIS, "[SEM1217S] Failed to load %s", fw_name);
	} else {
		total_bytes = fw->size;
		ptr = (uint8_t *)fw->data;
		if (read_data) {
			for (i = 0; i < read_length; i++) {
				read_data[i] = *(ptr + i);
			}
		}
	}
	release_firmware(fw);
	return rc;
}

int sem1217s_ois_fw_download(struct cam_ois_ctrl_t *o_ctrl)
{
	uint8_t             txdata[TX_BUFFER_SIZE] = {0};
	uint8_t             rxdata[RX_BUFFER_SIZE] = {0};
	uint16_t            tx_buff_size = 0;
	uint16_t            i = 0;
	uint16_t            chk_index = 0;
	uint16_t            idx = 0;
	uint16_t            check_sum = 0;
	uint32_t            updated_ver = 0;
	uint32_t            new_fw_ver = 0;
	uint32_t            current_fw_ver = 0;
	char                *fw_name_prog = NULL;
	char                name_prog[64] = {0};
	uint8_t             *chk_buffer = NULL;
	uint8_t             *fw_data = NULL;
	int32_t             rc = 0;
	uint32_t            current_fw_ver_temp = 0;

	chk_buffer = vmalloc(APP_FW_SIZE);
	fw_data = vmalloc(APP_FW_SIZE);

	if (NULL == chk_buffer || NULL == fw_data) {
		rc = -ENOMEM;
		goto memory_free;
	}

	/* Get FW Ver from Binary File */
	snprintf(name_prog, 64, "%s.prog", o_ctrl->ois_name);
	fw_name_prog = name_prog;

	rc = load_fw_buff(o_ctrl, fw_name_prog, fw_data, APP_FW_SIZE);

	if (rc) {
		CAM_ERR(CAM_OIS, "[SEM1217S] Failed to load firmware: %s", fw_name_prog);
		goto memory_free;
	}

	new_fw_ver = *(uint32_t *)&fw_data[APP_FW_SIZE - 12];  /* 0x7FF4 ~ 0x7FF7 */
	rc = i2c_read_data(o_ctrl, REG_APP_VER, 4, rxdata);

	if (rc) {

		CAM_ERR(CAM_OIS, "[SEM1217S] Failed to read REG_APP_VER:0x%x", REG_APP_VER);
		rc = -EIO;
		goto memory_free;

	}

	current_fw_ver = *(uint32_t *)rxdata;
	CAM_INFO(CAM_OIS,
		"[SEM1217S] Current firmware version = %d, new firmware version = %d",
		current_fw_ver, new_fw_ver);

	if( ((current_fw_ver < new_fw_ver) && (0 == semco_ois_fw_update_op)) ||
		(current_fw_ver != new_fw_ver) ||
		(FIRMWARE_UPDATE_EVERY_TIMES == semco_ois_fw_update_op)) {

		/* If there is firmware that needs to be updated, turn off OIS and AF */
		if (0 != current_fw_ver) {

			rc = i2c_read_data(o_ctrl, REG_OIS_STS, 1, rxdata); /* Read REG_OIS_STS */

			if (rc) {
				CAM_ERR(CAM_OIS,
					"[SEM1217S] Failed to read REG_OIS_STS:0x%x",
					REG_OIS_STS);
				rc = -EIO;
				goto memory_free;
			}

			if (rxdata[0] != STATE_READY) {
				txdata[0] = OIS_OFF; /* Set OIS_OFF */
				/* Write REG_OIS_CTRL information */
				rc = i2c_write_data(o_ctrl, REG_OIS_CTRL, 1, txdata, 0);
				if (rc) {
					CAM_ERR(CAM_OIS,
						"[SEM1217S] Failed to set REG_OIS_CTRL:0x%x,0x%x",
						REG_OIS_CTRL, txdata[0]);
					rc = -EIO;
					goto memory_free;
				}
			}

			rc = i2c_read_data(o_ctrl, REG_AF_STS, 1, rxdata); /* Read REG_AF_STS */

			if (rc) {
				CAM_ERR(CAM_OIS,
					"[SEM1217S] Failed to read REG_AF_STS:0x%x",
					REG_AF_STS);
				rc = -EIO;
				goto memory_free;
			}

			if (rxdata[0] != STATE_READY) {
				txdata[0] = AF_OFF; /* Set AF_OFF */
 				/* Write REG_AF_CTRL information */
				rc = i2c_write_data(o_ctrl, REG_AF_CTRL, 1, txdata, 0);
				if (rc) {
					CAM_ERR(CAM_OIS,
						"[SEM1217S] Failed to set REG_AF_CTRL:0x%x,0x%x",
						REG_AF_CTRL, txdata[0]);
					rc = -EIO;
					goto memory_free;
				}
			}
		}

		/* PAYLOAD_LEN = Packet size, FW_UPEN = TRUE */
		tx_buff_size = TX_SIZE_256_BYTE;
		switch (tx_buff_size) {
			case TX_SIZE_32_BYTE:
				txdata[0] = FWUP_CTRL_32_SET;
				break;
			case TX_SIZE_64_BYTE:
				txdata[0] = FWUP_CTRL_64_SET;
				break;
			case TX_SIZE_128_BYTE:
				txdata[0] = FWUP_CTRL_128_SET;
				break;
			case TX_SIZE_256_BYTE:
				txdata[0] = FWUP_CTRL_256_SET;
				break;
			default:
				/* Tx data size is not set, warning message */
				break;
		}

		/* Set the firmware version update control register */
		rc = i2c_write_data(o_ctrl, REG_FWUP_CTRL, 1, txdata, 0);
		if (rc) {
			CAM_ERR(CAM_OIS,
                                "[SEM1217S] Failed to set REG_FWUP_CTRL:0x%x,0x%x",
                                REG_AF_CTRL, txdata[0]);
			rc = -EIO;
			goto memory_free;
		}

		msleep(60);

		rc = i2c_read_data(o_ctrl, REG_OIS_STS, 1, rxdata);
		if (rc) {
			CAM_ERR(CAM_OIS, "[SEM1217S] Failed to read REG_OIS_STS:0x%x", REG_OIS_STS);
			rc = -EIO;
			goto memory_free;
		}

		if (STATE_FW_UPDATE != rxdata[0]) {
			CAM_INFO(CAM_OIS, "[SEM1217S] OIS firmware upgrade status check failed");
			rc = -EINVAL;
			goto memory_free;
		}

		rc = i2c_read_data(o_ctrl, REG_APP_VER, 4, rxdata);
		if (rc) {
			CAM_ERR(CAM_OIS, "[SEM1217S] Failed to read REG_APP_VER:0x%x", REG_APP_VER);
			rc = -EIO;
			goto memory_free;
		}

		current_fw_ver_temp = *(uint32_t *)rxdata;

		if (0x00 != current_fw_ver_temp) {
			CAM_ERR(CAM_OIS, "[SEM1217S] OIS firmware version check failed");
			goto memory_free;
		}

		for (i = 0; i < (APP_FW_SIZE / tx_buff_size); i++) {

			CAM_INFO(CAM_OIS, "[SEM1217S] Write REG_DATA_BUF i = %d",i);
			memcpy(&chk_buffer[tx_buff_size * i], &fw_data[idx], tx_buff_size);

			for (chk_index = 0; chk_index < tx_buff_size; chk_index += 2) {
				check_sum += ((chk_buffer[chk_index + 1 + (tx_buff_size * i)] << 8) |
				chk_buffer[chk_index + (tx_buff_size * i)]);
			}

			memcpy(txdata, &fw_data[idx], tx_buff_size);

			rc = i2c_write_data(o_ctrl, REG_DATA_BUF, tx_buff_size, txdata, 0);
			if (rc) {
				CAM_ERR(CAM_OIS,
					"[SEM1217S] Failed to write REG_DATA_BUF:0x%x, index = %d",
					REG_DATA_BUF, i);
				rc = -EIO;
				goto memory_free;
			}
			idx += tx_buff_size;
			msleep(20);

		}

		*(uint16_t *)txdata = check_sum;

		rc = i2c_write_data(o_ctrl, REG_FWUP_CHKSUM, 2, txdata, 0);
		if (rc) {
			CAM_ERR(CAM_OIS,
				"[SEM1217S] Failed to set REG_FWUP_CTRL:0x%x,0x%x",
				REG_AF_CTRL, txdata[0]);
			rc = -EIO;
			goto memory_free;
		}

		msleep(200);

		rc = i2c_read_data(o_ctrl, REG_FWUP_CHKSUM, 2, rxdata);
		if (rc) {
			CAM_ERR(CAM_OIS,
				"[SEM1217S] Failed to read REG_FWUP_CHKSUM:0x%x",
				REG_FWUP_CHKSUM);
			rc = -EIO;
			goto memory_free;
		}

		CAM_INFO(CAM_OIS, "[SEM1217S] REG_FWUP_CHKSUM = 0x%x, 0x%x", rxdata[0], rxdata[1]);

		rc = i2c_read_data(o_ctrl, REG_FWUP_ERR, 1, rxdata);
		if (rc) {
			CAM_ERR(CAM_OIS, "[SEM1217S] Failed to read REG_FWUP_ERR:0x%x", REG_FWUP_ERR);
			rc = -EIO;
			goto memory_free;
		}

		CAM_INFO(CAM_OIS, "[SEM1217S] REG_FWUP_ERR = 0x%x", rxdata[0]);

		if (rxdata[0] != NO_ERROR) {
			CAM_ERR(CAM_OIS, "[SEM1217S] Failed to update firmware");
			rc = -EINVAL;
			goto memory_free;
		}

		txdata[0] = RESET_REQ;
		rc = i2c_write_data(o_ctrl, REG_FWUP_CTRL, 1, txdata, 0);
		if (rc) {
			CAM_ERR(CAM_OIS,
				"[SEM1217S] Failed to set REG_FWUP_CTRL:0x%x,0x%x",
				REG_FWUP_CTRL, txdata[0]);
			rc = -EIO;
			goto memory_free;
		}

		msleep(200);

		rc = i2c_read_data(o_ctrl, REG_APP_VER, 4, rxdata);
		if (rc) {
			CAM_ERR(CAM_OIS, "[SEM1217S] Failed to read REG_APP_VER:0x%x", REG_APP_VER);
			rc = -EIO;
			goto memory_free;
		}

		updated_ver = *(uint32_t *)rxdata;
		CAM_INFO(CAM_OIS,
			"[SEM1217S] firmware version = %d, new firmware version = %d",
			updated_ver, new_fw_ver);

		if (updated_ver != new_fw_ver) {
			CAM_ERR(CAM_OIS, "[SEM1217S] updated_ver != new_fw_ver");
			rc = -EINVAL;
			goto memory_free;
		}

		CAM_INFO(CAM_OIS, "[SEM1217S] Firmware update success");
	}

	ois_fw_version_set(current_fw_ver, o_ctrl->ois_name);

	/*calib ois, download x y offset*/
	if (o_ctrl->is_ois_calib) {
		rc = sem1217s_ois_apply_settings(o_ctrl,
			&o_ctrl->i2c_calib_data);
		if ((rc == -EAGAIN) &&
				(o_ctrl->io_master_info.master_type == CCI_MASTER)) {
			CAM_WARN(CAM_OIS,"[sem1217s] CCI HW is restting: Reapplying calib settings");
			usleep_range(1000, 1010);
			rc = sem1217s_ois_apply_settings(o_ctrl,
				&o_ctrl->i2c_calib_data);
		}
		if (rc) {
			CAM_ERR(CAM_OIS,"[sem1217s] Cannot apply calib data %d flag %d",rc,
				o_ctrl->opcode.customized_ois_flag);
		//	goto pwr_dwn;
		} else {
			CAM_DBG(CAM_OIS, "[sem1217s] apply calib data settings success");
		}
	}

	/*init ois*/
	if (o_ctrl->i2c_fwinit_data.is_settings_valid == 1) {
		rc = sem1217s_ois_apply_settings(o_ctrl,
			&o_ctrl->i2c_fwinit_data);
		if (rc == -EAGAIN) {
			CAM_WARN(CAM_OIS,
				"[sem1217s] CCI HW is restting: Reapplying i2c_fwinit_data settings");
			usleep_range(1000, 1010);
			rc = sem1217s_ois_apply_settings(o_ctrl,
				&o_ctrl->i2c_fwinit_data);
		}
		if (rc) {
			CAM_ERR(CAM_OIS,"Cannot apply i2c_fwinit_data data %d, flag %d",rc,
				o_ctrl->opcode.customized_ois_flag);
		//	goto pwr_dwn;
		} else {
			CAM_DBG(CAM_OIS, "[sem1217s] OIS program Flash settings success");
		}
	}

memory_free:

	if (NULL != chk_buffer) {
		vfree(chk_buffer);
	}

	if (NULL != fw_data) {
		vfree(fw_data);
	}

	return rc;
}
