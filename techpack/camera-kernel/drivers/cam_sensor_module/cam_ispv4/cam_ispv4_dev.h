/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */

#ifndef _CAM_ISPV4_DEV_H_
#define _CAM_ISPV4_DEV_H_

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/irqreturn.h>
#include <linux/iommu.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio/driver.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/workqueue.h>
#include <linux/genalloc.h>
#include <linux/spinlock.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/ispv4_defs.h>
#include <cam_sensor_cmn_header.h>
#include <cam_subdev.h>
#include "cam_debug_util.h"
#include "cam_context.h"
#include "cam_soc_util.h"
#include "linux/completion.h"
#include <media/cam_ispv4.h>
#include <linux/mfd/ispv4_defs.h>

enum cam_ispv4_state_t {
	CAM_ISPV4_INIT,
	CAM_ISPV4_ACQUIRE,
	CAM_ISPV4_CONFIG,
	CAM_ISPV4_START,
};

/**
 * struct intf_params
 * @device_hdl: Device Handle
 * @session_hdl: Session Handle
 * @link_hdl: Link Handle
 * @ops: KMD operations
 * @crm_cb: Callback API pointers
 */
struct intf_params {
	int32_t device_hdl;
	int32_t session_hdl;
	int32_t link_hdl;
	struct cam_req_mgr_kmd_ops ops;
	struct cam_req_mgr_crm_cb *crm_cb;
};

struct cam_ispv4_ctrl_t {
	char device_name[CAM_CTX_DEV_NAME_MAX_LENGTH];
	struct platform_device *pdev;
	//struct cam_hw_soc_info soc_info;
	struct device_node *of_node;
	struct cam_subdev v4l2_dev_str;
	struct intf_params bridge_intf;
	uint16_t pipeline_delay;
	struct ispv4_v4l2_dev *priv;
	struct mutex cam_ispv4_mutex;
	enum cam_ispv4_state_t ispv4_state;
	struct ispv4_sensor_meta per_frame[MAX_PER_FRAME_ARRAY];
	bool fw_boot;
	struct completion rpmsg_isp_ready;
	struct completion rpmsg_asst_ready;
	struct dentry *dbgfileptr;
	bool nopoweroff;
	//struct ispv3_image_device *priv;
	//uint32_t id;
};

int cam_ispv4_driver_init(void);
void cam_ispv4_driver_exit(void);
#endif /* _CAM_ISPV4_DEV_H_ */
