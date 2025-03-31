/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef _CAM_OIS_CORE_H_
#define _CAM_OIS_CORE_H_

#include <linux/cma.h>
#include "cam_ois_dev.h"
#include "cam_req_mgr_dev.h" // xiaomi add

#define OIS_NAME_LEN 64
#define OIS_ENDIANNESS_MASK_FW              0x0F
#define OIS_ENDIANNESS_MASK_INPUTPARAM      0xF0
#define INVALID_FW_ADDR                     0xFFFF

struct ois_firmware_info
{
	char name[OIS_NAME_LEN];
	uint64_t fw_version;
	bool is_valid;
};

/**
 * @power_info: power setting info to control the power
 *
 * This API construct the default ois power setting.
 *
 * @return Status of operation. Negative in case of error. Zero otherwise.
 */
int32_t cam_ois_construct_default_power_setting(
	struct cam_sensor_power_ctrl_t *power_info);


int cam_ois_driver_cmd(struct cam_ois_ctrl_t *e_ctrl, void *arg);

/**
 * @o_ctrl: OIS ctrl structure
 *
 * This API handles the shutdown ioctl/close
 */
void cam_ois_shutdown(struct cam_ois_ctrl_t *o_ctrl);

struct completion *cam_ois_get_i3c_completion(uint32_t index);

/*xiaomi add begin*/
/**
 * @o_ctrl: OIS ctrl structure
 *
 * This API init ois parklens info of o_ctrl
 */
int32_t init_ois_parklens_info(struct cam_ois_ctrl_t *o_ctrl);

/**
 * @o_ctrl: OIS ctrl structure
 *
 * This API deinit parklens info of o_ctrl
 */
int32_t ois_deinit_parklens_info(struct cam_ois_ctrl_t *o_ctrl);

/**
 * @o_ctrl: OIS ctrl structure
 *
 * This API query whether parklens power down or not
 */
bool ois_parklens_power_down(struct cam_ois_ctrl_t *o_ctrl);

/**
 * @o_ctrl: OIS ctrl structure
 *
 * This API trigger a thread to do parklens
 */
int32_t ois_parklens_thread_trigger(struct cam_ois_ctrl_t *o_ctrl);

/**
 * @o_ctrl: OIS ctrl structure
 *
 * This API stop parklens thread
 */
int32_t ois_parklens_thread_stop(struct cam_ois_ctrl_t *o_ctrl, enum parklens_opcodes opcode);

/**
 * @fw_version: fw version
 *
 * This API for ois fw version set
 */
void ois_fw_version_set(uint64_t fw_version, char* name);

/**
 * @info: Sub device info to req mgr
 *
 * This API publish the subdevice info to req mgr
 */
int32_t cam_ois_publish_dev_info(struct cam_req_mgr_device_info *info);

/**
 * @link: Link setup info
 *
 * This API establishes link ois subdevice with req mgr
 */
int32_t cam_ois_establish_link(struct cam_req_mgr_core_dev_link_setup *link);

/**
 * @apply: Req mgr structure for applying request
 *
 * This API applies the request that is mentioned
 */
int32_t cam_ois_apply_request(struct cam_req_mgr_apply_request *apply);

/**
 * @flush: Req mgr structure for flushing request
 *
 * This API flushes the request that is mentioned
 */
int cam_ois_flush_request(struct cam_req_mgr_flush_request *flush);

/**
 * cam_ois_update_req_mgr - camera ois update reg manager
 * @o_ctrl:        camera ois controller
 * @csl_packet:    camera packet
 *
 * Returns success or failure
 */
int cam_ois_update_req_mgr(struct cam_ois_ctrl_t *o_ctrl, struct cam_packet *csl_packet);
/*xiaomi add end*/
#endif
/* _CAM_OIS_CORE_H_ */
