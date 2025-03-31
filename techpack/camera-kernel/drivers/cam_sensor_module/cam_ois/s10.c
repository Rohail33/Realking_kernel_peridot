#include <linux/module.h>
#include <linux/firmware.h>
#include "cam_ois_core.h"
#include "cam_ois_soc.h"
#include "cam_sensor_util.h"
#include "cam_debug_util.h"
#include "cam_res_mgr_api.h"
#include "cam_common_util.h"
#include "cam_packet_util.h"
#include "s10.h"

static int s10_ois_fw_update_op = 0;
module_param(s10_ois_fw_update_op, int, 0644);
static int s10_ois_fw_dump_op = 0;
module_param(s10_ois_fw_dump_op, int, 0644);
static int s10_ois_fw_check = 0;
module_param(s10_ois_fw_check, int, 0644);
static bool     result_checksum = false;
static uint32_t gyrogainx = 0, gyrogainy = 0;

// read program ID
struct cam_sensor_i2c_reg_array SETTING_FW_REV =
{
    //reg_addr, reg_data,   delayms,    data_mask
    0x50BC,     0,          0,          0
};

// OIS status Check
struct cam_sensor_i2c_reg_array SETTING_OIS_STS =
{
    //reg_addr, reg_data,    delayms,    data_mask
    0x5010,     0x00000001,   300,        0
};

//FLC Command Register
struct cam_sensor_i2c_reg_array SETTING_FLCCMD[] =
{
    //reg_addr, reg_data,     delayms,    data_mask
    {0x4004,    0x00000005,    0,          0},
};
struct cam_sensor_i2c_reg_setting  write_FLCCMD =
{
    SETTING_FLCCMD,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_DWORD,
    0,
    NULL,
    0,
};

//OIS Servo Operation
struct cam_sensor_i2c_reg_array SETTING_CTRL[] =
{
    //reg_addr, reg_data,     delayms,    data_mask
    {0x5000,    0x00000001,    5,          0},
};
struct cam_sensor_i2c_reg_setting  write_CTRL =
{
    SETTING_CTRL,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_DWORD,
    0,
    NULL,
    0,
};

//Boot Area Address Register
struct cam_sensor_i2c_reg_array SETTING_FLCBTADR[] =
{
    //reg_addr, reg_data,     delayms,    data_mask
    {0x401C,    0x000000C1,    0,          0},
};
struct cam_sensor_i2c_reg_setting  write_FLCBTADR =
{
    SETTING_FLCBTADR,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_DWORD,
    0,
    NULL,
    0,
};

//Checksum Data Register
struct cam_sensor_i2c_reg_array SETTING_CHECKSUM_DATA[] =
{
    //reg_addr, reg_data,     delayms,    data_mask
    {0x50C4,    0x00000000,    0,          0},
};
struct cam_sensor_i2c_reg_setting  write_CHECKSUM_DATA =
{
    SETTING_CHECKSUM_DATA,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_DWORD,
    0,
    NULL,
    0,
};

//Update Part Data Register
struct cam_sensor_i2c_reg_array SETTING_UPDATEPART_DATA[] =
{
    //reg_addr, reg_data,     delayms,    data_mask
    {0x5038,    0x00000041,    0,          0},
};
struct cam_sensor_i2c_reg_setting  write_UPDATEPART_DATA =
{
    SETTING_UPDATEPART_DATA,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_DWORD,
    0,
    NULL,
    0,
};

//Checksum Control Register
struct cam_sensor_i2c_reg_array SETTING_CHECKSUM_CTRL[] =
{
    //reg_addr, reg_data,     delayms,    data_mask
    {0x50C0,    0x00000001,    0,          0},
};
struct cam_sensor_i2c_reg_setting  write_CHECKSUM_CTRL =
{
    SETTING_CHECKSUM_CTRL,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_DWORD,
    0,
    NULL,
    0,
};

//X-axis Gyro 0 Point Offset Setting Register
struct cam_sensor_i2c_reg_array SETTING_GYROX_CTRL[] =
{
    //reg_addr, reg_data,     delayms,    data_mask
    {0x5814,    0,             0,          0},
};
struct cam_sensor_i2c_reg_setting  write_GYROX_CTRL =
{
    SETTING_GYROX_CTRL,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_DWORD,
    0,
    NULL,
    0,
};

//Y-axis Gyro 0 Point Offset Setting Register
struct cam_sensor_i2c_reg_array SETTING_GYROY_CTRL[] =
{
    //reg_addr, reg_data,     delayms,    data_mask
    {0x5818,    0,             0,          0},
};
struct cam_sensor_i2c_reg_setting  write_GYROY_CTRL =
{
    SETTING_GYROY_CTRL,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_DWORD,
    0,
    NULL,
    0,
};

//FLC Status Register
struct cam_sensor_i2c_reg_array SETTING_FLCST =
{
    //reg_addr, reg_data,     delayms,    data_mask
    0x4050,    0x00000000,    30,          0xFFFFFFFD
};

//OIS Error Register
struct cam_sensor_i2c_reg_array SETTING_OIS_ERR =
{
    //reg_addr, reg_data,     delayms,    data_mask
    0x500C,    0x00000000,    1,          0xFFFFFEFF
};

//CPU stop Register
struct cam_sensor_i2c_reg_array SETTING_CPUSTP[] =
{
    //reg_addr, reg_data,     delayms,    data_mask
    {0x0534,    0x00000000,    0,          0},
};
struct cam_sensor_i2c_reg_setting  write_CPUSTP =
{
    SETTING_CPUSTP,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_DWORD,
    0,
    NULL,
    0,
};

//Gyro Polarity and Orient Setting Register
struct cam_sensor_i2c_reg_array SETTING_GYRO_ORIENT =
{
    //reg_addr, reg_data,     delayms,    data_mask
    0x5808,      0,            0,        0x00010101
};

//Gyro Gain X Setting Register
struct cam_sensor_i2c_reg_array SETTING_GYROGAINX[] =
{
    //reg_addr, reg_data,     delayms,    data_mask
    {0x5828,    0,             0,          0},
};
struct cam_sensor_i2c_reg_setting  write_GYROGAINX_CTRL =
{
    SETTING_GYROGAINX,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_DWORD,
    0,
    NULL,
    0,
};

//Gyro Gain Y Setting Register
struct cam_sensor_i2c_reg_array SETTING_GYROGAINY[] =
{
    //reg_addr, reg_data,     delayms,    data_mask
    {0x582C,    0,             0,          0},
};
struct cam_sensor_i2c_reg_setting  write_GYROGAINY_CTRL =
{
    SETTING_GYROGAINY,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_DWORD,
    0,
    NULL,
    0,
};

static int32_t convert_data_endian(uint32_t *reg_data)
{
    uint32_t temp = 0;
    int32_t  rc   = 0;

    if (!reg_data) {
        CAM_ERR(CAM_OIS, "[S10]Invalid Args");
        return -EINVAL;
    }

    temp = (*reg_data);
    (*reg_data) = ((((temp) & 0xff) << 24) | (((temp) & 0xff00) << 8) | (((temp) & 0xff0000) >> 8) | (((temp) >> 24) & 0xff));

    return rc;
}

static int32_t write_s10_one_register(struct camera_io_master *io_master_info, 
    struct cam_sensor_i2c_reg_setting *write_setting, uint32_t replace_data)
{
    int32_t  rc   = 0;

    if ((!write_setting) || (!io_master_info)) {
        CAM_ERR(CAM_OIS, "[S10]Invalid Args");
        return -EINVAL;
    }

    (write_setting->reg_setting)[0].reg_data = replace_data;
    rc = convert_data_endian(&((write_setting->reg_setting)[0].reg_data));
    if (0 == rc) {
        rc = cam_cci_i2c_write_table((io_master_info), write_setting);
        if (rc) {
            CAM_ERR(CAM_OIS, "[S10] write_s10_one_register fail rc = %d", rc);
        }
    } else {
        CAM_ERR(CAM_OIS, "[S10] convert_data_endian fail rc = %d", rc);
    }

    CAM_DBG(CAM_OIS, "[S10] address 0x%x, data 0x%x, replace_data 0x%x, delay %d",
        (write_setting->reg_setting)[0].reg_addr,
        (write_setting->reg_setting)[0].reg_data,
        replace_data,
        (write_setting->reg_setting)[0].delay);

    return rc;
}

static bool cal_fw_checksum(struct cam_ois_ctrl_t *o_ctrl, uint16_t *fw_data, uint32_t fw_size)
{
    struct cam_sensor_cci_client      *ois_cci_client = NULL;
    int32_t                            rc = 0, i = 0;
    uint16_t                           checksum = 0;

    ois_cci_client = o_ctrl->io_master_info.cci_client;

    //calculate checksum
    for (i = 0; i < (fw_size / 2); i++){
        checksum += fw_data[i];
    }
    CAM_INFO(CAM_OIS,"[S10] calculate checksum =  0x%x", checksum);
    //set checksum
    write_s10_one_register(&(o_ctrl->io_master_info), &write_CHECKSUM_DATA, (uint32_t) checksum);

    //request check checksum
    SETTING_CHECKSUM_CTRL[0].data_mask = 0;
    SETTING_CHECKSUM_CTRL[0].delay     = 0;
    write_s10_one_register(&(o_ctrl->io_master_info), &write_CHECKSUM_CTRL, 0x00000001);

    //poll check checksum
    SETTING_CHECKSUM_CTRL[0].reg_data  = 0x00000000;
    SETTING_CHECKSUM_CTRL[0].data_mask = 0xFFFFFFFE;
    SETTING_CHECKSUM_CTRL[0].delay     = 50;
    rc = cam_cci_i2c_poll_with_32(ois_cci_client, SETTING_CHECKSUM_CTRL[0].reg_addr, SETTING_CHECKSUM_CTRL[0].reg_data,
        SETTING_CHECKSUM_CTRL[0].data_mask, CAMERA_SENSOR_I2C_TYPE_DWORD,
        CAMERA_SENSOR_I2C_TYPE_WORD, SETTING_CHECKSUM_CTRL[0].delay);
    if (rc == 0){
        CAM_DBG(CAM_OIS,"[S10] poll 0x50C0 success");
    } else {
        CAM_ERR(CAM_OIS,"[S10] poll 0x50C0 failed rc = %d", rc);
        return false;
    }

    //Check the checksum
    rc = cam_cci_i2c_poll_with_32(ois_cci_client, SETTING_OIS_ERR.reg_addr, SETTING_OIS_ERR.reg_data,
        SETTING_OIS_ERR.data_mask, CAMERA_SENSOR_I2C_TYPE_DWORD,
        CAMERA_SENSOR_I2C_TYPE_WORD, SETTING_OIS_ERR.delay);
    if (rc == 0){
        CAM_INFO(CAM_OIS,"[S10] Check the checksum success");
        return true;
    } else {
        CAM_ERR(CAM_OIS,"[S10] Check the checksum failed rc = %d", rc);
        return false;
    }
}

static int32_t apply_fw_setting(struct cam_ois_ctrl_t *o_ctrl, uint16_t *fw_data, uint32_t fw_size)
{
    struct cam_sensor_cci_client      *ois_cci_client = NULL;
    int32_t                            rc = 0;

    if (!o_ctrl) {
        CAM_ERR(CAM_OIS, "[S10]Invalid Args");
        return -EINVAL;
    }

    ois_cci_client = o_ctrl->io_master_info.cci_client;

    CAM_DBG(CAM_OIS, "[S10]apply kernel firmware setting.");
    //execute NOP
    write_s10_one_register(&(o_ctrl->io_master_info), &write_FLCCMD, 0x00000000);
    //set boot address
    write_s10_one_register(&(o_ctrl->io_master_info), &write_FLCBTADR, 0x000000C1);
    //restart cpu
    write_s10_one_register(&(o_ctrl->io_master_info), &write_CPUSTP, 0x00000000);

    usleep_range(10000, 10010);
    //poll ois status is IDLE
    rc = cam_cci_i2c_poll_with_32(ois_cci_client, SETTING_OIS_STS.reg_addr, SETTING_OIS_STS.reg_data,
            SETTING_OIS_STS.data_mask, CAMERA_SENSOR_I2C_TYPE_DWORD,
            CAMERA_SENSOR_I2C_TYPE_WORD, SETTING_OIS_STS.delay);
    if (rc == 0){
        CAM_DBG(CAM_OIS,"[S10] poll 0x5010 success");
    } else {
        CAM_ERR(CAM_OIS,"[S10] poll 0x5010 failed rc = %d", rc);
    }

    if(1 == rc){
        return -rc;
    }

    result_checksum = cal_fw_checksum(o_ctrl, fw_data, fw_size);

    if ((0 == rc) && (result_checksum)){
        CAM_INFO(CAM_OIS, "[S10]apply kernel firmware setting success.");
    } else {
        CAM_ERR(CAM_OIS, "[S10]apply kernel firmware setting failed.");
    }

    //set update part data
    write_s10_one_register(&(o_ctrl->io_master_info), &write_UPDATEPART_DATA, 0x00000041);
    usleep_range(190000, 190010);

    return rc;
}

static int s10_ois_apply_calib_settings(struct cam_ois_ctrl_t *o_ctrl,
                struct i2c_settings_array *i2c_set)
{
    struct i2c_settings_list *i2c_list;
    struct cam_sensor_cci_client   *ois_cci_client = o_ctrl->io_master_info.cci_client;
    int32_t rc = 0;
    int32_t j = 0;
    uint32_t buff[4]={0};
    int32_t i = 0;

    cam_cci_i2c_read_with_little_endian(ois_cci_client, SETTING_GYRO_ORIENT.reg_addr,
            &(SETTING_GYRO_ORIENT.reg_data), CAMERA_SENSOR_I2C_TYPE_WORD,
            CAMERA_SENSOR_I2C_TYPE_DWORD, FALSE);

    CAM_INFO(CAM_OIS,"[S10] GYRO_ORIENT is %d flash gyrogainx 0x%x, gyrogainy 0x%x", (SETTING_GYRO_ORIENT.reg_data & SETTING_GYRO_ORIENT.data_mask), gyrogainx, gyrogainy);

    list_for_each_entry(i2c_list, &(i2c_set->list_head), list) {
            switch (i2c_list->op_code) {
            case CAM_SENSOR_I2C_WRITE_RANDOM: {
                    for (j = 0;j < i2c_list->i2c_settings.size;j++) {
                        if(i2c_list->i2c_settings.reg_setting[j].reg_addr == GYRO_X){
                            buff[i] = (i2c_list->i2c_settings.reg_setting[j].reg_data);
                            i++;
                        } else if (i2c_list->i2c_settings.reg_setting[j].reg_addr == GYRO_Y){
                            buff[i] = (i2c_list->i2c_settings.reg_setting[j].reg_data);
                            i++;
                        } else if (i2c_list->i2c_settings.reg_setting[j].reg_addr == GYRO_GAINX){
                            buff[i] = (i2c_list->i2c_settings.reg_setting[j].reg_data);
                            i++;
                        } else if (i2c_list->i2c_settings.reg_setting[j].reg_addr == GYRO_GAINY){
                            buff[i] = (i2c_list->i2c_settings.reg_setting[j].reg_data);
                            i++;
                        }
                        trace_cam_i2c_write_log_event("[S10]", o_ctrl->ois_name,
                            i2c_set->request_id, j, "WRITE", i2c_list->i2c_settings.reg_setting[j].reg_addr,
                            i2c_list->i2c_settings.reg_setting[j].reg_data);
                    }
                    break;
            }
            default:
                    break;
            }
    }

    write_s10_one_register(&(o_ctrl->io_master_info), &write_GYROX_CTRL, buff[0]);
    write_s10_one_register(&(o_ctrl->io_master_info), &write_GYROY_CTRL, buff[1]);
    if ((buff[2] != gyrogainx) || (buff[3] != gyrogainy)) {
        write_s10_one_register(&(o_ctrl->io_master_info), &write_GYROGAINX_CTRL, buff[2]);
        write_s10_one_register(&(o_ctrl->io_master_info), &write_GYROGAINY_CTRL, buff[3]);
        CAM_INFO(CAM_OIS,"[S10] using eeprom gyro gainx=0x%x gyroy=0x%x", SETTING_GYROGAINX[0].reg_data, SETTING_GYROGAINY[0].reg_data);
    }

    CAM_INFO(CAM_OIS,"[S10] reg_data x=0x%x y=0x%x", SETTING_GYROX_CTRL[0].reg_data, SETTING_GYROY_CTRL[0].reg_data);

    if (rc < 0) {
            CAM_ERR(CAM_OIS,
                    "[S10] Failed in Applying i2c wrt settings");
    }

    return rc;
}

static int32_t write_firmware_to_flcdata(struct cam_ois_ctrl_t *o_ctrl,
    struct cam_sensor_i2c_reg_setting *i2c_reg_setting)
{
    struct cam_sensor_cci_client      *ois_cci_client = NULL;
    int32_t                            rc = 0, rc1 = 0;

    if ((!o_ctrl) ||(!i2c_reg_setting)) {
        CAM_ERR(CAM_OIS, "[S10]Invalid Args");
        return -EINVAL;
    }

    ois_cci_client = o_ctrl->io_master_info.cci_client;
    //set write command
    write_s10_one_register(&(o_ctrl->io_master_info), &write_FLCCMD, 0x00000002);
    //write data
    rc = camera_io_dev_write_continuous(&(o_ctrl->io_master_info),
        i2c_reg_setting, CAM_SENSOR_I2C_WRITE_SEQ);

    //poll flash busy
    rc1 = cam_cci_i2c_poll_with_32(ois_cci_client, SETTING_FLCST.reg_addr, SETTING_FLCST.reg_data,
        SETTING_FLCST.data_mask, CAMERA_SENSOR_I2C_TYPE_DWORD,
        CAMERA_SENSOR_I2C_TYPE_WORD, SETTING_FLCST.delay);
    if (rc1 == 0){
        CAM_DBG(CAM_OIS,"[S10] poll 0x4050 success");
    } else {
        CAM_ERR(CAM_OIS,"[S10] poll 0x4050 failed rc = %d", rc);
    }

    return rc;
}

//apply setting for s10
static int32_t s10_ois_apply_settings(struct cam_ois_ctrl_t *o_ctrl,
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
                convert_data_endian(&(i2c_list->i2c_settings.reg_setting[j].reg_data));
                CAM_DBG(CAM_OIS, "[S10] ois name  %s, request id %d, j=[%d], reg_addr 0x%x, 0x%x delay %d",o_ctrl->ois_name,
                    i2c_set->request_id, j,i2c_list->i2c_settings.reg_setting[j].reg_addr,
                    i2c_list->i2c_settings.reg_setting[j].reg_data, i2c_list->i2c_settings.reg_setting[j].delay);
                trace_cam_i2c_write_log_event("[S10]", o_ctrl->ois_name,
                    i2c_set->request_id, j, "WRITE", i2c_list->i2c_settings.reg_setting[j].reg_addr,
                    i2c_list->i2c_settings.reg_setting[j].reg_data);
            }
            break;
        }
        case CAM_SENSOR_I2C_POLL: {
            for (j = 0;j < i2c_list->i2c_settings.size;j++) {
                if (FLCST_ADDR == (i2c_list->i2c_settings.reg_setting[j].reg_addr)){
                    i2c_list->i2c_settings.reg_setting[j].data_mask = 0xFFFFFFFD;
                }
                trace_cam_i2c_write_log_event("[S10]", o_ctrl->ois_name,
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
                    "[S10] Failed in Applying i2c wrt settings");
                return rc;
            } else{
                CAM_DBG(CAM_OIS, "[S10] success write!");
            }
        } else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_SEQ) {
            rc = camera_io_dev_write_continuous(
                &(o_ctrl->io_master_info),
                &(i2c_list->i2c_settings),
                CAM_SENSOR_I2C_WRITE_SEQ);
            if (rc < 0) {
                CAM_ERR(CAM_OIS,
                    "[S10] Failed to seq write I2C settings: %d",
                    rc);
                return rc;
            } else {
                CAM_DBG(CAM_OIS,
                    "[S10] Success to seq write I2C settings");
            }
        } else if (i2c_list->op_code == CAM_SENSOR_I2C_POLL) {
            size = i2c_list->i2c_settings.size;
            for (i = 0; i < size; i++) {
                rc = cam_cci_i2c_poll_with_32(
                (o_ctrl->io_master_info.cci_client),
                i2c_list->i2c_settings.reg_setting[i].reg_addr,
                i2c_list->i2c_settings.reg_setting[i].reg_data,
                i2c_list->i2c_settings.reg_setting[i].data_mask,
                i2c_list->i2c_settings.data_type,
                i2c_list->i2c_settings.addr_type,
                i2c_list->i2c_settings.reg_setting[i].delay);
                if (rc < 0) {
                    CAM_ERR(CAM_OIS,
                        "[S10] i2c poll apply setting Fail read_addr 0x%x", i2c_list->i2c_settings.reg_setting[i].reg_addr);
                    return rc;
                } else if (I2C_COMPARE_MISMATCH == rc) {
                    rc = 0;
                    CAM_ERR(CAM_OIS, "i2c poll mismatch read_addr 0x%x",
                        i2c_list->i2c_settings.reg_setting[i].reg_addr);
                } else {
                    CAM_DBG(CAM_OIS,
                        "[S10] i2c poll success rc %d !read_addr 0x%x",
                        rc, i2c_list->i2c_settings.reg_setting[i].reg_addr);
                }
            }
        }
    }
    return rc;
}

static int32_t cam_s10_read_flash(struct cam_ois_ctrl_t *o_ctrl)
{
    struct cam_sensor_cci_client      *ois_cci_client = NULL;
    uint32_t                          addr = 0, data = 0;
    int32_t                           rc = 0;
    bool                              NG_judge = false;

    if (!o_ctrl) {
        CAM_ERR(CAM_OIS, "[S10]Invalid Args");
        return -EINVAL;
    }

    ois_cci_client = o_ctrl->io_master_info.cci_client;

    for (addr = START_DUMP_ADDR; addr < END_DUMP_ADDR; addr = addr + DL_BYTES) {
        rc = cam_cci_i2c_read_with_little_endian(ois_cci_client, addr,
            &data, CAMERA_SENSOR_I2C_TYPE_WORD,
            CAMERA_SENSOR_I2C_TYPE_DWORD, FALSE);
        if (rc) {
            CAM_WARN(CAM_OIS,
                "[S10] cam_s10_read_flash failed");
            return rc;
        }

        if (((THERMISTOR == addr) || (RESISTANCE0 == addr) || (RESISTANCE2 == addr)) &&
            (REG_ZERO == data)) {
                CAM_ERR(CAM_OIS, "[S10]Thermistor or resistance error addr:0x%04x data: 0x%08x", addr, data);
                NG_judge = true;
        }

        if ((WIRESTATUS == addr) && (REG_ZERO != data)) {
            CAM_ERR(CAM_OIS, "[S10]OpenShort error addr:0x%04x data: 0x%08x", addr, data);
            NG_judge = true;
        }

        if (GYRO_GAINX == addr) {
            gyrogainx = data;
        } else if (GYRO_GAINY == addr) {
            gyrogainy = data;
        }

        CAM_DBG(CAM_OIS, "[S10]addr:0x%04x data: 0x%08x", addr, data);
    }
    CAM_INFO(CAM_OIS, "[S10]NG_judge %d", NG_judge);

    return rc;
}

static int32_t cam_s10_download(struct cam_ois_ctrl_t *o_ctrl)
{
    uint16_t                           total_dwords = 0;
    uint16_t                           each_download_size = 0;
    uint8_t                           *ptr = NULL;
    int32_t                            rc = 0, cnt, i;
    uint32_t                           fw_size;
    const struct firmware             *fw = NULL;
    const char                        *fw_name_prog = NULL;
    uint32_t                           curr_fw_version = 0;
    char                               name_prog[32] = {0};
    struct device                     *dev = &(o_ctrl->pdev->dev);
    struct cam_sensor_i2c_reg_setting  i2c_reg_setting;
    void                              *vaddr = NULL;
    struct cam_sensor_cci_client      *ois_cci_client = NULL;
    struct timespec64                  ts1, ts2; // xiaomi add
    long                               microsec0 = 0, microsec1 = 0, microsec2 = 0; // xiaomi add
    bool                               skip_update = false;


    if (!o_ctrl) {
        CAM_ERR(CAM_OIS, "[S10]Invalid Args");
        return -EINVAL;
    }

    ois_cci_client = o_ctrl->io_master_info.cci_client;

    snprintf(name_prog, 32, "%s.prog", o_ctrl->ois_name);
    /* cast pointer as const pointer*/
    fw_name_prog = name_prog;
    CAM_GET_TIMESTAMP(ts1);
    //poll ois status is IDLE
    rc = cam_cci_i2c_poll_with_32(ois_cci_client, SETTING_OIS_STS.reg_addr, SETTING_OIS_STS.reg_data,
            SETTING_OIS_STS.data_mask, CAMERA_SENSOR_I2C_TYPE_DWORD,
            CAMERA_SENSOR_I2C_TYPE_WORD, SETTING_OIS_STS.delay);
    if (rc == 0){
        CAM_DBG(CAM_OIS,"[S10] poll 0x5010 success");
    } else {
        result_checksum = false;
        CAM_ERR(CAM_OIS,"[S10] poll 0x5010 failed rc = %d", rc);
    }
    CAM_GET_TIMESTAMP(ts2);
    CAM_GET_TIMESTAMP_DIFF_IN_MICRO(ts1, ts2, microsec0);

    /* 1. Check if the firmware needs to be updated */
    rc = cam_cci_i2c_read_with_little_endian(ois_cci_client, SETTING_FW_REV.reg_addr,
        &curr_fw_version, CAMERA_SENSOR_I2C_TYPE_WORD,
        CAMERA_SENSOR_I2C_TYPE_DWORD, TRUE);
    CAM_GET_TIMESTAMP(ts2);
    CAM_GET_TIMESTAMP_DIFF_IN_MICRO(ts1, ts2, microsec1);

    if ((false == result_checksum) || (FIRMWARE_FLASH_DUMP_SWITCH == s10_ois_fw_check)){
        rc = request_firmware(&fw, fw_name_prog, dev);
        if (rc) {
            CAM_ERR(CAM_OIS, "[S10]Failed to locate %s", fw_name_prog);
            release_firmware(fw);
            return rc;
        }
        result_checksum = cal_fw_checksum(o_ctrl, (uint16_t *)fw->data, (uint32_t )fw->size);
        CAM_INFO(CAM_OIS, "[S10] need do cal_fw_checksum, result_checksum %d,s10_ois_fw_check %d ",
                result_checksum, s10_ois_fw_check);
        //set ois on
        write_s10_one_register(&(o_ctrl->io_master_info), &write_CTRL, 0x00000001);

        if (cam_s10_read_flash(o_ctrl)){
            CAM_WARN(CAM_OIS,
                "[S10] read flash failed! rc:%d", rc);
        } else {
            CAM_INFO(CAM_OIS, "[S10] read flash success!");
        }
        //set ois off
        write_s10_one_register(&(o_ctrl->io_master_info), &write_CTRL, 0x00000000);
        release_firmware(fw);
    }
    CAM_GET_TIMESTAMP(ts2);
    CAM_GET_TIMESTAMP_DIFF_IN_MICRO(ts1, ts2, microsec2);
    CAM_DBG(CAM_OIS, "[S10]microsec %d -- %d -- %d", microsec0, microsec1, microsec2);

    if ((SKIP_VERSION == curr_fw_version) && ((o_ctrl->opcode.fw_version) < SKIP_VERSION)) {
        skip_update = true;
        result_checksum = true;
    }

    CAM_INFO(CAM_OIS, "[S10]read FW_ver ID:0x%x, current FW_ves ID:0x%x, result_checksum %d, skip_update %d",
            curr_fw_version,
            o_ctrl->opcode.fw_version, result_checksum, skip_update);

    if ((((curr_fw_version != o_ctrl->opcode.fw_version) || (false == result_checksum))
            && (0 == s10_ois_fw_update_op) && (false == skip_update)) ||
         (FIRMWARE_UPDATE_EVERY_TIMES == s10_ois_fw_update_op)) {
        result_checksum = false;

        /* 2. erash flash && program flash*/
        if (o_ctrl->i2c_init_data.is_settings_valid == 1) {
            //poll ois status is IDLE
            rc = cam_cci_i2c_poll_with_32(ois_cci_client, SETTING_OIS_STS.reg_addr, SETTING_OIS_STS.reg_data,
                SETTING_OIS_STS.data_mask, CAMERA_SENSOR_I2C_TYPE_DWORD,
                CAMERA_SENSOR_I2C_TYPE_WORD, SETTING_OIS_STS.delay);
            if (rc == 0){
                CAM_INFO(CAM_OIS,"[S10] poll 0x5010 success");
            } else {
                CAM_ERR(CAM_OIS,"[S10] poll 0x5010 failed rc = %d", rc);
            }
            //apply init settings
            rc = s10_ois_apply_settings(o_ctrl,
                &o_ctrl->i2c_init_data);
            if (rc == -EAGAIN) {
                CAM_WARN(CAM_OIS,
                    "[S10] CCI HW is restting: Reapplying i2c_init_data settings");
                usleep_range(1000, 1010);
                rc = s10_ois_apply_settings(o_ctrl,
                    &o_ctrl->i2c_init_data);
            }
            if (rc) {
                CAM_ERR(CAM_OIS,"[S10] Cannot apply i2c_init_data data %d flag %d",rc,
                    o_ctrl->opcode.customized_ois_flag);
                return rc;
            } else {
                CAM_INFO(CAM_OIS, "[S10] apply i2c_init_data data success");
            }
        }

        /*3. Load FW and DL*/
        rc = request_firmware(&fw, fw_name_prog, dev);
        if (rc) {
            CAM_ERR(CAM_OIS, "[S10]Failed to locate %s", fw_name_prog);
            return rc;
        }

        total_dwords = fw->size/DL_BYTES; // 4 bytes to write eeprom
        each_download_size = DL_FLCDATA_SIZE/DL_BYTES;
        i2c_reg_setting.addr_type = o_ctrl->opcode.fw_addr_type;
        i2c_reg_setting.data_type = CAMERA_SENSOR_I2C_TYPE_DWORD;
        i2c_reg_setting.size = each_download_size;
        i2c_reg_setting.delay = DL_DELAY_MS;
        fw_size = (sizeof(struct cam_sensor_i2c_reg_array) * each_download_size);
        vaddr = vmalloc(fw_size);
        if (!vaddr) {
            CAM_ERR(CAM_OIS,
                "[S10]Failed in allocating i2c_array: fw_size: %u", fw_size);
            release_firmware(fw);
            return -ENOMEM;
        }

        CAM_DBG(CAM_OIS, "[S10]FW prog size(dword):%d.", total_dwords);

        i2c_reg_setting.reg_setting = (struct cam_sensor_i2c_reg_array *) (
            vaddr);

        for (i = 0, ptr = (uint8_t *)fw->data; i < total_dwords;) {
            for (cnt = 0; cnt < each_download_size && i < total_dwords;
                cnt++, i++, ptr+=4) {
                    i2c_reg_setting.reg_setting[cnt].reg_addr = FLCDATA_ADDR + cnt * 4;
                    i2c_reg_setting.reg_setting[cnt].reg_data = ptr[0] << 24 |
                        ptr[1] << 16 | ptr[2] << 8 | ptr[3];
                    i2c_reg_setting.reg_setting[cnt].delay = 0;
                    i2c_reg_setting.reg_setting[cnt].data_mask = 0;
                    CAM_DBG(CAM_OIS, "[S10]addr:0x%04x data: 0x%02x 0x%02x 0x%02x 0x%02x reg_data 0x%x",
                            i2c_reg_setting.reg_setting[cnt].reg_addr,
                            ptr[0], ptr[1], ptr[2], ptr[3], i2c_reg_setting.reg_setting[cnt].reg_data);
            }
            i2c_reg_setting.size = cnt;

            rc = write_firmware_to_flcdata(o_ctrl, &i2c_reg_setting);
            if (rc < 0) {
                CAM_ERR(CAM_OIS, "[S10]OIS FW(prog) size(%d) download failed. %d",
                    total_dwords, rc);
                goto release_firmware;
            } else {
                CAM_DBG(CAM_OIS, "[S10] write_firmware_to_flcdata success rc = %d", rc);
            }
        }

        /* 4. Check whether the download is successful*/
        apply_fw_setting(o_ctrl, (uint16_t *)fw->data, (uint32_t )fw->size);
        release_firmware:
            vfree(vaddr);
            vaddr = NULL;
            fw_size = 0;
            release_firmware(fw);
        //read fw version
        cam_cci_i2c_read_with_little_endian(ois_cci_client, SETTING_FW_REV.reg_addr,
            &curr_fw_version, CAMERA_SENSOR_I2C_TYPE_WORD,
            CAMERA_SENSOR_I2C_TYPE_DWORD, FALSE);

        CAM_INFO(CAM_OIS, "[S10]after DL, read program ID:0x%x, current ID:0x%x",
                curr_fw_version,
                o_ctrl->opcode.fw_version);

        if ((false == skip_update) && (curr_fw_version != o_ctrl->opcode.fw_version) &&
            (0 == s10_ois_fw_update_op)){
            rc = -EINVAL;
        }
    }

    ois_fw_version_set(curr_fw_version, o_ctrl->ois_name);

    if (FIRMWARE_FLASH_DUMP_SWITCH == s10_ois_fw_dump_op){
        if (cam_s10_read_flash(o_ctrl)){
            CAM_WARN(CAM_OIS,
                "[S10] read flash failed! rc:%d", rc);
        } else {
            CAM_INFO(CAM_OIS, "[S10] read flash success!");
        }
    }

    return rc;
}

int s10_ois_pkt_download(struct cam_ois_ctrl_t *o_ctrl)
{
    int rc = 0;
    int i;

    if (!o_ctrl) {
        CAM_ERR(CAM_OIS, "[S10]Invalid Args");
        return -EINVAL;
    }

    for (i = 0; i<FIRMWARE_UPDATE_RETRY_TIMES; i++)
    {
        rc = cam_s10_download(o_ctrl);
        if (rc){
            CAM_WARN(CAM_OIS,
                    "[S10] fw update failed retry:%d/%d",
                    i,
                    FIRMWARE_UPDATE_RETRY_TIMES);
        }else{
            CAM_INFO(CAM_OIS, "[S10] FW update success!");
            break;
        }
    }

    if (rc)
    {
        CAM_ERR(CAM_OIS, "fw down load failed!");
        goto pwr_dwn;
    }
    else
    {
        /*calib ois, download x y offset*/
        if (o_ctrl->is_ois_calib) {
            rc = s10_ois_apply_calib_settings(o_ctrl,
                &o_ctrl->i2c_calib_data);
            if ((rc == -EAGAIN) &&
                    (o_ctrl->io_master_info.master_type == CCI_MASTER)) {
                CAM_WARN(CAM_OIS,"[S10] CCI HW is restting: Reapplying calib settings");
                usleep_range(1000, 1010);
                rc = s10_ois_apply_calib_settings(o_ctrl,
                    &o_ctrl->i2c_calib_data);
            }
            if (rc) {
                CAM_ERR(CAM_OIS,"[S10] Cannot apply calib data %d flag %d",rc,
                    o_ctrl->opcode.customized_ois_flag);
                goto pwr_dwn;
            } else {
                CAM_INFO(CAM_OIS, "[S10] apply calib data settings success");
            }
        }

        /*fwinit ois*/
        if (o_ctrl->i2c_fwinit_data.is_settings_valid == 1) {
            rc = s10_ois_apply_settings(o_ctrl,
                &o_ctrl->i2c_fwinit_data);
            if (rc == -EAGAIN) {
                CAM_WARN(CAM_OIS,
                    "[S10] CCI HW is restting: Reapplying i2c_fwinit_data settings");
                usleep_range(1000, 1010);
                rc = s10_ois_apply_settings(o_ctrl,
                    &o_ctrl->i2c_fwinit_data);
            }
            if (rc) {
                CAM_ERR(CAM_OIS,"Cannot apply i2c_fwinit_data data %d, flag %d",rc,
                    o_ctrl->opcode.customized_ois_flag);
                goto pwr_dwn;
            } else {
                CAM_INFO(CAM_OIS, "[S10] apply i2c_fwinit_data success");
            }
        }
    }

    if (!rc)
        return rc;
pwr_dwn:
    CAM_ERR(CAM_OIS, "OIS init fail! rc=%d", rc);
    return rc;
}