// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <cam_sensor_cmn_header.h>
#include "cam_ispv4_core.h"
#include "cam_sensor_util.h"
#include "cam_soc_util.h"
#include "cam_trace.h"
#include "cam_common_util.h"
#include "cam_packet_util.h"
#include <media/cam_sensor.h>
#include <media/cam_ispv4.h>
#include "cam_req_mgr_dev.h"
#include <linux/sched/clock.h>
#include <linux/completion.h>
#include <linux/jiffies.h>

//#define DEBUG_LOAD

#define CHECK_COMP_AVALID(comp)                                                \
	do {                                                                   \
		if (!comp.avalid) {                                            \
			ret = -ENODEV;                                         \
			goto release_mutex;                                    \
		}                                                              \
	} while (0)

/*ispv4 cam log add information*/
char *cam_ops_name[] = {"CAM_QUERY_CAP",	  "CAM_ACQUIRE_DEV",
                        "CAM_START_DEV",	  "CAM_STOP_DEV",
                        "CAM_CONFIG_DEV",	  "CAM_RELEASE_DEV",
                        "CAM_SD_SHUTDOWN",  "CAM_FLUSH_REQ",
                        "CAM_QUERY_CAP_V2", "CAM_COMMON_OPCODE_MAX"};

#define SELF_OPCMD_NAME(idx) [idx - CAM_EXT_OPCODE_BASE] = #idx

char *cam_opcode_name[] = {
	SELF_OPCMD_NAME(ISP_OPCODE_PWR_ON),
	SELF_OPCMD_NAME(ISP_OPCODE_PWR_OFF),
	SELF_OPCMD_NAME(ISP_OPCODE_HDMA_TRANS),
	SELF_OPCMD_NAME(ISP_OPCODE_IONMAP),
	SELF_OPCMD_NAME(ISP_OPCODE_IONUNMAP),
	SELF_OPCMD_NAME(ISPV4_OPCODE_RPMSG_SEND_ISP),
	SELF_OPCMD_NAME(ISPV4_OPCODE_RPMSG_RECV_ISP),
	SELF_OPCMD_NAME(ISPV4_OPCODE_RPMSG_GETERR_ISP),
	SELF_OPCMD_NAME(ISPV4_OPCODE_RPROC_BOOT),
	SELF_OPCMD_NAME(ISPV4_OPCODE_RPROC_SHUTDOWN),
	SELF_OPCMD_NAME(ISPV4_RPROC_DDR_PARAM_LOAD),
	SELF_OPCMD_NAME(ISPV4_RPROC_DDR_PARAM_STORE),
	SELF_OPCMD_NAME(ISP_OPCODE_SUSPEND),
	SELF_OPCMD_NAME(ISP_OPCODE_RESUME),
	SELF_OPCMD_NAME(ISP_OPCODE_CHANGE_SPI_SPEED),
};

#define GET_CAM_OPS_NAME(index)                                \
	(CAM_QUERY_CAP <= index && index <= CAM_COMMON_OPCODE_MAX) \
		? cam_ops_name[index - CAM_QUERY_CAP]                  \
		: ((ISP_OPCODE_PWR_ON <= index &&                      \
			index <= ISP_OPCODE_CHANGE_SPI_SPEED)              \
			   ? cam_opcode_name[index - CAM_EXT_OPCODE_BASE]  \
			   : "unknown ops")

const char *cam_packer_opcodes_name[] = {
	"CAMERA_ISPV4_CMD_OPCODE_UPDATE",
	"CAMERA_ISPV4_CMD_OPCODE_RPMSG_SEND",
	"CAMERA_ISPV4_CMD_OPCODE_RPMSG_RECV",
	"CAMERA_ISPV4_CMD_OPCODE_RPMSG_GETERR",
	"CAMERA_ISPV4_CMD_OPCODE_PWR_ON",
	"CAMERA_ISPV4_CMD_OPCODE_PWR_OFF",
	"CAMERA_ISPV4_CMD_OPCODE_IONMAP_WITH_NOTIFY",
	"CAMERA_ISPV4_CMD_OPCODE_IONUNMAP",
	"CAMERA_ISPV4_CMD_OPCODE_ANALOG_BYPASS",
	"CAMERA_ISPV4_CMD_OPCODE_DIGITAL_BYPASS",
	"CAMERA_ISPV4_CMD_OPCODE_RAWLOG_DUMP",
	"CAMERA_ISPV4_CMD_OPCODE_DEBUGINFO_DUMP",
	"CAMERA_ISPV4_CMD_OPCODE_BOOTINFO_DUMP",
	"CAMERA_ISPV4_CMD_OPCODE_IONMAP_REGION",
	"CAMERA_ISPV4_CMD_OPCODE_IONUNMAP_REGION",
	"CAMERA_ISPV4_CMD_OPCODE_MAX"};

#define GET_PACKET_OPS_NAME(index)                                        		\
	((index) <= CAMERA_ISPV4_CMD_OPCODE_MAX) 					\
		? cam_packer_opcodes_name[index - CAMERA_ISPV4_CMD_OPCODE_UPDATE] 	\
		: "unknown ops"

static ktime_t softime;

int cam_ispv4_notify_message(struct cam_ispv4_ctrl_t *s_ctrl,
			     struct cam_miisp_message *msg,
			     uint32_t id)
{
	struct v4l2_event event;
	struct cam_miisp_message *ev_header;

	if (!msg)
		return -EINVAL;

	CAM_DBG(CAM_ISPV4, "MIISP notify id(%d) event", id);

	event.id = id;
	event.type = V4L_EVENT_MIISP_EVENT;
	ev_header = CAM_REQ_MGR_GET_PAYLOAD_PTR(event,
		struct cam_miisp_message);
	memcpy(ev_header, msg, sizeof(struct cam_miisp_message));

	v4l2_event_queue(s_ctrl->v4l2_dev_str.sd.devnode, &event);
	return 0;
}

static int cam_ispv4_notify_wrapper(int msg_type, int event_id,
				    void* data, int len,
				    void *priv)
{
	struct cam_miisp_message msg;
	struct cam_ispv4_ctrl_t *s_ctrl = priv;

	memset(&msg, 0, sizeof(msg));
	msg.session_hdl = s_ctrl->bridge_intf.session_hdl,
	msg.msg_type = msg_type;
	if (len != 0) {
		memcpy(msg.u.data, data, len);
		msg.len = len;
	}
	return cam_ispv4_notify_message(s_ctrl, &msg, event_id);
}

static void ispv4_rpmsg_cb(void *priv, void *id, void* data, int len)
{
	size_t nid = (size_t)id;
	int type = 0;
	if (nid == MIISP_V4L_EVENT_RPMSG_ISP_ERR) {
		type = MIISP_RPMSG_TIMEOUT_TYPE;
		data = NULL;
		len = 0;
		goto publish;
	}
	/*remove rpmsg header, hal do not need*/
	type = MIISP_RPMSG_MSG_TYPE;
	len -= sizeof_field(struct xm_ispv4_rpmsg_pkg, header);
	if (len > MIISP_MSG_MAX_LEN * 4)
		len = MIISP_MSG_MAX_LEN * 4;
	data = (uint8_t *)data + sizeof_field(struct xm_ispv4_rpmsg_pkg, header);
	CAM_DBG(CAM_ISPV4, "rpmsg recv data[0] = %d", *(uint8_t*)data);

publish:
	cam_ispv4_notify_wrapper(type, nid, data, len, priv);
}

static irqreturn_t ispv4_notify_sof(int irq, void *priv)
{
	ktime_t timediff;
	timediff = ktime_to_ms(ktime_sub(ktime_get(), softime));

	softime = ktime_get();
	cam_ispv4_notify_wrapper(MIISP_SOF_MSG_TYPE, MIISP_V4L_EVENT_SOF,
				 NULL, 0, priv);
	CAM_INFO(CAM_ISPV4,"MIISP notify sof event, irq(%d) interval %lld", irq, timediff);
	return IRQ_HANDLED;
}

static irqreturn_t ispv4_notify_eof(int irq, void *priv)
{
	cam_ispv4_notify_wrapper(MIISP_EOF_MSG_TYPE, MIISP_V4L_EVENT_EOF,
				 NULL, 0, priv);
	CAM_INFO(CAM_ISPV4,"MIISP notify eof event, irq(%d)", irq);
	return IRQ_HANDLED;
}

static irqreturn_t ispv4_notify_thermal(int irq, void *priv)
{
	cam_ispv4_notify_wrapper(MIISP_THERMAL_MSG_TYPE, MIISP_V4L_EVENT_THERMAL,
				 NULL, 0, priv);
	CAM_INFO(CAM_ISPV4,"MIISP notify thermal event, irq(%d)", irq);
	return IRQ_HANDLED;
}

int ispv4_exception_notify(void *priv, void* data, int len)
{
	CAM_INFO(CAM_ISPV4,"MIISP exception event notify");

	//TODO: maybe different exception type or data
	(void)data;
	(void)len;

	return cam_ispv4_notify_wrapper(MIISP_EXCEPTION_MSG_TYPE,
					MIISP_V4L_EVENT_EXCEPTION,
					NULL, 0, priv);
}

int ispv4_coredump_notify(void *priv, int type)
{
	CAM_DBG(CAM_ISPV4,"MIISP coredump event notify");
	(void)type;

	return cam_ispv4_notify_wrapper(MIISP_COREDUMP_MSG_TYPE,
					MIISP_V4L_EVENT_CRASH,
					NULL, 0, priv);
}

int ispv4_wdt_notify(void *data)
{
	struct cam_ispv4_ctrl_t *s_ctrl = data;
	struct ispv4_v4l2_dev *priv = s_ctrl->priv;

	if (!priv->v4l2_rproc.avalid)
		return -ENODEV;

	priv->v4l2_rproc.dump_ramlog(priv->v4l2_rproc.rp);
	priv->v4l2_rproc.dump_bootinfo(priv->v4l2_rproc.rp);
	priv->v4l2_rproc.dump_debuginfo(priv->v4l2_rproc.rp);
	CAM_INFO(CAM_ISPV4, "cam ispv4 debuginfo dump success");

	return cam_ispv4_notify_wrapper(MIISP_WDT_MSG_TYPE,
					MIISP_V4L_EVENT_WDT,
					NULL, 0, data);
}

int ispv4_sof_notify(void *data)
{
	CAM_INFO(CAM_ISPV4,"MIISP sof event notify");

	return cam_ispv4_notify_wrapper(MIISP_SOF_MSG_TYPE,
					MIISP_V4L_EVENT_SOF,
					NULL, 0, data);
}

int ispv4_rpmsg_ready_notify(void *data, int ept, bool st)
{
	struct cam_ispv4_ctrl_t *s_ctrl = data;

	CAM_DBG(CAM_ISPV4,"MIISP rpmsg ept %d %s notify",
		 ept, st ? "ready" : "remove");

	if (ept == XM_ISPV4_IPC_EPT_RPMSG_ISP) {
		if (st) {
			complete(&s_ctrl->rpmsg_isp_ready);
		} else {
			reinit_completion(&s_ctrl->rpmsg_isp_ready);
		}
	} else if (ept == XM_ISPV4_IPC_EPT_RPMSG_ASST) {
		if (st) {
			complete(&s_ctrl->rpmsg_asst_ready);
		} else {
			reinit_completion(&s_ctrl->rpmsg_asst_ready);
		}
	} else {
		CAM_ERR(CAM_ISPV4,"MIISP unknown rpmsg ept %d", ept);
		return -EINVAL;
	}

	return 0;
}

int cam_ispv4_register_callback(struct cam_ispv4_ctrl_t *s_ctrl)
{
	struct ispv4_v4l2_dev *priv = s_ctrl->priv;

	if (priv->v4l2_rproc.avalid) {
		priv->v4l2_rproc.register_exception_cb(priv->v4l2_rproc.rp,
						       ispv4_exception_notify, s_ctrl);
		priv->v4l2_rproc.register_crash_cb(priv->v4l2_rproc.rp,
						   ispv4_coredump_notify, s_ctrl);
	}

	if (priv->v4l2_rpmsg.avalid && priv->v4l2_rproc.avalid) {
		priv->v4l2_rpmsg.register_cb(priv->v4l2_rproc.rp,
					     XM_ISPV4_IPC_EPT_RPMSG_ISP,
					     ispv4_rpmsg_cb, s_ctrl);
	}

	if (priv->v4l2_ctrl.avalid) {
		priv->v4l2_ctrl.register_wdt_cb(priv->v4l2_ctrl.data,
						ispv4_wdt_notify, s_ctrl);
		// priv->v4l2_ctrl.register_sof_cb(priv->v4l2_ctrl.data,
		// 				ispv4_sof_notify, s_ctrl);
	}

	return 0;
}

int cam_ispv4_remove_maped_region(struct cam_ispv4_ctrl_t *s_ctrl)
{
	struct ispv4_v4l2_dev *priv = s_ctrl->priv;
	int i = 0;

	//remove any region has maped
	if (priv->v4l2_ionmap.avalid) {
		for (i = 0; i < ISPV4_IONMAP_NUM; i++) {
			priv->v4l2_ionmap.unmap(priv->v4l2_ionmap.dev, i);
		}
	}

	return 0;
}

int cam_ispv4_start_frame(void *ppriv)
{
	struct cam_ispv4_ctrl_t *s_ctrl = ppriv;
	struct ispv4_v4l2_dev *priv = s_ctrl->priv;
	int ret = 0;

	if (!s_ctrl->fw_boot)
		return ret;

	if (!priv->v4l2_pci.avalid)
		return -ENODEV;

	if (!priv->v4l2_pci.sof_registered) {
		ret = priv->v4l2_pci.pcie_msi_register(priv->v4l2_pci.pcidev,
				MSI_SWINT5, ispv4_notify_sof, "ispv4_sof", s_ctrl);
		if (ret) {
			CAM_ERR(CAM_ISPV4, "register sof irq fail");
			return ret;
		}
		priv->v4l2_pci.sof_registered = true;
	} else {
		CAM_WARN(CAM_ISPV4, "sof irq has been register");
	}
	if (!priv->v4l2_pci.eof_registered) {
		ret = priv->v4l2_pci.pcie_msi_register(priv->v4l2_pci.pcidev,
				MSI_SWINT6, ispv4_notify_eof, "ispv4_eof", s_ctrl);
		if (ret) {
			CAM_ERR(CAM_ISPV4, "register eof irq fail");
			return ret;
		}
		priv->v4l2_pci.eof_registered = true;
	} else {
		CAM_WARN(CAM_ISPV4, "eof irq has been register");
	}

	if (!priv->v4l2_pci.thermal_registered) {
		ret = priv->v4l2_pci.pcie_msi_register(priv->v4l2_pci.pcidev,
				MSI_SWINT4, ispv4_notify_thermal, "ispv4_thermal", s_ctrl);
		if (ret) {
			CAM_ERR(CAM_ISPV4, "register thermal irq fail");
			return ret;
		}
		priv->v4l2_pci.thermal_registered = true;
	} else {
		CAM_WARN(CAM_ISPV4, "thermal irq has been register");
	}

	return ret;
}

int cam_ispv4_stop_frame(void *ppriv)
{
	struct cam_ispv4_ctrl_t *s_ctrl = ppriv;
	struct ispv4_v4l2_dev *priv = s_ctrl->priv;
	int ret = 0;

	if (!s_ctrl->fw_boot)
		return ret;

	if (!priv->v4l2_pci.avalid)
		return -ENODEV;


	if (priv->v4l2_pci.sof_registered) {
		priv->v4l2_pci.pcie_msi_unregister(priv->v4l2_pci.pcidev,
						MSI_SWINT5, s_ctrl);
		priv->v4l2_pci.sof_registered = false;
	} else {
		CAM_WARN(CAM_ISPV4, "sof not register");
	}
	if(priv->v4l2_pci.eof_registered) {
		priv->v4l2_pci.pcie_msi_unregister(priv->v4l2_pci.pcidev,
				MSI_SWINT6, s_ctrl);
		priv->v4l2_pci.eof_registered = false;
	} else {
		CAM_WARN(CAM_ISPV4, "eof not register");
	}

	if(priv->v4l2_pci.thermal_registered) {
		priv->v4l2_pci.pcie_msi_unregister(priv->v4l2_pci.pcidev,
				MSI_SWINT4, s_ctrl);
		priv->v4l2_pci.thermal_registered = false;
	} else {
		CAM_WARN(CAM_ISPV4, "thermal not register");
	}

	return ret;
}

void cam_ispv4_crashdump(void *ppriv)
{
	struct cam_ispv4_ctrl_t *s_ctrl = ppriv;
	struct ispv4_v4l2_dev *priv = s_ctrl->priv;
	int ret = 0;

	if (!priv)
		return;

	if (!priv->v4l2_ctrl.avalid)
		return;

	ret = priv->v4l2_ctrl.ispv4_get_powerstat(priv->v4l2_ctrl.pdev);
	if (ret) {
		CAM_ERR(CAM_ISPV4, "cam ispv4 is pwrdown when crash");
		return;
	}

	if (!priv->v4l2_rproc.avalid)
		return;

	ret = priv->v4l2_rproc.get_boot_status(priv->v4l2_rproc.rp);
	if (ret == false) {
		CAM_ERR(CAM_ISPV4, "cam ispv4 is not booted when crash");
		return;
	}

	if (s_ctrl->fw_boot) {
		if (priv->v4l2_spi.avalid)
			priv->v4l2_spi.spi_gettick(priv->v4l2_spi.spidev);
		msleep(600);
		if (priv->v4l2_spi.avalid)
			priv->v4l2_spi.spi_gettick(priv->v4l2_spi.spidev);

		if (priv->v4l2_rproc.avalid && priv->v4l2_pci.linkup)
			priv->v4l2_rproc.dump_ramlog(priv->v4l2_rproc.rp);
	}

}

int cam_ispv4_power_up(void *ppriv)
{
	struct cam_ispv4_ctrl_t *s_ctrl = ppriv;
	struct ispv4_v4l2_dev *priv = s_ctrl->priv;
	int ret = 0;

	if (!priv)
		return -ENODEV;

	if (!priv->v4l2_ctrl.avalid)
		return -ENODEV;

#if IS_ENABLED(CONFIG_MIISP_CHIP)
	ret = priv->v4l2_ctrl.ispv4_power_on_seq(priv->v4l2_ctrl.pdev);
	if (ret) {
		CAM_ERR(CAM_ISPV4, "cam ispv4 turn on cpu fail");
		return -1;
	}
#else
	if (!priv->v4l2_pci.linkup) {
		priv->v4l2_ctrl.ispv4_fpga_reset(priv->v4l2_ctrl.pdev);
	}
#endif

	//Boot
	if (!s_ctrl->fw_boot) {
		ret = priv->v4l2_pmic.config(priv->v4l2_pmic.data,
				ISPV4_PMIC_CONFIG_SAVE_CURRENT_OFF);
		return ret;
	}


	if (!priv->v4l2_pci.linkup) {
		ret = priv->v4l2_pci.resume_pci(priv->v4l2_pci.pcidev);
		if (ret) {
			CAM_ERR(CAM_ISPV4, "resume pci fail");
			return ret;
		} else {
			CAM_INFO(CAM_ISPV4, "resume pci success");
			priv->v4l2_pci.linkup = true;
		}
	} else {
		CAM_ERR(CAM_ISPV4, "pci has been linkup");
	}


	if (!priv->v4l2_rproc.avalid)
		return -ENODEV;

	priv->v4l2_rproc.register_rpm_ready_cb(priv->v4l2_rproc.rp,
					       ispv4_rpmsg_ready_notify,
					       s_ctrl);

	ret = priv->v4l2_rproc.boot(priv->v4l2_rproc.rp, NULL);

	if (ret != 0) {
		CAM_ERR(CAM_ISPV4, "rproc boot failed, ret=%d", ret);
		return ret;
	}

	priv->v4l2_pci.set_isp_time(priv->v4l2_pci.pcidata, local_clock());

	ret = wait_for_completion_timeout(&s_ctrl->rpmsg_isp_ready, msecs_to_jiffies(300));
	if (!ret) {
		CAM_ERR(CAM_ISPV4, "MIISP wait rpmsg isp ready timeout");
		return -ETIMEDOUT;
	}
	ret = wait_for_completion_timeout(&s_ctrl->rpmsg_asst_ready, msecs_to_jiffies(300));
	if (!ret) {
		CAM_ERR(CAM_ISPV4, "MIISP wait rpmsg asst ready timeout");
		return -ETIMEDOUT;
	}

	ret = cam_ispv4_register_callback(s_ctrl);
	if (ret) {
		CAM_ERR(CAM_ISPV4, "cam_ispv4_register_callback fail");
		return ret;
	}

	priv->v4l2_ctrl.enable_wdt_irq(priv->v4l2_ctrl.data);

	return ret;
}

static int cam_ispv4_send_poweroff_ipc(struct cam_ispv4_ctrl_t *s_ctrl)
{
	struct ispv4_v4l2_dev *priv = s_ctrl->priv;
	struct xm_ispv4_rpmsg_pkg ipc_msg;
	int ret;

	if (!priv->v4l2_rpmsg.avalid)
		return -ENODEV;

	ipc_msg.func = ICC_REQUEST_POWER_OFF;
	ipc_msg.header.type = MIPC_MSGHEADER_CMD;
	ret = priv->v4l2_rpmsg.send(priv->v4l2_rpmsg.rp,
				    XM_ISPV4_IPC_EPT_RPMSG_ISP,
				    MIPC_MSGHEADER_CMD,
				    sizeof(struct xm_ispv4_rpmsg_pkg),
				    (void*)&ipc_msg,
				    FALSE, NULL);
	if (ret != 0) {
		CAM_ERR(CAM_ISPV4, "rpmsg-isp send fail ret = %d", ret);
		return -EINVAL;
	}


	if (ipc_msg.header.param != ICC_CMD_OK) {
		CAM_ERR(CAM_ISPV4, "power off ipc param ret:%d",
				ipc_msg.header.param);
		return -EINVAL;
	}

	return 0;
}

int cam_ispv4_power_down(void *ppriv)
{
	struct cam_ispv4_ctrl_t *s_ctrl = ppriv;
	struct ispv4_v4l2_dev *priv = s_ctrl->priv;
	int ret = 0;

	if (!priv)
		return -ENODEV;

	if (!priv->v4l2_ctrl.avalid)
		return -ENODEV;
	priv->v4l2_ctrl.mipi_iso_disable(priv->v4l2_ctrl.data);

	if (!priv->v4l2_rproc.avalid) {
		CAM_ERR(CAM_ISPV4, "v4l2_rproc invalid");
		return -ENODEV;
	}
	if (s_ctrl->fw_boot) {
		CAM_DBG(CAM_ISPV4, "disable wdt irq");
		if (priv->v4l2_ctrl.avalid)
			priv->v4l2_ctrl.disable_wdt_irq(priv->v4l2_ctrl.data);
		ret = cam_ispv4_send_poweroff_ipc(s_ctrl);
		if (ret) {
			//if fail, continue power off seq.
			CAM_ERR(CAM_ISPV4, "power off ipc fail");
		} else {
			CAM_DBG(CAM_ISPV4, "power off ipc send success");
		}

		ret = priv->v4l2_rproc.shutdown(priv->v4l2_rproc.rp);
		if (ret) {
			CAM_ERR(CAM_ISPV4, "rproc shutdown fail");
			return -EINVAL;
		} else {
			CAM_DBG(CAM_ISPV4, "rproc shutdown success");
		}
		reinit_completion(&s_ctrl->rpmsg_isp_ready);
		reinit_completion(&s_ctrl->rpmsg_asst_ready);

		if (!priv->v4l2_ionmap.avalid)
			return -ENODEV;
		ret = priv->v4l2_ionmap.remove_any_mappd(priv->v4l2_ionmap.dev);
		if (ret) {
			CAM_ERR(CAM_ISPV4, "remove_any_mappd fail");
			return ret;
		} else {
			CAM_DBG(CAM_ISPV4, "remove_any_mappd success");
		}

		if (!priv->v4l2_pci.avalid)
			return -ENODEV;
		if (!priv->v4l2_pci.linkup) {
			CAM_ERR(CAM_ISPV4, "pci has been link down");
		} else {
			ret = priv->v4l2_pci.suspend_pci(priv->v4l2_pci.pcidev);
			if (ret) {
				CAM_ERR(CAM_ISPV4, "suspend pci fail");
				ret = priv->v4l2_pci.suspend_pci_force(
							priv->v4l2_pci.pcidev);
				priv->v4l2_pci.linkup = false;
				if (ret) {
					CAM_ERR(CAM_ISPV4, "suspend pci force fail ret = %d",
								ret);
					return -EINVAL;
				}
				CAM_INFO(CAM_ISPV4, "suspend pci force success");
			} else {
				CAM_INFO(CAM_ISPV4, "suspend pci success");
				priv->v4l2_pci.linkup = false;
			}
		}
	} else {
		ret = priv->v4l2_pmic.config(priv->v4l2_pmic.data,
				ISPV4_PMIC_CONFIG_SAVE_CURRENT_ON);
	}

#if IS_ENABLED(CONFIG_MIISP_CHIP)
	if (!priv->v4l2_ctrl.avalid)
		return -ENODEV;
	priv->v4l2_ctrl.ispv4_power_off_seq(priv->v4l2_ctrl.pdev);
#endif

	return ret;
}

static void cam_ispv4_update_req_mgr(struct cam_ispv4_ctrl_t *s_ctrl,
				     struct cam_packet *csl_packet)
{
	struct cam_req_mgr_add_request add_req;

	add_req.link_hdl = s_ctrl->bridge_intf.link_hdl;
	add_req.req_id = csl_packet->header.request_id;
	CAM_DBG(CAM_ISPV4, " Rxed Req Id: %lld",
		csl_packet->header.request_id);

	add_req.dev_hdl = s_ctrl->bridge_intf.device_hdl;

	if (s_ctrl->bridge_intf.crm_cb &&
		s_ctrl->bridge_intf.crm_cb->add_req) {
		s_ctrl->bridge_intf.crm_cb->add_req(&add_req);
		CAM_DBG(CAM_ISPV4, "ISPV4 Request Id: %lld added to CRM",
			add_req.req_id);
	} else {
		CAM_ERR(CAM_ISPV4, "ISPV4 Can't add Request ID: %lld to CRM",
			csl_packet->header.request_id);
	}
}

void cam_ispv4_shutdown(struct cam_ispv4_ctrl_t *s_ctrl)
{
	int ret = 0;

	if (s_ctrl->bridge_intf.device_hdl != -1) {
		ret = cam_destroy_device_hdl(s_ctrl->bridge_intf.device_hdl);
		if (ret < 0)
			CAM_ERR(CAM_ISPV4,
				"dhdl already destroyed: ret = %d", ret);
	}

	s_ctrl->bridge_intf.device_hdl = -1;
	s_ctrl->bridge_intf.link_hdl = -1;
	s_ctrl->bridge_intf.session_hdl = -1;
	s_ctrl->ispv4_state = CAM_ISPV4_INIT;
}

int cam_ispv4_process_config(struct cam_ispv4_ctrl_t *s_ctrl, void *arg)
{
	int ret = 0;
	uintptr_t generic_pkt_addr, cmd_buf_ptr;
	size_t remain_len = 0, len_of_buffer;
	struct cam_control *ioctl_ctrl = NULL;
	struct cam_config_dev_cmd dev_config;
	struct cam_packet *csl_packet = NULL;
	struct cam_cmd_buf_desc *cmd_desc = NULL;
	uint32_t *cmd_buf = NULL;
	uint32_t *offset = NULL;

	ioctl_ctrl = (struct cam_control *)arg;

	memset(&dev_config, 0, sizeof(struct cam_config_dev_cmd));

	if (access_ok((void *)ioctl_ctrl->handle, sizeof(dev_config))
		&& copy_from_user(&dev_config,
		u64_to_user_ptr(ioctl_ctrl->handle),
		sizeof(dev_config)))
		return -EFAULT;
	ret = cam_mem_get_cpu_buf(dev_config.packet_handle,
		&generic_pkt_addr, &len_of_buffer);
	if (ret) {
		CAM_ERR(CAM_ISPV4,
			"ISPV4 error in converting command Handle Error: %d", ret);
		return -EINVAL;
	}
	remain_len = len_of_buffer;
	if ((sizeof(struct cam_packet) > len_of_buffer) ||
		((size_t)dev_config.offset >= len_of_buffer -
		sizeof(struct cam_packet))) {
		CAM_ERR(CAM_ISPV4,
			"Inval cam_packet strut size: %zu, len_of_buff: %zu",
			 sizeof(struct cam_packet), len_of_buffer);
		ret = -EINVAL;
		goto end;
	}

	remain_len -= (size_t)dev_config.offset;
	csl_packet = (struct cam_packet *)
		(generic_pkt_addr + (uint32_t)dev_config.offset);

	if (cam_packet_util_validate_packet(csl_packet,
		remain_len)) {
		CAM_ERR(CAM_ISPV4, "Invalid packet params");
		ret = -EINVAL;
		goto end;
	}
	CAM_DBG(CAM_ISPV4, "packet ops %s start",
			 GET_PACKET_OPS_NAME(csl_packet->header.op_code & 0xFFFFFF));
	switch(csl_packet->header.op_code & 0xFFFFFF) {
		case CAMERA_ISPV4_CMD_OPCODE_UPDATE: {
			struct ispv4_sensor_meta *cmd_set = NULL;
			int frame_off = 0;

			offset = (uint32_t *)&csl_packet->payload;
			offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
			cmd_desc = (struct cam_cmd_buf_desc *)(offset);

			ret = cam_mem_get_cpu_buf(cmd_desc->mem_handle,
				&cmd_buf_ptr, &len_of_buffer);
			if (ret) {
				CAM_ERR(CAM_ISPV4, "Fail in get buffer: %d", ret);
				goto end;
			}
			if ((len_of_buffer < sizeof(struct ispv4_sensor_meta)) ||
				(cmd_desc->offset >
				(len_of_buffer - sizeof(struct ispv4_sensor_meta)))) {
				CAM_ERR(CAM_ISPV4, "Not enough buffer");
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}
			remain_len = len_of_buffer - cmd_desc->offset;
			cmd_buf = (uint32_t *)(cmd_buf_ptr + cmd_desc->offset);
			cmd_set = (struct ispv4_sensor_meta *)cmd_buf;
			frame_off = csl_packet->header.request_id % MAX_PER_FRAME_ARRAY;
			memcpy(&(s_ctrl->per_frame[frame_off]), cmd_set,
				sizeof(struct ispv4_sensor_meta));

			cam_ispv4_update_req_mgr(s_ctrl, csl_packet);
			s_ctrl->ispv4_state = CAM_ISPV4_CONFIG;
			CAM_DBG(CAM_ISPV4, "CAMERA_ISPV4_CMD_OPCODE_UPDATE finish");
			cam_mem_put_cpu_buf(cmd_desc->mem_handle);
		}
		break;
		case CAMERA_ISPV4_CMD_OPCODE_RPMSG_SEND: {
			struct ispv4_rpmsg_send_para *rpmsg_para = NULL;
			struct ispv4_v4l2_dev *priv = s_ctrl->priv;

			if (!priv->v4l2_rpmsg.avalid) {
				ret = -ENODEV;
				goto end;
			}

			offset = (uint32_t *)&csl_packet->payload;
			offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
			cmd_desc = (struct cam_cmd_buf_desc *)(offset);

			ret = cam_mem_get_cpu_buf(cmd_desc->mem_handle,
				&cmd_buf_ptr, &len_of_buffer);
			if (ret) {
				CAM_ERR(CAM_ISPV4, "Fail in get buffer: %d", ret);
				goto end;
			}
			if ((len_of_buffer < sizeof(struct ispv4_rpmsg_send_para)) ||
				(cmd_desc->offset >
				(len_of_buffer - sizeof(struct ispv4_rpmsg_send_para)))) {
				CAM_ERR(CAM_ISPV4, "Not enough buffer");
				ret = -EINVAL;
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}
			remain_len = len_of_buffer - cmd_desc->offset;
			cmd_buf = (uint32_t *)(cmd_buf_ptr + cmd_desc->offset);
			rpmsg_para = (struct ispv4_rpmsg_send_para *)cmd_buf;

			ret = priv->v4l2_rpmsg.send(priv->v4l2_rpmsg.rp,
						    XM_ISPV4_IPC_EPT_RPMSG_ISP,
						    rpmsg_para->cmd, rpmsg_para->len,
						    (void*)rpmsg_para->data,
						    FALSE, NULL);
			if (ret != 0) {
				CAM_ERR(CAM_ISPV4, "rpmsg-isp send fail ret = %d", ret);
				ret = -EINVAL;
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}

			if (copy_to_user(u64_to_user_ptr(ioctl_ctrl->handle),
					&dev_config,
					sizeof(struct cam_config_dev_cmd))) {
				CAM_ERR(CAM_ISPV4, "Failed Copy to User");
				ret = -EFAULT;
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}

			CAM_DBG(CAM_ISPV4, "rpmsg-isp send(cmd:%d, len:%d)",
					     rpmsg_para->cmd, rpmsg_para->len);
			cam_mem_put_cpu_buf(cmd_desc->mem_handle);
		}
		break;
		case CAMERA_ISPV4_CMD_OPCODE_RPMSG_RECV: {
			struct ispv4_rpmsg_recv_para *rpmsg_para = NULL;
			struct ispv4_v4l2_dev *priv = s_ctrl->priv;

			if (!priv->v4l2_rpmsg.avalid) {
				ret = -ENODEV;
				goto end;
			}

			offset = (uint32_t *)&csl_packet->payload;
			offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
			cmd_desc = (struct cam_cmd_buf_desc *)(offset);

			ret = cam_mem_get_cpu_buf(cmd_desc->mem_handle,
				&cmd_buf_ptr, &len_of_buffer);
			if (ret) {
				CAM_ERR(CAM_ISPV4, "Fail in get buffer: %d", ret);
				goto end;
			}

			remain_len = len_of_buffer - cmd_desc->offset;
			cmd_buf = (uint32_t *)(cmd_buf_ptr + cmd_desc->offset);
			rpmsg_para = (struct ispv4_rpmsg_recv_para *)cmd_buf;

			ret = priv->v4l2_rpmsg.recv(priv->v4l2_rpmsg.rp,
						    XM_ISPV4_IPC_EPT_RPMSG_ISP,
						    rpmsg_para->cap,
						    (void *)&rpmsg_para->data[0],
						    FALSE);
			if (ret != 0) {
				CAM_ERR(CAM_ISPV4, "rpmsg-isp recv fail");
				ret = -EINVAL;
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}

			if (copy_to_user(u64_to_user_ptr(ioctl_ctrl->handle),
					&dev_config,
					sizeof(struct cam_config_dev_cmd))) {
				CAM_ERR(CAM_ISPV4, "Failed Copy to User");
				ret = -EFAULT;
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}

			CAM_DBG(CAM_ISPV4, "rpmsg-isp recv");
			cam_mem_put_cpu_buf(cmd_desc->mem_handle);
		}
		break;
		case CAMERA_ISPV4_CMD_OPCODE_RPMSG_GETERR: {
			struct ispv4_rpmsg_recv_para *rpmsg_para = NULL;
			struct ispv4_v4l2_dev *priv = s_ctrl->priv;

			if (!priv->v4l2_rpmsg.avalid) {
				ret = -ENODEV;
				goto end;
			}

			offset = (uint32_t *)&csl_packet->payload;
			offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
			cmd_desc = (struct cam_cmd_buf_desc *)(offset);

			ret = cam_mem_get_cpu_buf(cmd_desc->mem_handle,
				&cmd_buf_ptr, &len_of_buffer);
			if (ret) {
				CAM_ERR(CAM_ISPV4, "Fail in get buffer: %d", ret);
				goto end;
			}

			remain_len = len_of_buffer - cmd_desc->offset;
			cmd_buf = (uint32_t *)(cmd_buf_ptr + cmd_desc->offset);
			rpmsg_para = (struct ispv4_rpmsg_recv_para *)cmd_buf;

			ret = priv->v4l2_rpmsg.get_err(priv->v4l2_rpmsg.rp,
						       XM_ISPV4_IPC_EPT_RPMSG_ISP,
						       rpmsg_para->cap,
						       (void *)&rpmsg_para->data[0],
						       FALSE);
			if (ret != 0) {
				CAM_ERR(CAM_ISPV4, "rpmsg-isp get err msg fail");
				ret = -EINVAL;
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}

			if (copy_to_user(u64_to_user_ptr(ioctl_ctrl->handle),
					&dev_config,
					sizeof(struct cam_config_dev_cmd))) {
				CAM_ERR(CAM_ISPV4, "Failed Copy to User");
				ret = -EFAULT;
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}

			CAM_DBG(CAM_ISPV4, "rpmsg-isp get err msg");
			cam_mem_put_cpu_buf(cmd_desc->mem_handle);
		}
		break;
		case CAMERA_ISPV4_CMD_OPCODE_RAWLOG_DUMP: {
			struct ispv4_v4l2_dev *priv = s_ctrl->priv;
			if (!priv->v4l2_rproc.avalid) {
				ret = -ENODEV;
				goto end;
			}
			priv->v4l2_rproc.dump_ramlog(priv->v4l2_rproc.rp);
			CAM_DBG(CAM_ISPV4, "cam ispv4 rawlog dump success");
		}
		break;
		case CAMERA_ISPV4_CMD_OPCODE_DEBUGINFO_DUMP: {
			struct ispv4_v4l2_dev *priv = s_ctrl->priv;
			if (!priv->v4l2_rproc.avalid) {
				ret = -ENODEV;
				goto end;
			}
			priv->v4l2_rproc.dump_debuginfo(priv->v4l2_rproc.rp);
			CAM_DBG(CAM_ISPV4, "cam ispv4 debuginfo dump success");
		}
		break;
		case CAMERA_ISPV4_CMD_OPCODE_BOOTINFO_DUMP: {
			struct ispv4_v4l2_dev *priv = s_ctrl->priv;
			if (!priv->v4l2_rproc.avalid) {
				ret = -ENODEV;
				goto end;
			}
			priv->v4l2_rproc.dump_bootinfo(priv->v4l2_rproc.rp);
			CAM_DBG(CAM_ISPV4, "cam ispv4 bootinfo dump success");
		}
		break;
		case CAMERA_ISPV4_CMD_OPCODE_ANALOG_BYPASS: {
			struct ispv4_v4l2_dev *priv = s_ctrl->priv;

			if (!priv->v4l2_ctrl.avalid) {
				ret = -ENODEV;
				goto end;
			}
			if (!priv->v4l2_pmic.avalid) {
				ret = -ENODEV;
				goto end;
			}

			priv->v4l2_ctrl.mipi_iso_enable(priv->v4l2_ctrl.data);
			ret = priv->v4l2_pmic.config(priv->v4l2_pmic.data,
					ISPV4_PMIC_CONFIG_ANALOG_BYPASS);

			CAM_DBG(CAM_ISPV4, "CAMERA_ISPV4_CMD_OPCODE_ANALOG_BYPASS finish");
		}
		break;
		case CAMERA_ISPV4_CMD_OPCODE_DIGITAL_BYPASS: {
			struct ispv4_v4l2_dev *priv = s_ctrl->priv;

			if (!priv->v4l2_pmic.avalid) {
				ret = -ENODEV;
				goto end;
			}
			ret = priv->v4l2_pmic.config(priv->v4l2_pmic.data,
					ISPV4_PMIC_CONFIG_DIGITAL_BYPASS);

			CAM_DBG(CAM_ISPV4,
					 "CAMERA_ISPV4_CMD_OPCODE_DIGITAL_BYPASS finish");
		}
		break;
		case CAMERA_ISPV4_CMD_OPCODE_IONMAP_WITH_NOTIFY: {
			int i = 0, j = 0;
			struct ispv4_v4l2_dev *priv = s_ctrl->priv;
			struct ispv4_rpmsg_send_para *rpmsg_para = NULL;
			struct ispv4_ipc_hdma_msg *hdma_msg = NULL;
			struct meta_buf *buf = NULL;
			uint32_t buf_size;
#ifdef DEBUG_LOAD
			/*for debug*/
			int k = 0;
			uint32_t *dbg;
#endif
			if (!priv->v4l2_rpmsg.avalid || !priv->v4l2_ionmap.avalid) {
				ret = -ENODEV;
				goto end;
			}

			offset = (uint32_t *)&csl_packet->payload;
			offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
			cmd_desc = (struct cam_cmd_buf_desc *)(offset);

			for (i = 0; i < csl_packet->num_cmd_buf; i++) {
				ret = cam_mem_get_cpu_buf(cmd_desc[i].mem_handle,
					&cmd_buf_ptr, &len_of_buffer);
				if (ret) {
					CAM_ERR(CAM_ISPV4,
						"ionmap, fail in get buffer: %d",
						 ret);
					goto end;
				}
				remain_len = len_of_buffer - cmd_desc->offset;
				cmd_buf = (uint32_t *)(cmd_buf_ptr + cmd_desc->offset);
				rpmsg_para = (struct ispv4_rpmsg_send_para *)cmd_buf;

				//CAM_INFO(CAM_ISPV4,
				//	"ionmap dump ispv4_rpmsg_send_para len %d",
				//	rpmsg_para->len);
				buf_size = sizeof(struct ispv4_rpmsg_send_para);
				if ((len_of_buffer < buf_size + rpmsg_para->len) ||
				(cmd_desc->offset >
				(len_of_buffer - buf_size - rpmsg_para->len))) {
					CAM_ERR(CAM_ISPV4,
						"ionmap, cmd buf[%d] not enough buffer",
						i);
					ret = -EINVAL;
					cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
					goto end;
				}
				hdma_msg = (struct ispv4_ipc_hdma_msg *)rpmsg_para->data;

#ifdef DEBUG_LOAD
				dbg = (uint32_t *)hdma_msg;
				//CAM_INFO(CAM_ISPV4, "ionmap dump ispv4_ipc_hdma_msg");
				for (k = 0; k < sizeof(struct ispv4_ipc_hdma_msg) / 4; k++) {
					CAM_DBG(CAM_ISPV4,
						"[addr]: %llx [hex]: %lx [dec]: %d",
						dbg,*dbg, *dbg);
					dbg++;
				}
#endif
				buf = hdma_msg->buf;
				for (j = 0; j < hdma_msg->meta_buf_num; j++) {
					buf_size = sizeof(struct meta_buf) +
						buf->total_blk_num * sizeof(struct buf_blk);

#ifdef DEBUG_LOAD
					dbg = (uint32_t *)buf;
					CAM_DBG(CAM_ISPV4,
						"dump meta_buf[%d], num of buf_blk %d",
						j, buf->total_blk_num);
					for (k = 0; k < buf_size / 4; k++) {
						CAM_DBG(CAM_ISPV4,
							"[addr]: %llx [hex]: %lx [dec]: %d",
							dbg,*dbg, *dbg);
						dbg++;
					}
#endif
					ret = priv->v4l2_ionmap.mapfd_no_region(
							priv->v4l2_ionmap.dev,
							buf->buf_fd, &buf->hdma_addr);
					if (ret) {
						CAM_ERR(CAM_ISPV4,
							"ionmap, fd %d map fail",
							buf->buf_fd);
						ret = -EINVAL;
						cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
						goto end;
					}
					CAM_DBG(CAM_ISPV4,
						"ionmap, fd %d map success, iova %llx",
						buf->buf_fd, buf->hdma_addr);
					buf = (struct meta_buf *)
						(((uint32_t*)buf) + (buf_size / 4));
				}
				ret = priv->v4l2_rpmsg.send(priv->v4l2_rpmsg.rp,
							XM_ISPV4_IPC_EPT_RPMSG_ISP,
							rpmsg_para->cmd, rpmsg_para->len,
							(void*)rpmsg_para->data,
							FALSE, NULL);
				if (ret) {
					CAM_ERR(CAM_ISPV4, "ionmap, rpmsg send fail ret = %d", ret);
					ret = -EINVAL;
					cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
					goto end;
				}
				cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
			}
			if (copy_to_user(u64_to_user_ptr(ioctl_ctrl->handle),
					&dev_config,
					sizeof(struct cam_config_dev_cmd))) {
				CAM_ERR(CAM_ISPV4, "Failed Copy to User");
				ret = -EFAULT;
				goto end;
			}
			CAM_DBG(CAM_ISPV4, "ionmap, rpmsg send success");
		}
		break;
		case CAMERA_ISPV4_CMD_OPCODE_IONUNMAP: {
			int i = 0, j = 0;
			struct ispv4_v4l2_dev *priv = s_ctrl->priv;
			struct ispv4_rpmsg_send_para *rpmsg_para = NULL;
			struct ispv4_ipc_hdma_msg *hdma_msg = NULL;
			struct meta_buf *buf = NULL;
			uint32_t buf_size;

#ifdef DEBUG_LOAD
			/*for debug*/
			int k = 0;
			uint32_t *dbg;
#endif
			if (!priv->v4l2_ionmap.avalid)
				return -ENODEV;

			offset = (uint32_t *)&csl_packet->payload;
			offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
			cmd_desc = (struct cam_cmd_buf_desc *)(offset);

			//CAM_INFO(CAM_ISPV4,"ionunmap, buf num %d ", csl_packet->num_cmd_buf);

			for (i = 0; i < csl_packet->num_cmd_buf; i++) {
				ret = cam_mem_get_cpu_buf(cmd_desc[i].mem_handle,
					&cmd_buf_ptr, &len_of_buffer);
				if (ret) {
					CAM_ERR(CAM_ISPV4,
						"ionunmap, fail in get buffer: %d",
						ret);
					goto end;
				}
				remain_len = len_of_buffer - cmd_desc->offset;
				cmd_buf = (uint32_t *)(cmd_buf_ptr + cmd_desc->offset);
				rpmsg_para = (struct ispv4_rpmsg_send_para *)cmd_buf;

				//CAM_INFO(CAM_ISPV4,
				//	"ionunmap dump ispv4_rpmsg_send_para len %d",
				//	rpmsg_para->len);
				buf_size = sizeof(struct ispv4_rpmsg_send_para);
				if ((len_of_buffer < buf_size + rpmsg_para->len) ||
				(cmd_desc->offset >
				(len_of_buffer - buf_size - rpmsg_para->len))) {
					CAM_ERR(CAM_ISPV4,
						"ionunmap, cmd buf[%d] not enough buffer",
						i);
					ret = -EINVAL;
					cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
					goto end;

				}
				hdma_msg = (struct ispv4_ipc_hdma_msg *)rpmsg_para->data;

#ifdef DEBUG_LOAD
				dbg = (uint32_t *)hdma_msg;
				CAM_DBG(CAM_ISPV4, "ionunmap dump ispv4_ipc_hdma_msg");
				for (k = 0; k < sizeof(struct ispv4_ipc_hdma_msg) / 4; k++) {
					CAM_DBG(CAM_ISPV4,
						"[addr]: %llx [hex]: %lx [dec]: %d",
						dbg,*dbg, *dbg);
					dbg++;
				}
#endif
				buf = hdma_msg->buf;
				for (j = 0; j < hdma_msg->meta_buf_num; j++) {
					buf_size = sizeof(struct meta_buf) +
						buf->total_blk_num * sizeof(struct buf_blk);
#ifdef DEBUG_LOAD
					dbg = (uint32_t *)buf;

					CAM_DBG(CAM_ISPV4,
						"dump meta_buf[%d], num of buf_blk %d",
						j, buf->total_blk_num);
					for (k = 0; k < buf_size / 4; k++) {
						CAM_DBG(CAM_ISPV4,
							"[addr]: %llx [hex]: %lx [dec]: %d",
							dbg,*dbg, *dbg);
						dbg++;
					}
#endif
					ret = priv->v4l2_ionmap.unmap_no_region(
								priv->v4l2_ionmap.dev,
								buf->buf_fd);
					if (ret) {
						CAM_ERR(CAM_ISPV4,
							"ionunmap, fd %d unmap fail",
							buf->buf_fd);
						ret = -EINVAL;
						cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
						goto end;
					}
					CAM_DBG(CAM_ISPV4,
						"ionunmap, fd %d unmap success, iova %llx",
						buf->buf_fd, buf->hdma_addr);
					buf = (struct meta_buf *)
						(((uint32_t*)buf) + (buf_size / 4));
				}
				cam_mem_put_cpu_buf(cmd_desc[i].mem_handle);
			}
			if (copy_to_user(u64_to_user_ptr(ioctl_ctrl->handle),
					&dev_config,
					sizeof(struct cam_config_dev_cmd))) {
				CAM_ERR(CAM_ISPV4, "Failed Copy to User");
				ret = -EFAULT;
				goto end;
			}
			CAM_DBG(CAM_ISPV4, "ionunmap success");
		}
		break;
		case CAMERA_ISPV4_CMD_OPCODE_PWR_ON: {
			ret = cam_ispv4_power_up(s_ctrl);
		}
		break;

		case CAMERA_ISPV4_CMD_OPCODE_PWR_OFF: {
			ret = cam_ispv4_power_down(s_ctrl);
		}
		break;
		case CAMERA_ISPV4_CMD_OPCODE_IONMAP_REGION: {
			struct ispv4_v4l2_dev *priv = s_ctrl->priv;
			struct ispv4_ionmap_para *ionmap_para;

			if (!priv->v4l2_ionmap.avalid) {
				ret = -ENODEV;
				goto end;
			}

			offset = (uint32_t *)&csl_packet->payload;
			offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
			cmd_desc = (struct cam_cmd_buf_desc *)(offset);

			ret = cam_mem_get_cpu_buf(cmd_desc->mem_handle,
				&cmd_buf_ptr, &len_of_buffer);
			if (ret) {
				CAM_ERR(CAM_ISPV4, "ionmap_region fail in get buffer: %d",
							ret);
				goto end;
			}
			remain_len = len_of_buffer - cmd_desc->offset;
			cmd_buf = (uint32_t *)(cmd_buf_ptr + cmd_desc->offset);
			ionmap_para = (struct ispv4_ionmap_para *)cmd_buf;

			if ((len_of_buffer < sizeof(struct ispv4_ionmap_para)) ||
			(cmd_desc->offset >
			(len_of_buffer - sizeof(struct ispv4_ionmap_para)))) {
				CAM_ERR(CAM_ISPV4, "ionmap_region cmd buf not enough buffer");
				ret = -EINVAL;
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}

			ret = priv->v4l2_ionmap.mapfd(priv->v4l2_ionmap.dev,
						      ionmap_para->fd,
						      ionmap_para->region,
						      &(ionmap_para->iova));
			if (ret) {
				CAM_ERR(CAM_ISPV4, "ionmap_region fd %d region %d map fail",
							ionmap_para->fd, ionmap_para->iova);
				ret = -EINVAL;
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}

			if (copy_to_user(u64_to_user_ptr(ioctl_ctrl->handle),
					&dev_config,
					sizeof(struct cam_config_dev_cmd))) {
				CAM_ERR(CAM_ISPV4, "Failed Copy to User");
				ret = -EFAULT;
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}
			CAM_DBG(CAM_ISPV4, "ionmap_region fd %d map success",
					 ionmap_para->fd);
			cam_mem_put_cpu_buf(cmd_desc->mem_handle);
		}
		break;
		case CAMERA_ISPV4_CMD_OPCODE_IONUNMAP_REGION: {
			struct ispv4_v4l2_dev *priv = s_ctrl->priv;
			struct ispv4_ionmap_para *ionmap_para;

			if (!priv->v4l2_ionmap.avalid) {
				ret = -ENODEV;
				goto end;
			}

			offset = (uint32_t *)&csl_packet->payload;
			offset += (csl_packet->cmd_buf_offset / sizeof(uint32_t));
			cmd_desc = (struct cam_cmd_buf_desc *)(offset);

			ret = cam_mem_get_cpu_buf(cmd_desc->mem_handle,
				&cmd_buf_ptr, &len_of_buffer);
			if (ret) {
				CAM_ERR(CAM_ISPV4, "ionunmap_region fail in get buffer: %d",
							ret);
				goto end;
			}
			remain_len = len_of_buffer - cmd_desc->offset;
			cmd_buf = (uint32_t *)(cmd_buf_ptr + cmd_desc->offset);
			ionmap_para = (struct ispv4_ionmap_para *)cmd_buf;

			if ((len_of_buffer < sizeof(struct ispv4_ionmap_para)) ||
			(cmd_desc->offset >
			(len_of_buffer - sizeof(struct ispv4_ionmap_para)))) {
				CAM_ERR(CAM_ISPV4, "ionunmap_region cmd buf not enough buffer");
				ret = -EINVAL;
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}

			ret = priv->v4l2_ionmap.unmap(priv->v4l2_ionmap.dev,
						      ionmap_para->region);
			if (ret) {
				CAM_ERR(CAM_ISPV4, "ionunmap_region fd %d region %d unmap fail",
							ionmap_para->fd, ionmap_para->iova);
				ret = -EINVAL;
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}

			if (copy_to_user(u64_to_user_ptr(ioctl_ctrl->handle),
					&dev_config,
					sizeof(struct cam_config_dev_cmd))) {
				CAM_ERR(CAM_ISPV4, "Failed Copy to User");
				ret = -EFAULT;
				cam_mem_put_cpu_buf(cmd_desc->mem_handle);
				goto end;
			}
			CAM_DBG(CAM_ISPV4, "ionunmap_region fd %d region %d unmap success",
					 ionmap_para->fd, ionmap_para->region);
			cam_mem_put_cpu_buf(cmd_desc->mem_handle);
		}
		break;
	}

end:
	cam_mem_put_cpu_buf(dev_config.packet_handle);
	return ret;
}

int32_t cam_ispv4_driver_cmd(struct cam_ispv4_ctrl_t *s_ctrl,
			     void *arg)
{
	struct cam_control *cmd = (struct cam_control *)arg;
	struct ispv4_v4l2_dev *priv;
	int ret = 0;

	if (!s_ctrl || !arg) {
		CAM_ERR(CAM_ISPV4, "s_ctrl is NULL");
		return -EINVAL;
	}

	priv = s_ctrl->priv;
	if (!priv) {
		CAM_ERR(CAM_ISPV4, "The ispv4 data struct is NULL!");
		return -EINVAL;
	}
	CAM_DBG(CAM_ISPV4, "ispv4 cam executive ops: %s start", GET_CAM_OPS_NAME(cmd->op_code));
	mutex_lock(&(s_ctrl->cam_ispv4_mutex));
	switch (cmd->op_code) {
	case CAM_ACQUIRE_DEV: {
		struct cam_sensor_acquire_dev ispv4_acq_dev;
		struct cam_create_dev_hdl bridge_params;

		if (s_ctrl->bridge_intf.device_hdl != -1) {
			CAM_ERR(CAM_ISPV4, "Device is already acquired");
			ret = -EINVAL;
			goto release_mutex;
		}
		ret = copy_from_user(&ispv4_acq_dev,
			u64_to_user_ptr(cmd->handle),
			sizeof(ispv4_acq_dev));
		if (ret < 0) {
			CAM_ERR(CAM_ISPV4, "Failed Copying from user");
			goto release_mutex;
		}

		bridge_params.session_hdl = ispv4_acq_dev.session_handle;
		bridge_params.ops = &s_ctrl->bridge_intf.ops;
		bridge_params.v4l2_sub_dev_flag = 0;
		bridge_params.media_entity_flag = 0;
		bridge_params.priv = s_ctrl;
		bridge_params.dev_id = CAM_ISPV4;

		ispv4_acq_dev.device_handle =
			cam_create_device_hdl(&bridge_params);
		s_ctrl->bridge_intf.device_hdl = ispv4_acq_dev.device_handle;
		s_ctrl->bridge_intf.session_hdl = ispv4_acq_dev.session_handle;

		if (u64_to_user_ptr(ispv4_acq_dev.info_handle) == NULL) {
			/*if hal need boot fw, this ptr will be null*/
			s_ctrl->fw_boot = true;
		} else {
			/*else this ptr will be something, we don't use*/
			s_ctrl->fw_boot = false;
		}
		ret = cam_ispv4_power_up(s_ctrl);
		if (ret < 0) {
			CAM_ERR(CAM_ISPV4, "cam_ispv4_turn_on fail");
			ret = -EINVAL;
			goto release_mutex;
		}

		if (priv->v4l2_rproc.ddr_kernel_data_update(priv->v4l2_rproc.rp)) {
			ispv4_acq_dev.info_handle = 0x1; //ddr update flag
			CAM_DBG(CAM_ISPV4, "ddr_kernel_data_update");
		}

		if (copy_to_user(u64_to_user_ptr(cmd->handle),
			&ispv4_acq_dev,
			sizeof(struct cam_sensor_acquire_dev))) {
			CAM_ERR(CAM_ISPV4, "Failed Copy to User");
			ret = -EFAULT;
			goto release_mutex;
		}

		s_ctrl->ispv4_state = CAM_ISPV4_ACQUIRE;
		CAM_DBG(CAM_ISPV4,
			"CAM_ACQUIRE_DEV Success");
	}
		break;

	case CAM_RELEASE_DEV:
		if ((s_ctrl->ispv4_state == CAM_ISPV4_INIT) ||
			(s_ctrl->ispv4_state == CAM_ISPV4_START)) {
			ret = -EINVAL;
			CAM_WARN(CAM_ISPV4,
			"Not in right state to release : %d",
			s_ctrl->ispv4_state);
			goto release_mutex;
		}

		if (s_ctrl->bridge_intf.link_hdl != -1) {
			CAM_ERR(CAM_ISPV4,
				"Device [%d] still active on link 0x%x",
				s_ctrl->ispv4_state,
				s_ctrl->bridge_intf.link_hdl);
			ret = -EAGAIN;
			goto release_mutex;
		}

		if (cmd->reserved > 0) {
			ret = cam_ispv4_power_down(s_ctrl);
			if (ret < 0) {
				CAM_ERR(CAM_ISPV4, "cam_ispv4_turn_off fail");
				ret = -EINVAL;
				goto release_mutex;
			}
		} else {
			CAM_INFO(CAM_ISPV4, "cam_ispv4_turn_off workaround");
		}

		ret = cam_destroy_device_hdl(s_ctrl->bridge_intf.device_hdl);
		if (ret < 0)
			CAM_ERR(CAM_ISPV4,
				"failed in destroying the device hdl");
		s_ctrl->bridge_intf.device_hdl = -1;
		s_ctrl->bridge_intf.link_hdl = -1;
		s_ctrl->bridge_intf.session_hdl = -1;

		s_ctrl->ispv4_state = CAM_ISPV4_INIT;
		CAM_DBG(CAM_ISPV4, "CAM_RELEASE_DEV Success");
		break;

	case CAM_START_DEV:
		ret = cam_ispv4_start_frame(s_ctrl);
		if (ret) {
			CAM_ERR(CAM_ISPV4, "ISPV4 STARTDEV state %d", s_ctrl->ispv4_state);
			ret = -EINVAL;
			goto release_mutex;
		}
		s_ctrl->ispv4_state = CAM_ISPV4_START;
		CAM_DBG(CAM_ISPV4, "ISPV4 STARTDEV state %d", s_ctrl->ispv4_state);
		break;

	case CAM_STOP_DEV: {
		if (s_ctrl->fw_boot) {
			struct ispv4_rpmsg_send_para *rpmsg_para = kmalloc(cmd->size, GFP_KERNEL);
			if (!rpmsg_para) {
				CAM_ERR(CAM_ISPV4, "STOPDEV kmalloc fail");
				ret = -ENOMEM;
				goto release_mutex;
			}
			ret = copy_from_user(rpmsg_para,
				u64_to_user_ptr(cmd->handle),
				cmd->size);
			if (ret < 0) {
				CAM_ERR(CAM_ISPV4, "Failed Copying from user");
				kfree(rpmsg_para);
				ret = -EINVAL;
				goto release_mutex;
			}

			if (!priv->v4l2_rpmsg.avalid) {
				kfree(rpmsg_para);
				ret = -ENODEV;
				goto release_mutex;
			}
			ret = priv->v4l2_rpmsg.send(priv->v4l2_rpmsg.rp,
							XM_ISPV4_IPC_EPT_RPMSG_ISP,
							rpmsg_para->cmd, rpmsg_para->len,
							(void*)rpmsg_para->data,
							FALSE, NULL);
			if (ret != 0) {
				CAM_ERR(CAM_ISPV4, "rpmsg-isp send fail ret = %d", ret);
				kfree(rpmsg_para);
				ret = -EINVAL;
				goto release_mutex;
			}

			if (copy_to_user(u64_to_user_ptr(cmd->handle),
					rpmsg_para,
					cmd->size)) {
				CAM_ERR(CAM_ISPV4, "Failed Copy to User");
				kfree(rpmsg_para);
				ret = -EINVAL;
				goto release_mutex;
			}
			kfree(rpmsg_para);
			CAM_DBG(CAM_ISPV4, "ISPV4 STOPDEV streamoff rpmsg success");
		}

		ret = cam_ispv4_stop_frame(s_ctrl);
		if (ret) {
			CAM_ERR(CAM_ISPV4, "ISPV4 STOPDEV state %d", s_ctrl->ispv4_state);
			ret = -EINVAL;
			goto release_mutex;
		}

		s_ctrl->ispv4_state = CAM_ISPV4_ACQUIRE;
		CAM_DBG(CAM_ISPV4, "ISPV4 CAM_STOP_DEV");
	}
		break;

	case CAM_CONFIG_DEV:
		ret = cam_ispv4_process_config(s_ctrl, arg);
		CAM_DBG(CAM_ISPV4, "CAM_CONFIG_DEV finish");
		break;

	case ISP_OPCODE_PWR_ON:
		ret = cam_ispv4_power_up(s_ctrl);
		CAM_DBG(CAM_ISPV4, "ISP_OPCODE_PWR_ON finish");
		break;

	case ISP_OPCODE_PWR_OFF:
		ret = cam_ispv4_power_down(s_ctrl);
		CAM_DBG(CAM_ISPV4, "ISP_OPCODE_PWR_OFF finish");
		break;

	case ISP_OPCODE_HDMA_TRANS: {
		struct ispv4_hdma_para hdma_para;
		uint32_t *data;

		CHECK_COMP_AVALID(priv->v4l2_pci);

		ret = copy_from_user(&hdma_para,
			u64_to_user_ptr(cmd->handle),
			sizeof(hdma_para));
		if (ret < 0) {
			CAM_ERR(CAM_ISPV4, "Failed Copying from user");
			goto release_mutex;
		}
		if (hdma_para.len == 0 || hdma_para.len > 4*1024*1024){
			CAM_ERR(CAM_ISPV4, "hdma trans size erro");
			ret = -EINVAL;
			goto release_mutex;
		}
		data = kzalloc(hdma_para.len, GFP_KERNEL);
		if (data == NULL) {
			ret = -ENOMEM;
			goto release_mutex;
		}

		if (hdma_para.hdma_dir == HDMA_FROM_DEVICE) {
			ret = copy_from_user(data,
				u64_to_user_ptr(hdma_para.data_handle),
				hdma_para.len);
			if (ret < 0) {
				CAM_ERR(CAM_ISPV4, "Failed Copying from user");
				goto release_mutex;
			}
		}

		ret = priv->v4l2_pci.hdma_trans(priv->v4l2_pci.pcidata,
						hdma_para.hdma_dir, data,
						hdma_para.len,
						hdma_para.ep_addr);
		if (ret != 0) {
			CAM_ERR(CAM_ISPV4, "pcie hdma transfer fail");
			goto release_mutex;
		}

		if (hdma_para.hdma_dir == HDMA_TO_DEVICE) {
			ret = copy_to_user(u64_to_user_ptr(hdma_para.data_handle),
					  data, hdma_para.len);
			if (ret < 0) {
				CAM_ERR(CAM_ISPV4, "Failed Copy to User");
				ret = -EFAULT;
				goto release_mutex;
			}
		}

		CAM_DBG(CAM_ISPV4, "pcie hdma transfer");
	}
		break;

	case ISP_OPCODE_IONMAP: {
		struct ispv4_ionmap_para ionmap_para;

		CHECK_COMP_AVALID(priv->v4l2_ionmap);

		ret = copy_from_user(&ionmap_para,
			u64_to_user_ptr(cmd->handle),
			sizeof(ionmap_para));
		if (ret < 0) {
			CAM_ERR(CAM_ISPV4, "Failed Copying from user");
			goto release_mutex;
		}

		ret = priv->v4l2_ionmap.mapfd(
			priv->v4l2_ionmap.dev,
			ionmap_para.fd,
			ionmap_para.region, &(ionmap_para.iova));
		if (ret != 0) {
			CAM_ERR(CAM_ISPV4, "map region failed, ret=%d", ret);
			goto release_mutex;
		}

		if (copy_to_user(u64_to_user_ptr(cmd->handle),
			&ionmap_para,
			sizeof(ionmap_para))) {
			CAM_ERR(CAM_ISPV4, "Failed Copy to User");
			ret = -EFAULT;
			goto release_mutex;
		}

		CAM_DBG(CAM_ISPV4, "map region %d fd:%d, ionmap_iova:0x%x",
			 ionmap_para.region, ionmap_para.fd, ionmap_para.iova);
	}
		break;

	case ISP_OPCODE_IONUNMAP: {
		struct ispv4_ionmap_para ionmap_para;

		CHECK_COMP_AVALID(priv->v4l2_ionmap);

		ret = copy_from_user(&ionmap_para,
			u64_to_user_ptr(cmd->handle),
			sizeof(ionmap_para));
		if (ret < 0) {
			CAM_ERR(CAM_ISPV4, "Failed Copying from user");
			goto release_mutex;
		}

		ret = priv->v4l2_ionmap.unmap(
			priv->v4l2_ionmap.dev,
			ionmap_para.region);
		if (ret != 0)
			goto release_mutex;
		CAM_DBG(CAM_ISPV4, "unmap region %d fd:%d",
			 ionmap_para.region, ionmap_para.fd);
	}
		break;

	case ISPV4_OPCODE_RPMSG_SEND_ISP: {
		struct ispv4_rpmsg_send_para rpmsg_para;

		CHECK_COMP_AVALID(priv->v4l2_rpmsg);

		ret = copy_from_user(&rpmsg_para,
			u64_to_user_ptr(cmd->handle),
			sizeof(rpmsg_para));
		if (ret < 0) {
			CAM_ERR(CAM_ISPV4, "Failed Copying from user");
			goto release_mutex;
		}

		ret = priv->v4l2_rpmsg.send(priv->v4l2_rpmsg.rp,
					    XM_ISPV4_IPC_EPT_RPMSG_ISP,
					    rpmsg_para.cmd, rpmsg_para.len,
					    (void*)rpmsg_para.data_handle,
					    TRUE, &rpmsg_para.msgid);
		if (ret != 0) {
			CAM_ERR(CAM_ISPV4, "rpmsg-isp send fail ret = %d", ret);
			goto release_mutex;
		}

		if (copy_to_user(u64_to_user_ptr(cmd->handle),
			&rpmsg_para,
			sizeof(rpmsg_para))) {
			CAM_ERR(CAM_ISPV4, "Failed Copy to User");
			ret = -EFAULT;
			goto release_mutex;
		}

		CAM_DBG(CAM_ISPV4, "rpmsg-isp send(cmd:%d, len:%d, msgid:%d)",
				rpmsg_para.cmd, rpmsg_para.len, rpmsg_para.msgid);
	}
		break;

	case ISPV4_OPCODE_RPMSG_RECV_ISP: {
		struct ispv4_rpmsg_recv_para rpmsg_para;

		CHECK_COMP_AVALID(priv->v4l2_rpmsg);

		ret = copy_from_user(&rpmsg_para,
			u64_to_user_ptr(cmd->handle),
			sizeof(rpmsg_para));
		if (ret < 0) {
			CAM_ERR(CAM_ISPV4, "Failed Copying from user");
			goto release_mutex;
		}

		ret = priv->v4l2_rpmsg.recv(priv->v4l2_rpmsg.rp,
					    XM_ISPV4_IPC_EPT_RPMSG_ISP,
					    rpmsg_para.cap,
					    (void *)rpmsg_para.data_handle,
					    TRUE);
		if (ret != 0) {
			CAM_ERR(CAM_ISPV4, "rpmsg-isp recv fail");
			goto release_mutex;
		}

		CAM_DBG(CAM_ISPV4, "rpmsg-isp recv");
	}
		break;

	case ISPV4_OPCODE_RPMSG_GETERR_ISP: {
		struct ispv4_rpmsg_recv_para rpmsg_para;

		CHECK_COMP_AVALID(priv->v4l2_rpmsg);

		ret = copy_from_user(&rpmsg_para,
			u64_to_user_ptr(cmd->handle),
			sizeof(rpmsg_para));
		if (ret < 0) {
			CAM_ERR(CAM_ISPV4, "Failed Copying from user");
			goto release_mutex;
		}

		ret = priv->v4l2_rpmsg.get_err(priv->v4l2_rpmsg.rp,
					       XM_ISPV4_IPC_EPT_RPMSG_ISP,
					       rpmsg_para.cap,
					       (void *)rpmsg_para.data_handle,
					       TRUE);
		if (ret != 0) {
			CAM_ERR(CAM_ISPV4, "rpmsg-isp get err msg fail");
			goto release_mutex;
		}

		CAM_DBG(CAM_ISPV4, "rpmsg-isp get err msg");
	}
		break;

	case ISPV4_OPCODE_RPROC_BOOT:
		CHECK_COMP_AVALID(priv->v4l2_rproc);
		ret = priv->v4l2_rproc.boot(priv->v4l2_rproc.rp, NULL);
		if (ret != 0) {
			CAM_ERR(CAM_ISPV4, "rproc boot failed, ret=%d", ret);
			goto release_mutex;
		}
		CAM_DBG(CAM_ISPV4, "rproc boot");
		break;

	case ISPV4_OPCODE_RPROC_SHUTDOWN:
		CHECK_COMP_AVALID(priv->v4l2_rproc);
		ret = priv->v4l2_rproc.shutdown(priv->v4l2_rproc.rp);
		if (ret != 0) {
			CAM_ERR(CAM_ISPV4, "rproc shutdown fail");
			goto release_mutex;
		}
		CAM_DBG(CAM_ISPV4, "rproc shutdown");
		break;

	case ISP_OPCODE_SUSPEND:
		CHECK_COMP_AVALID(priv->v4l2_pci);

		if (!priv->v4l2_pci.linkup) {
			CAM_ERR(CAM_ISPV4, "pci has been link down");
			ret = -EFAULT;
			goto release_mutex;
		}
		ret = priv->v4l2_pci.suspend_pci(priv->v4l2_pci.pcidev);
		if (ret) {
			CAM_ERR(CAM_ISPV4, "suspend pci fail");
			goto release_mutex;
		} else {
			CAM_DBG(CAM_ISPV4, "suspend pci success");
			priv->v4l2_pci.linkup = false;
		}
		break;

	case ISP_OPCODE_RESUME:
		CHECK_COMP_AVALID(priv->v4l2_pci);
		CHECK_COMP_AVALID(priv->v4l2_ctrl);
		if (priv->v4l2_pci.linkup) {
			CAM_ERR(CAM_ISPV4, "pci has been link up");
			goto release_mutex;
		}
#if !(IS_ENABLED(CONFIG_MIISP_CHIP))
		priv->v4l2_ctrl.ispv4_fpga_reset(priv->v4l2_ctrl.pdev);
#endif
		ret = priv->v4l2_pci.resume_pci(priv->v4l2_pci.pcidev);
		if (ret) {
			CAM_ERR(CAM_ISPV4, "resume pci fail");
			goto release_mutex;
		} else {
			CAM_DBG(CAM_ISPV4, "resume pci success");
			priv->v4l2_pci.linkup = true;
		}

		break;

	case ISP_OPCODE_CHANGE_SPI_SPEED: {
		uint32_t speed;
		CHECK_COMP_AVALID(priv->v4l2_spi);

		ret = copy_from_user(&speed,
			u64_to_user_ptr(cmd->handle),
			sizeof(speed));
		if (ret < 0) {
			CAM_ERR(CAM_ISPV4, "Failed Copying from user");
			goto release_mutex;
		}

		ret = priv->v4l2_spi.spi_speed(priv->v4l2_spi.spidev, speed);
		if (ret != 0) {
			CAM_ERR(CAM_ISPV4, "spi change speed fail");
			goto release_mutex;
		}
		CAM_DBG(CAM_ISPV4, "spi change speed to %d", speed);
	}
		break;

	default:
		CAM_ERR(CAM_ISPV4, "Invalid Opcode: %d", cmd->op_code);
		ret = -EINVAL;
		goto release_mutex;
	}

release_mutex:
	mutex_unlock(&(s_ctrl->cam_ispv4_mutex));

	return ret;
}

int cam_ispv4_publish_dev_info(struct cam_req_mgr_device_info *info)
{
	int ret = 0;
	struct cam_ispv4_ctrl_t *s_ctrl = NULL;

	if (!info)
		return -EINVAL;

	s_ctrl = (struct cam_ispv4_ctrl_t *)
		 cam_get_device_priv(info->dev_hdl);

	if (!s_ctrl) {
		CAM_ERR(CAM_ISPV4, "Device data is NULL");
		return -EINVAL;
	}

	info->dev_id = CAM_REQ_MGR_DEVICE_ISPV4;
	strlcpy(info->name, CAM_ISPV4_NAME, sizeof(info->name));
	info->p_delay = 2;
	info->trigger = CAM_TRIGGER_POINT_SOF;

	return ret;
}

int cam_ispv4_establish_link(struct cam_req_mgr_core_dev_link_setup *link)
{
	struct cam_ispv4_ctrl_t *s_ctrl = NULL;

	if (!link)
		return -EINVAL;

	s_ctrl = (struct cam_ispv4_ctrl_t *)
		cam_get_device_priv(link->dev_hdl);
	if (!s_ctrl) {
		CAM_ERR(CAM_ISPV4, "Device data is NULL");
		return -EINVAL;
	}

	mutex_lock(&s_ctrl->cam_ispv4_mutex);
	if (link->link_enable) {
		s_ctrl->bridge_intf.link_hdl = link->link_hdl;
		s_ctrl->bridge_intf.crm_cb = link->crm_cb;
	} else {
		s_ctrl->bridge_intf.link_hdl = -1;
		s_ctrl->bridge_intf.crm_cb = NULL;
	}
	mutex_unlock(&s_ctrl->cam_ispv4_mutex);

	return 0;
}

int32_t cam_ispv4_apply_request(struct cam_req_mgr_apply_request *apply)
{
	int32_t ret = 0;
	struct cam_ispv4_ctrl_t *s_ctrl = NULL;
	struct ispv4_v4l2_dev *priv;
	struct ispv4_sensor_meta *cmd_set;
	int frame_off = 0;
	ktime_t timestart, timestartrpc, timeendrpc, timeend;
	if (!apply)
		return -EINVAL;
	s_ctrl = (struct cam_ispv4_ctrl_t *) cam_get_device_priv(apply->dev_hdl);
	frame_off = apply->request_id % MAX_PER_FRAME_ARRAY;
	cmd_set = &s_ctrl->per_frame[frame_off];

	priv = s_ctrl->priv;
	if (!priv->v4l2_rpmsg.avalid) {
		CAM_ERR(CAM_ISPV4, "rpmsg-isp invalid");
		return -ENODEV;
	}
	timestart = ktime_get();

	mutex_lock(&s_ctrl->cam_ispv4_mutex);

	timestartrpc = ktime_get();

	ret = priv->v4l2_rpmsg.send(priv->v4l2_rpmsg.rp,
				    XM_ISPV4_IPC_EPT_RPMSG_ISP,
				    cmd_set->pkg.header.type, sizeof(struct ispv4_sensor_meta),
				    (void*)cmd_set,
				    FALSE, NULL);
	timeendrpc = ktime_get();
	mutex_unlock(&s_ctrl->cam_ispv4_mutex);
	timeend = ktime_get();
	if (ret != 0) {
		CAM_ERR(CAM_ISPV4, "rpmsg-isp send ispv4_cmd_set fail ret = %d, request id %d",
				ret, apply->request_id);
		return -EINVAL;
	}

	CAM_DBG(CAM_ISPV4, "rpmsg-isp send ispv4_cmd_set, request id %d, total takes %lld, rpc takes %lld", apply->request_id,
		ktime_to_ms(ktime_sub(timeend, timestart)),ktime_to_ms(ktime_sub(timeendrpc, timestartrpc)));

	return ret;
}

int32_t cam_ispv4_flush_request(struct cam_req_mgr_flush_request *flush_req)
{
	CAM_DBG(CAM_ISPV4, "ISPV4 Flush");
	return 0;
}
