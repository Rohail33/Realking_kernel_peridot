#ifndef _CAM_BU24721_H_
#define _CAM_BU24721_H_

#define FLASH_I2C_ADDR       0XA0
#define OIS_I2C_ADDR         0X7C
#define GYRO_CHANNEL         0xF09C
#define GYRO_CALIB           0xF09D

#define CALI_SIZE_BYTE       64
#define CALI_ADDR            0x3F70

#define ENABLE_READ_EEPROM   1

inline uint32_t makeDw(uint8_t *ptr){
return (*(ptr+3)|((*(ptr+2))<<8)|\
        ((*(ptr+1))<<16) | (*(ptr) <<24));
}

struct bu24721_ois_i2c_info_t {
	uint16_t slave_addr;
	uint8_t  i2c_freq_mode;
};
struct cam_sensor_i2c_reg_array F050_SETTING[] =
{
    //addr,     data,   delay,    data_mask
    {0xf050, 0, 0, 0},
};
struct cam_sensor_i2c_reg_setting  F050_write =
{
    F050_SETTING,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_BYTE,
    1,
    NULL,
    0,
};

//poll setting
struct cam_sensor_i2c_reg_array F024_SETTING =
{
    //addr,     data,   delay,    data_mask
    0xF024,    0x01,    100,   0
};

//FW version
struct cam_sensor_i2c_reg_array F01C_SETTING =
{
    //addr,     data,   delay,    data_mask
    0xF01C,    0x1A8F006B,    100,   0
};

//flash reset1  f097
struct cam_sensor_i2c_reg_array F097_SETTING[] =
{
    //addr,     data,   delay,    data_mask
    {0xf097, 0, 0, 0},
};
struct cam_sensor_i2c_reg_setting  F097_write =
{
    F097_SETTING,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_BYTE,
    1,
    NULL,
    0,
};

//flash reset2  0xf058
struct cam_sensor_i2c_reg_array F058_SETTING[] =
{
    //addr,     data,   delay,    data_mask
    {0xf058, 0, 0 , 0},
};
struct cam_sensor_i2c_reg_setting  F058_write =
{
    F058_SETTING,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_BYTE,
    8,
    NULL,
    0,
};

//gyro offset channel   0xf09c
struct cam_sensor_i2c_reg_array F09C_SETTING[] =
{
    //addr,     data,   delay,    data_mask
    {0xf09c, 0, 0, 0},
};
struct cam_sensor_i2c_reg_setting  F09C_write =
{
    F09C_SETTING,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_BYTE,
    0,
    NULL,
    0,
};

//gyro offset val  0xf09d
struct cam_sensor_i2c_reg_array F09D_SETTING[] =
{
    //addr,     data,   delay,    data_mask
    {0xf09d, 0,0 , 0},
};
struct cam_sensor_i2c_reg_setting  F09D_write =
{
    F09D_SETTING,
    1,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    CAMERA_SENSOR_I2C_TYPE_WORD,
    0,
    NULL,
    0,
};
int32_t bu24721_ois_update_i2c_info(struct cam_ois_ctrl_t *o_ctrl,
		struct bu24721_ois_i2c_info_t  *i2c_info);

int bu24721_ois_apply_settings(struct cam_ois_ctrl_t *o_ctrl,
		struct i2c_settings_array *i2c_set);

int bu24721_ois_pkt_download(struct cam_ois_ctrl_t *o_ctrl);

int bu24721_ois_apply_calib_settings(struct cam_ois_ctrl_t *o_ctrl,
		struct i2c_settings_array *i2c_set);
#endif
