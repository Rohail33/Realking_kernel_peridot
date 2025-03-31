#include <linux/module.h>
#include <linux/firmware.h>
#include "cam_ois_core.h"
#include "cam_ois_soc.h"
#include "cam_sensor_util.h"
#include "cam_debug_util.h"
#include "cam_res_mgr_api.h"
#include "cam_common_util.h"
#include "cam_packet_util.h"
#include "bu24721.h"

static int bu24721_ois_enable_read_eeprom = 0;
module_param(bu24721_ois_enable_read_eeprom, int, 0644);

int bu24721_ois_pkt_download(struct cam_ois_ctrl_t *o_ctrl)
{
	uint32_t                           reg_data = 0;
	struct cam_sensor_cci_client      *ois_cci_client = o_ctrl->io_master_info.cci_client;
	uint16_t                           total_bytes = 0;
	uint8_t                           *ptr = NULL;
	int32_t                            rc = 0, cnt, i;
	uint32_t                           fw_size;
	const struct firmware             *fw = NULL;
	const char                        *fw_name_prog = NULL;
	const char                        *fw_name_coeff = NULL;
	const char                        *fw_name_mem = NULL;
	char                               name_prog[64] = {0};
	char                               name_coeff[64] = {0};
	char                               name_mem[64] = {0};
	struct device                     *dev = &(o_ctrl->pdev->dev);
	struct cam_sensor_i2c_reg_setting  i2c_reg_setting;
	void                              *vaddr = NULL;
	uint32_t                           gyro_Gain_H = 0;
	uint32_t                           gyro_Gain_L = 0;
	char                              *data_tmp = NULL;

	struct bu24721_ois_i2c_info_t i2c_info = {
		FLASH_I2C_ADDR,
		I2C_FAST_PLUS_MODE
	};

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	//step1.read fw version  F050->POLL->FW  VERSION->POLL
	cam_cci_i2c_write_table(&(o_ctrl->io_master_info), &F050_write);
	cam_cci_i2c_poll(ois_cci_client, F024_SETTING.reg_addr, F024_SETTING.reg_data,
				F024_SETTING.data_mask, CAMERA_SENSOR_I2C_TYPE_BYTE,
				CAMERA_SENSOR_I2C_TYPE_WORD, F024_SETTING.delay);
	cam_cci_i2c_write_table(&(o_ctrl->io_master_info), &F097_write);
	cam_cci_i2c_write_table(&(o_ctrl->io_master_info), &F058_write);
	cam_cci_i2c_write_table(&(o_ctrl->io_master_info), &F050_write);
	cam_cci_i2c_poll(ois_cci_client, F024_SETTING.reg_addr, F024_SETTING.reg_data,
				F024_SETTING.data_mask, CAMERA_SENSOR_I2C_TYPE_BYTE,
				CAMERA_SENSOR_I2C_TYPE_WORD, F024_SETTING.delay);

	cam_cci_i2c_read(ois_cci_client, F01C_SETTING.reg_addr, &reg_data,
			CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_DWORD, TRUE);

	cam_cci_i2c_poll(ois_cci_client, F024_SETTING.reg_addr, F024_SETTING.reg_data,
				F024_SETTING.data_mask, CAMERA_SENSOR_I2C_TYPE_BYTE,
				CAMERA_SENSOR_I2C_TYPE_WORD, F024_SETTING.delay);
	F01C_SETTING.reg_data = o_ctrl->opcode.fw_version;
	CAM_INFO(CAM_OIS, "[BU24721]  read addr 0x%04x  version 0x%04x, flag %d", F01C_SETTING.reg_data,
		reg_data, o_ctrl->opcode.customized_ois_flag);

	/* read calibration data*/
	if (bu24721_ois_enable_read_eeprom == ENABLE_READ_EEPROM)
	{
		data_tmp = kzalloc(CALI_SIZE_BYTE, GFP_KERNEL);
		if (!data_tmp)
			return -ENOMEM;

		i2c_info.slave_addr = FLASH_I2C_ADDR;
		rc = bu24721_ois_update_i2c_info(o_ctrl,  &i2c_info);
		if(rc) {
			CAM_ERR(CAM_OIS, "[BU24721] failed: to update i2c info rc %d", rc );
		}
		cam_camera_cci_i2c_read_seq(ois_cci_client,
			CALI_ADDR,
			data_tmp, CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_BYTE, CALI_SIZE_BYTE);

		for (i=0;i<CALI_SIZE_BYTE;)
		{
			CAM_DBG(CAM_OIS, "[BU24721]addr:0x%04x data: 0x%02x 0x%02x 0x%02x 0x%02x",
					CALI_ADDR+i,
					data_tmp[i], data_tmp[i+1], data_tmp[i+2], data_tmp[i+3]);
			i+=4;
		}

		i2c_info.slave_addr = OIS_I2C_ADDR;
		rc = bu24721_ois_update_i2c_info(o_ctrl,  &i2c_info);
		if(rc) {
			CAM_ERR(CAM_OIS, "[BU24721] failed: to update i2c info rc %d", rc );
		}
		kfree(data_tmp);
		data_tmp=NULL;
	}

	if(F01C_SETTING.reg_data != reg_data) {
		i2c_info.slave_addr = FLASH_I2C_ADDR;
		rc = bu24721_ois_update_i2c_info(o_ctrl,  &i2c_info);
		if(rc) {
			CAM_ERR(CAM_OIS, "[BU24721] failed: to update i2c info rc %d", rc );
		}
		//Step 2 erash flash && program flash
		if (o_ctrl->i2c_init_data.is_settings_valid == 1) {
			rc = bu24721_ois_apply_settings(o_ctrl,
				&o_ctrl->i2c_init_data);
			if (rc == -EAGAIN) {
				CAM_WARN(CAM_OIS,
					"[bu24721] CCI HW is restting: Reapplying i2c_init_data settings");
				usleep_range(1000, 1010);
				rc = bu24721_ois_apply_settings(o_ctrl,
					&o_ctrl->i2c_init_data);
			}
			if (rc) {
				CAM_ERR(CAM_OIS,"[bu24721] Cannot apply i2c_init_data data %d flag %d",rc,
					o_ctrl->opcode.customized_ois_flag);
			//	goto pwr_dwn;
			} else {
				CAM_DBG(CAM_OIS, "[bu24721] OIS program Flash settings success");
			}
		}
		snprintf(name_coeff, 64, "%s.coeff", o_ctrl->ois_name);

		snprintf(name_prog, 64, "%s.prog", o_ctrl->ois_name);

		snprintf(name_mem, 64, "%s.mem", o_ctrl->ois_name);

		/* cast pointer as const pointer*/
		fw_name_prog = name_prog;
		fw_name_coeff = name_coeff;
		fw_name_mem = name_mem;

		/* Load FW */
		rc = request_firmware(&fw, fw_name_prog, dev);
		if (rc) {
		    CAM_ERR(CAM_OIS, "[bu24721] Failed to locate %s", fw_name_prog);
		    return rc;
		}

		total_bytes = fw->size;
		i2c_reg_setting.addr_type = o_ctrl->opcode.fw_addr_type;
		i2c_reg_setting.data_type = CAMERA_SENSOR_I2C_TYPE_DWORD;
		i2c_reg_setting.size = total_bytes;
		i2c_reg_setting.delay = 0;
		fw_size = (sizeof(struct cam_sensor_i2c_reg_array) * total_bytes);
		vaddr = vmalloc(fw_size);
		if (!vaddr) {
			CAM_ERR(CAM_OIS,
				"[bu24721] Failed in allocating i2c_array: fw_size: %u", fw_size);
			release_firmware(fw);
			return -ENOMEM;
		}
		CAM_DBG(CAM_OIS,"[bu24721] %s: fw_size: %u", fw_name_prog, fw_size);
		i2c_reg_setting.reg_setting = (struct cam_sensor_i2c_reg_array *) (vaddr);

		for (cnt = 0, i = 0, ptr = (uint8_t *)fw->data; 4*cnt < total_bytes && 4*i < total_bytes;
			cnt++, ptr+=4, i++) {
			i2c_reg_setting.reg_setting[cnt].reg_addr = o_ctrl->opcode.prog + cnt;
			CAM_DBG(CAM_OIS,"[bu24721 PROG]  addr:0x%x w 0x%x",
				i2c_reg_setting.reg_setting[cnt].reg_addr, makeDw(ptr));
			i2c_reg_setting.reg_setting[cnt].reg_data = makeDw(ptr);
			i2c_reg_setting.reg_setting[cnt].delay = 0;
			i2c_reg_setting.reg_setting[cnt].data_mask = 0;
		}
		i2c_reg_setting.size = cnt;

		rc = camera_io_dev_write(&(o_ctrl->io_master_info),
			&i2c_reg_setting);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "[bu24721] OIS FW(prog) size(%d) download failed. %d",
				total_bytes, rc);
			goto release_firmware;
		}

		vfree(vaddr);
		vaddr = NULL;
		fw_size = 0;
		release_firmware(fw);

		rc = request_firmware(&fw, fw_name_coeff, dev);
		if (rc) {
		    CAM_ERR(CAM_OIS, "[bu24721] Failed to locate %s", fw_name_coeff);
		    return rc;
		}

		total_bytes = fw->size;
		i2c_reg_setting.addr_type = o_ctrl->opcode.fw_addr_type;
		i2c_reg_setting.data_type = CAMERA_SENSOR_I2C_TYPE_DWORD;
		i2c_reg_setting.size = total_bytes;
		i2c_reg_setting.delay = 0;
		fw_size = (sizeof(struct cam_sensor_i2c_reg_array) * total_bytes);
		vaddr = vmalloc(fw_size);
		if (!vaddr) {
		    CAM_ERR(CAM_OIS,
			    "[bu24721] Failed in allocating i2c_array: fw_size: %u", fw_size);
		    release_firmware(fw);
		    return -ENOMEM;
		}

		CAM_DBG(CAM_OIS,"[bu24721] %s: fw_size: %u", fw_name_coeff, fw_size);
		i2c_reg_setting.reg_setting = (struct cam_sensor_i2c_reg_array *) (
		    vaddr);

		for (cnt = 0, i = 0, ptr = (uint8_t *)fw->data; 4*cnt < total_bytes && 4*i < total_bytes;
			    cnt++, ptr+=4, i++) {
			i2c_reg_setting.reg_setting[cnt].reg_addr = o_ctrl->opcode.coeff + cnt;
			CAM_DBG(CAM_OIS,"[bu24721 COEFF]  addr:0x%x w 0x%x",
				i2c_reg_setting.reg_setting[cnt].reg_addr, makeDw(ptr));
			i2c_reg_setting.reg_setting[cnt].reg_data = makeDw(ptr);
			i2c_reg_setting.reg_setting[cnt].delay = 0;
			i2c_reg_setting.reg_setting[cnt].data_mask = 0;
		}
		i2c_reg_setting.size = cnt;

		rc = camera_io_dev_write(&(o_ctrl->io_master_info),
			&i2c_reg_setting);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "[BU24721] OIS FW(coeff) size(%d) download failed rc: %d",
				total_bytes, rc);
			goto release_firmware;
		}
		vfree(vaddr);
		vaddr = NULL;
		fw_size = 0;
		release_firmware(fw);

		rc = request_firmware(&fw, fw_name_mem, dev);
		if (rc) {
			CAM_ERR(CAM_OIS, "[BU24721] Failed to locate %s", fw_name_mem);
			return rc;
		}

		total_bytes = fw->size;
		i2c_reg_setting.addr_type = o_ctrl->opcode.fw_addr_type;
		i2c_reg_setting.data_type = CAMERA_SENSOR_I2C_TYPE_DWORD;
		i2c_reg_setting.size = total_bytes;
		i2c_reg_setting.delay = 0;
		fw_size = (sizeof(struct cam_sensor_i2c_reg_array) * total_bytes);
		vaddr = vmalloc(fw_size);
		if (!vaddr) {
			CAM_ERR(CAM_OIS,
				"[BU24721] Failed in allocating i2c_array: fw_size: %u", fw_size);
			release_firmware(fw);
			return -ENOMEM;
		}

		CAM_DBG(CAM_OIS,"[bu24721] %s: fw_size: %u", fw_name_mem, fw_size);
		i2c_reg_setting.reg_setting = (struct cam_sensor_i2c_reg_array *) (
			vaddr);

		for (cnt = 0, i = 0, ptr = (uint8_t *)fw->data; 4*cnt < total_bytes && 4*i < total_bytes;
			cnt++, ptr+=4, i++) {
			i2c_reg_setting.reg_setting[cnt].reg_addr = o_ctrl->opcode.memory + cnt;
			CAM_DBG(CAM_OIS,"[bu24721 MEM]  addr:0x%x w 0x%x",
				i2c_reg_setting.reg_setting[cnt].reg_addr, makeDw(ptr));
			i2c_reg_setting.reg_setting[cnt].reg_data = makeDw(ptr);
			i2c_reg_setting.reg_setting[cnt].delay = 0;
			i2c_reg_setting.reg_setting[cnt].data_mask = 0;
		}
		i2c_reg_setting.size = cnt;

		rc = camera_io_dev_write(&(o_ctrl->io_master_info),
			&i2c_reg_setting);

		if (rc < 0)
			CAM_ERR(CAM_OIS, "[BU24721] OIS FW(mem) size(%d) download failed rc: %d",
				total_bytes, rc);

		release_firmware:
			vfree(vaddr);
			vaddr = NULL;
			fw_size = 0;
			release_firmware(fw);
		//step 3  go to reset ois
		i2c_info.slave_addr = OIS_I2C_ADDR;
		rc = bu24721_ois_update_i2c_info(o_ctrl,  &i2c_info);
		if(rc) {
			CAM_ERR(CAM_OIS, "[BU24721] failed: to update i2c info rc %d", rc );
		}

		cam_cci_i2c_write_table(&(o_ctrl->io_master_info), &F097_write);
		cam_cci_i2c_write_table(&(o_ctrl->io_master_info), &F058_write);
		cam_cci_i2c_poll(ois_cci_client, F024_SETTING.reg_addr, F024_SETTING.reg_data,
						F024_SETTING.data_mask, CAMERA_SENSOR_I2C_TYPE_BYTE,
						CAMERA_SENSOR_I2C_TYPE_WORD, F024_SETTING.delay);
		//release
		cam_cci_i2c_write_table(&(o_ctrl->io_master_info), &F050_write);
		cam_cci_i2c_poll(ois_cci_client, F024_SETTING.reg_addr, F024_SETTING.reg_data,
						F024_SETTING.data_mask, CAMERA_SENSOR_I2C_TYPE_BYTE,
						CAMERA_SENSOR_I2C_TYPE_WORD, F024_SETTING.delay);
		//read version
		cam_cci_i2c_read(ois_cci_client, F01C_SETTING.reg_addr, &reg_data,
						CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_DWORD, TRUE);
		cam_cci_i2c_poll(ois_cci_client, F024_SETTING.reg_addr, F024_SETTING.reg_data,
						F024_SETTING.data_mask, CAMERA_SENSOR_I2C_TYPE_BYTE,
						CAMERA_SENSOR_I2C_TYPE_WORD, F024_SETTING.delay);
		CAM_INFO(CAM_OIS, "[BU24721] update end, read addr 0x%04x  version 0x%04x, flag %d",
			F01C_SETTING.reg_data, reg_data, o_ctrl->opcode.customized_ois_flag);

	}

	ois_fw_version_set(reg_data, o_ctrl->ois_name);

	/*calib ois, download x y offset*/
	if (o_ctrl->is_ois_calib) {
		rc = bu24721_ois_apply_calib_settings(o_ctrl,
			&o_ctrl->i2c_calib_data);
		if ((rc == -EAGAIN) &&
				(o_ctrl->io_master_info.master_type == CCI_MASTER)) {
			CAM_WARN(CAM_OIS,"[bu24721] CCI HW is restting: Reapplying calib settings");
			usleep_range(1000, 1010);
			rc = bu24721_ois_apply_calib_settings(o_ctrl,
				&o_ctrl->i2c_calib_data);
		}
		if (rc) {
			CAM_ERR(CAM_OIS,"[bu24721] Cannot apply calib data %d flag %d",rc,
				o_ctrl->opcode.customized_ois_flag);
		//	goto pwr_dwn;
		} else {
			CAM_DBG(CAM_OIS, "[bu24721] apply calib data settings success");
		}
	}

	//read gyro gain
	cam_cci_i2c_read(ois_cci_client, 0xF07A, &gyro_Gain_H,
		CAMERA_SENSOR_I2C_TYPE_WORD,CAMERA_SENSOR_I2C_TYPE_WORD,
		FALSE);
	cam_cci_i2c_read(ois_cci_client, 0xF07C, &gyro_Gain_L,
		CAMERA_SENSOR_I2C_TYPE_WORD,CAMERA_SENSOR_I2C_TYPE_WORD,
		FALSE);

	CAM_DBG(CAM_OIS,"[BU24721] default gyrogain x=0x%x y=0x%x",gyro_Gain_H,gyro_Gain_L);

	/*init ois*/
	if (o_ctrl->i2c_fwinit_data.is_settings_valid == 1) {
		rc = bu24721_ois_apply_settings(o_ctrl,
			&o_ctrl->i2c_fwinit_data);
		if (rc == -EAGAIN) {
			CAM_WARN(CAM_OIS,
				"[bu24721] CCI HW is restting: Reapplying i2c_fwinit_data settings");
			usleep_range(1000, 1010);
			rc = bu24721_ois_apply_settings(o_ctrl,
				&o_ctrl->i2c_fwinit_data);
		}
		if (rc) {
			CAM_ERR(CAM_OIS,"Cannot apply i2c_fwinit_data data %d, flag %d",rc,
				o_ctrl->opcode.customized_ois_flag);
		//	goto pwr_dwn;
		} else {
			CAM_DBG(CAM_OIS, "[bu24721] OIS program Flash settings success");
		}
	}
	return rc;
}

//apply setting for bu7421
int bu24721_ois_apply_settings(struct cam_ois_ctrl_t *o_ctrl,
		struct i2c_settings_array *i2c_set)
{
	struct i2c_settings_list *i2c_list;
	int32_t rc = 0;
	uint32_t i, size;
	int32_t j = 0;

	list_for_each_entry(i2c_list,
			&(i2c_set->list_head), list) {
		switch (i2c_list->op_code) {
		case CAM_SENSOR_I2C_WRITE_RANDOM:
		case CAM_SENSOR_I2C_WRITE_BURST:
		case CAM_SENSOR_I2C_WRITE_SEQ: {
			for (j = 0;j < i2c_list->i2c_settings.size;j++) {
				CAM_DBG(CAM_OIS,"[BU24721] ois name %s, request id %d, j=[%d], reg_addr 0x%x, 0x%x", o_ctrl->ois_name,
					i2c_set->request_id, j, i2c_list->i2c_settings.reg_setting[j].reg_addr,
					i2c_list->i2c_settings.reg_setting[j].reg_data);
				trace_cam_i2c_write_log_event("[BU24721]", o_ctrl->ois_name,
					i2c_set->request_id, j, "WRITE", i2c_list->i2c_settings.reg_setting[j].reg_addr,
					i2c_list->i2c_settings.reg_setting[j].reg_data);
			}
			break;
		}
		case CAM_SENSOR_I2C_READ_RANDOM:
		case CAM_SENSOR_I2C_READ_SEQ: {
			for (j = 0;j < i2c_list->i2c_settings.size;j++) {
				CAM_DBG(CAM_OIS, "[BU24721] ois name  %s, request id %d, j=[%d], reg_addr 0x%x, 0x%x",o_ctrl->ois_name,
					i2c_set->request_id, j,i2c_list->i2c_settings.reg_setting[j].reg_addr,
					i2c_list->i2c_settings.reg_setting[j].reg_data);
				trace_cam_i2c_write_log_event("[BU24721]", o_ctrl->ois_name,
					i2c_set->request_id, j, "READ", i2c_list->i2c_settings.reg_setting[j].reg_addr,
					i2c_list->i2c_settings.reg_setting[j].reg_data);
			}
			break;
		}
		case CAM_SENSOR_I2C_POLL: {
			for (j = 0;j < i2c_list->i2c_settings.size;j++) {
				trace_cam_i2c_write_log_event("[BU24721]", o_ctrl->ois_name,
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
					"[BU24721] Failed in Applying i2c wrt settings");
				return rc;
			}
		} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_SEQ) {
			rc = camera_io_dev_write_continuous(
				&(o_ctrl->io_master_info),
				&(i2c_list->i2c_settings),
				CAM_SENSOR_I2C_WRITE_SEQ);
			if (rc < 0) {
				CAM_ERR(CAM_OIS,
					"[BU24721] Failed to seq write I2C settings: %d",
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
						"[BU24721] i2c poll apply setting Fail");
					return rc;
				}
			}
        }
	}
	return rc;
}

int32_t bu24721_ois_update_i2c_info(struct cam_ois_ctrl_t *o_ctrl,
	 struct bu24721_ois_i2c_info_t  *i2c_info)
{
	struct cam_sensor_cci_client  *cci_client =NULL;
	if(o_ctrl->io_master_info.master_type == CCI_MASTER) {
		cci_client = o_ctrl->io_master_info.cci_client;
		if (!cci_client) {
			CAM_ERR(CAM_OIS, " [BU24721]   failed: cci_client %pK",
				cci_client);
			return -EINVAL;
		}
		cci_client->cci_i2c_master = o_ctrl->cci_i2c_master;
		cci_client->sid = (i2c_info->slave_addr) >> 1;
		cci_client->retries = 3;
		cci_client->id_map = 0;
		cci_client->i2c_freq_mode = i2c_info->i2c_freq_mode;
		CAM_INFO(CAM_OIS, " [BU24721]update OIS addr to: 0x%x",
			i2c_info->slave_addr);
	} else if (o_ctrl->io_master_info.master_type == I2C_MASTER) {
		o_ctrl->io_master_info.client->addr = i2c_info->slave_addr;
		CAM_DBG(CAM_OIS, "[BU24721] Slave addr: 0x%x", i2c_info->slave_addr);
	} else if (o_ctrl->io_master_info.master_type == SPI_MASTER) {
		CAM_ERR(CAM_OIS, "[BU24721] Slave addr: 0x%x Freq Mode: %d",
		i2c_info->slave_addr, i2c_info->i2c_freq_mode);
	}
	return 0;
}

int bu24721_ois_apply_calib_settings(struct cam_ois_ctrl_t *o_ctrl,
		struct i2c_settings_array *i2c_set)
{
	struct i2c_settings_list *i2c_list;
	struct cam_sensor_cci_client	*ois_cci_client = o_ctrl->io_master_info.cci_client;
	int32_t rc = 0;
	int32_t j = 0;
	uint32_t buff[4]={0};
	int32_t i = 0;

	list_for_each_entry(i2c_list,
			&(i2c_set->list_head), list) {
		switch (i2c_list->op_code) {
			case CAM_SENSOR_I2C_WRITE_RANDOM: {
				for (j = 0;j < i2c_list->i2c_settings.size;j++) {
					if(i2c_list->i2c_settings.reg_setting[j].reg_addr == GYRO_CALIB){
						buff[i] = (i2c_list->i2c_settings.reg_setting[j].reg_data & 0x0000ffff);
						i++;
						trace_cam_i2c_write_log_event("[BU24721]", o_ctrl->ois_name,
							i2c_set->request_id, j, "WRITE", i2c_list->i2c_settings.reg_setting[j].reg_addr, buff[i]);
					}
				}
				break;
			}
			default:
				break;
		}
	}

	F09C_SETTING[0].reg_data = 0;
	cam_cci_i2c_write_table(&(o_ctrl->io_master_info), &F09C_write);
	cam_cci_i2c_poll(ois_cci_client, F024_SETTING.reg_addr, F024_SETTING.reg_data,
			F024_SETTING.data_mask, CAMERA_SENSOR_I2C_TYPE_BYTE,
			CAMERA_SENSOR_I2C_TYPE_WORD, F024_SETTING.delay);

	F09D_SETTING[0].reg_data = buff[0];
	cam_cci_i2c_write_table(&(o_ctrl->io_master_info), &F09D_write);
	cam_cci_i2c_poll(ois_cci_client, F024_SETTING.reg_addr, F024_SETTING.reg_data,
			F024_SETTING.data_mask, CAMERA_SENSOR_I2C_TYPE_BYTE,
			CAMERA_SENSOR_I2C_TYPE_WORD, F024_SETTING.delay);

	F09C_SETTING[0].reg_data =1;
	cam_cci_i2c_write_table(&(o_ctrl->io_master_info), &F09C_write);
	cam_cci_i2c_poll(ois_cci_client, F024_SETTING.reg_addr, F024_SETTING.reg_data,
			F024_SETTING.data_mask, CAMERA_SENSOR_I2C_TYPE_BYTE,
			CAMERA_SENSOR_I2C_TYPE_WORD, F024_SETTING.delay);

	F09D_SETTING[0].reg_data = buff[1];
	cam_cci_i2c_write_table(&(o_ctrl->io_master_info), &F09D_write);
	cam_cci_i2c_poll(ois_cci_client, F024_SETTING.reg_addr, F024_SETTING.reg_data,
			F024_SETTING.data_mask, CAMERA_SENSOR_I2C_TYPE_BYTE,
			CAMERA_SENSOR_I2C_TYPE_WORD, F024_SETTING.delay);

	CAM_DBG(CAM_OIS,"[BU24721] reg_data x=0x%x y=0x%x",F09D_SETTING[0].reg_data = buff[0], F09D_SETTING[0].reg_data = buff[1]);

	if (rc < 0) {
		CAM_ERR(CAM_OIS,
			"[BU24721] Failed in Applying i2c wrt settings");
	}

	return rc;
}
