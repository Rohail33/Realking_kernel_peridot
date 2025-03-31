#include "cam_ispv4_dev.h"
#include "cam_req_mgr_dev.h"
#include "cam_debug_util.h"
#include "cam_ispv4_core.h"
#include "linux/completion.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/workqueue.h>
#include <linux/genalloc.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/remoteproc.h>
#include <linux/component.h>
#include <linux/debugfs.h>

static int cam_isp_subscribe_event(struct v4l2_subdev *sd,
	struct v4l2_fh *fh,
	struct v4l2_event_subscription *sub)
{
	return v4l2_event_subscribe(fh, sub, CAM_SUBDEVICE_EVENT_MAX, NULL);
}

static int cam_isp_unsubscribe_event(struct v4l2_subdev *sd,
	struct v4l2_fh *fh,
	struct v4l2_event_subscription *sub)
{
	return v4l2_event_unsubscribe(fh, sub);
}

static long cam_ispv4_subdev_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, void *arg)
{
	int ret = 0;
	struct cam_ispv4_ctrl_t *s_ctrl =
		v4l2_get_subdevdata(sd);

	switch (cmd) {
	case VIDIOC_CAM_CONTROL:
		ret = cam_ispv4_driver_cmd(s_ctrl, arg);
		break;
	case CAM_SD_SHUTDOWN:
		cam_ispv4_crashdump(s_ctrl);
		if (s_ctrl->nopoweroff) {
			s_ctrl->nopoweroff = false;
			break;
		}
		ret = cam_ispv4_power_down(s_ctrl);
		CAM_INFO(CAM_ISPV4, "v4l shutdown cam_ispv4_power_down success");
		break;
	default:
		CAM_ERR(CAM_ISPV4, "Invalid ioctl cmd: %d", cmd);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int cam_ispv4_subdev_close(struct v4l2_subdev *sd,
	struct v4l2_subdev_fh *fh)
{
	struct cam_ispv4_ctrl_t *s_ctrl =
		v4l2_get_subdevdata(sd);

	if (!s_ctrl) {
		CAM_ERR(CAM_ISPV4, "s_ctrl ptr is NULL");
		return -EINVAL;
	}

	mutex_lock(&(s_ctrl->cam_ispv4_mutex));
	cam_ispv4_shutdown(s_ctrl);
	mutex_unlock(&(s_ctrl->cam_ispv4_mutex));

	return 0;
}

#ifdef CONFIG_COMPAT
static long cam_ispv4_init_subdev_do_ioctl(struct v4l2_subdev *sd,
					   unsigned int cmd,
					   unsigned long arg)
{
	struct cam_control cmd_data;
	int32_t ret = 0;

	if (copy_from_user(&cmd_data, (void __user *)arg,
		sizeof(cmd_data))) {
		CAM_ERR(CAM_ISPV4, "Failed to copy from user_ptr=%pK size=%zu",
			(void __user *)arg, sizeof(cmd_data));
		return -EFAULT;
	}

	switch (cmd) {
	case VIDIOC_CAM_CONTROL:
		if (cmd_data.size > (1<<30))
			return -EINVAL;
		ret = cam_ispv4_subdev_ioctl(sd, cmd, &cmd_data);
		if (ret < 0)
			CAM_ERR(CAM_ISPV4, "cam_ispv4_subdev_ioctl failed");
		break;
	default:
		CAM_ERR(CAM_ISPV4, "Invalid compat ioctl cmd_type: %d", cmd);
		ret = -EINVAL;
	}

	if (!ret) {
		if (copy_to_user((void __user *)arg, &cmd_data,
			sizeof(cmd_data))) {
			CAM_ERR(CAM_ISPV4,
				"Failed to copy to user_ptr=%pK size=%zu",
				(void __user *)arg, sizeof(cmd_data));
			ret = -EFAULT;
		}
	}

	return ret;
}
#endif

static struct v4l2_subdev_core_ops cam_ispv4_subdev_core_ops = {
	.ioctl = cam_ispv4_subdev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = cam_ispv4_init_subdev_do_ioctl,
#endif
	.subscribe_event = cam_isp_subscribe_event,
	.unsubscribe_event = cam_isp_unsubscribe_event,
};

static struct v4l2_subdev_ops cam_ispv4_subdev_ops = {
	.core = &cam_ispv4_subdev_core_ops,
};

static const struct v4l2_subdev_internal_ops cam_ispv4_internal_ops = {
	.close = cam_ispv4_subdev_close,
};

static int cam_ispv4_init_subdev_params(struct cam_ispv4_ctrl_t *s_ctrl)
{
	int ret = 0;

	s_ctrl->v4l2_dev_str.internal_ops =
		&cam_ispv4_internal_ops;
	s_ctrl->v4l2_dev_str.ops =
		&cam_ispv4_subdev_ops;
	strlcpy(s_ctrl->device_name, "cam-ispv4-driver",
		sizeof(s_ctrl->device_name));
	s_ctrl->v4l2_dev_str.name =
		s_ctrl->device_name;
	s_ctrl->v4l2_dev_str.sd_flags =
		(V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS);
	s_ctrl->v4l2_dev_str.ent_function =
		CAM_ISPV4_DEVICE_TYPE;
	s_ctrl->v4l2_dev_str.token = s_ctrl;

	ret = cam_register_subdev(&(s_ctrl->v4l2_dev_str));
	if (ret)
		CAM_ERR(CAM_ISPV4, "Fail with cam_register_subdev ret: %d", ret);

	return ret;
}

static void cam_ispv4_component_unbind(struct device *dev,
	struct device *master_dev, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cam_ispv4_ctrl_t *s_ctrl;

	s_ctrl = platform_get_drvdata(pdev);
	if (!s_ctrl) {
		CAM_ERR(CAM_ISPV4, "ISPV4 device is NULL");
		return;
	}

	CAM_INFO(CAM_ISPV4, "platform remove invoked");
	mutex_lock(&(s_ctrl->cam_ispv4_mutex));
	cam_ispv4_shutdown(s_ctrl);
	mutex_unlock(&(s_ctrl->cam_ispv4_mutex));
	cam_unregister_subdev(&(s_ctrl->v4l2_dev_str));

	platform_set_drvdata(pdev, NULL);
	v4l2_set_subdevdata(&(s_ctrl->v4l2_dev_str.sd), NULL);
}

static int cam_ispv4_component_bind(struct device *dev,
	struct device *master_dev, void *data_t)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cam_ispv4_ctrl_t *s_ctrl = NULL;
	struct ispv4_v4l2_dev *priv, **privp;
	int ret = -EIO;

	/* Create ISPV4 control structure */
	s_ctrl = devm_kzalloc(&pdev->dev,
			      sizeof(struct cam_ispv4_ctrl_t), GFP_KERNEL);
	if (!s_ctrl)
		return -ENOMEM;

	mutex_init(&s_ctrl->cam_ispv4_mutex);

	s_ctrl->of_node = pdev->dev.of_node;

	/* fill in platform device */
	s_ctrl->pdev = pdev;

	ret = cam_ispv4_init_subdev_params(s_ctrl);
	if (ret)
		return -EINVAL;

	s_ctrl->bridge_intf.device_hdl = -1;
	s_ctrl->bridge_intf.link_hdl = -1;
	s_ctrl->bridge_intf.ops.get_dev_info = cam_ispv4_publish_dev_info;
	s_ctrl->bridge_intf.ops.link_setup = cam_ispv4_establish_link;
	s_ctrl->bridge_intf.ops.apply_req = cam_ispv4_apply_request;
	s_ctrl->bridge_intf.ops.flush_req = cam_ispv4_flush_request;

	s_ctrl->ispv4_state = CAM_ISPV4_INIT;
	s_ctrl->fw_boot = true;
	init_completion(&s_ctrl->rpmsg_isp_ready);
	init_completion(&s_ctrl->rpmsg_asst_ready);

	privp = dev_get_platdata(dev);
	priv = *privp;
	s_ctrl->priv = priv;

	platform_set_drvdata(pdev, s_ctrl);

	if (!cam_debugfs_available()) {
		CAM_ERR(CAM_ISPV4, "DebugFS is not available");
		return 0;
	}

	ret = cam_debugfs_create_subdir("ispv4", &s_ctrl->dbgfileptr);
	if (ret) {
		CAM_ERR(CAM_ISPV4, "DebugFS could not create directory!");
	}
	debugfs_create_bool("carsh_no_power_off", 0666, s_ctrl->dbgfileptr, &s_ctrl->nopoweroff);
	s_ctrl->nopoweroff = false;
	CAM_INFO(CAM_ISPV4, "camera probe succesed !");

	return ret;
}

const static struct component_ops cam_ispv4_component_ops = {
	.bind = cam_ispv4_component_bind,
	.unbind = cam_ispv4_component_unbind,
};

static int32_t cam_ispv4_driver_platform_probe(struct platform_device *pdev)
{
	int ret = 0;

	ret = component_add(&pdev->dev, &cam_ispv4_component_ops);
	if (ret)
		CAM_ERR(CAM_ISPV4, "failed to add component rc: %d", ret);

	return ret;
}

static int cam_ispv4_platform_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &cam_ispv4_component_ops);
	return 0;
}

struct platform_driver cam_ispv4_platform_driver = {
	.probe = cam_ispv4_driver_platform_probe,
	.driver = {
		.name = "ispv4-cam",
		.owner = THIS_MODULE,
		.suppress_bind_attrs = true,
	},
	.remove = cam_ispv4_platform_remove,
};

int cam_ispv4_driver_init(void)
{
	int32_t ret = 0;

	ret = platform_driver_register(&cam_ispv4_platform_driver);
	if (ret < 0) {
		CAM_ERR(CAM_ISPV4, "platform_driver_register Failed: ret = %d",
			ret);
		return ret;
	}

	return ret;
}

void cam_ispv4_driver_exit(void)
{
	platform_driver_unregister(&cam_ispv4_platform_driver);
}

MODULE_DESCRIPTION("qcomcam_ispv4_driver");
MODULE_LICENSE("GPL v2");
