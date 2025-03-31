/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _CAM_ISPV4_CORE_H_
#define _CAM_ISPV4_CORE_H_

#include "cam_ispv4_dev.h"

/**
 * @apply: Req mgr structure for applying request
 *
 * This API applies the request that is mentioned
 */
int cam_ispv4_apply_request(struct cam_req_mgr_apply_request *apply);

/**
 * @flush: Req mgr structure for flushing request
 *
 * This API flushes the request that is mentioned
 */
int cam_ispv4_flush_request(struct cam_req_mgr_flush_request *flush);

/**
 * @info: Sub device info to req mgr
 *
 * Publish the subdevice info
 */
int cam_ispv4_publish_dev_info(struct cam_req_mgr_device_info *info);

/**
 * @link: Link setup info
 *
 * This API establishes link with sensor subdevice with req mgr
 */
int cam_ispv4_establish_link(struct cam_req_mgr_core_dev_link_setup *link);

/**
 * @s_ctrl: Sensor ctrl structure
 * @arg:    Camera control command argument
 *
 * This API handles the camera control argument reached to sensor
 */
int32_t cam_ispv4_driver_cmd(struct cam_ispv4_ctrl_t *s_ctrl, void *arg);

/**
 * @s_ctrl: Sensor ctrl structure
 *
 * This API handles the camera sensor close/shutdown
 */
void cam_ispv4_shutdown(struct cam_ispv4_ctrl_t *s_ctrl);

int cam_ispv4_power_up(void *priv);
int cam_ispv4_power_down(void *priv);
void cam_ispv4_crashdump(void *priv);
#endif /* _CAM_ISPV4_CORE_H_ */
