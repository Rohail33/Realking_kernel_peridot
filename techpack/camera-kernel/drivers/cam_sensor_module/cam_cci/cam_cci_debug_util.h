// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2023, The Linux Foundation. All rights reserved.
 */

#ifndef _CAM_CCI_DEBUG_UTIL_
#define _CAM_CCI_DEBUG_UTIL_

#include <linux/debugfs.h>
#include "cam_cci_core.h"

#define DEBUGFS_STR_MAX_SIZE            128
#define DEBUGFS_NAME_MAX_SIZE           32

#define MIN_DEV_REG_ADDR                0x0000
#define MAX_DEV_TYPE_WORD               0xFFFF
#define MAX_DEV_TYPE_BYTE               0xFF
#define TIME_MAX_HOURS                  24
#define TIME_SHIFT_HOURS                8

#define PRINT_FIXED_LENGTH_4            4
#define PRINT_TIMESTAMP                 17
#define PRINT_SINGLE_TIME               2
#define PRINT_TIME_MS                   3

#define DEFAULT_MONITOR_INTERVAL_MS     100
#define MAX_DEV_MONITOR_COUNT           10

/**
 * struct cam_cci_debug
 * @name           : Pointer to cci device name
 * @type           : CCI device type
 * @read_data      : Buffer for cci read data
 * @index          : CCI device index in dts
 * @min_reg_addr   : Min register addr to read
 * @max_reg_addr   : Max register addr to read
 * @current_pos    : Read position saved for cci seq read
 * @interval_ms    : Register monitor time interval / ms
 * @parent_entry   : Debugfs entry for cci master
 * @entry          : Debugfs entry for current entry
 * @io_master_info : Info for current cci master
 * @reg_addr_type  : Register address type / byte
 * @reg_data_type  : Register data type / byte
 */
struct cam_cci_debug {
	char 	 *name;
	char     *type;
	uint8_t	 *read_data;
	uint32_t  index;
	uint32_t  min_reg_addr;
	uint32_t  max_reg_addr;
	uint32_t  current_pos;
	uint32_t  interval_ms;
	struct dentry *parent_entry;
	struct dentry *entry;
	struct camera_io_master     *io_master_info;
	enum camera_sensor_i2c_type  reg_addr_type;
	enum camera_sensor_i2c_type  reg_data_type;
};

/**
 * cam_cci_dev_rename_debugfs_entry()
 * @brief       : Rename debugfs when camera acquire device name from umd
 * @cci_debug   : Pointer to cci debugfs data struct
 * @device_name : CCI device name
 *
 * @return      : 0 on success, negative in case of failure
 */
int cam_cci_dev_rename_debugfs_entry(void *cci_debug,
	char *device_name);

/**
 * cam_cci_dev_create_debugfs_entry()
 * @brief          : Create debugfs entry and private data for cci device
 * @dev_name       : Pointer to cci device name
 * @dev_index      : CCI device index in dts
 * @dev_type       : CCI device type
 * @io_master_info : Info for current cci master
 * @cci_i2c_master : CCI master index
 * @cci_debug      : Pointer to cci debugfs data struct
 *
 * @return         : 0 on success, negative in case of failure
 */
int cam_cci_dev_create_debugfs_entry(char *dev_name,
	uint32_t dev_index, char *dev_type,
	struct camera_io_master *io_master_info,
	enum cci_i2c_master_t cci_i2c_master,
	void **cci_debug);

/**
 * cam_cci_dev_remove_debugfs_entry()
 * @brief     : Destroy debugfs entry and private data
 * @cci_debug : Pointer to cci debugfs data struct
 */
void cam_cci_dev_remove_debugfs_entry(void *cci_debug);

#endif /* _CAM_CCI_DEBUG_UTIL_H_ */
