#ifndef _CAM_APERTURE_CORE_H_
#define _CAM_APERTURE_CORE_H_

#include "cam_aperture_dev.h"


/**
 * @power_info: power setting info to control the power
 *
 * This API construct the default aperture power setting.
 *
 * @return Status of operation. Negative in case of error. Zero otherwise.
 */
int32_t cam_aperture_construct_default_power_setting(
	struct cam_sensor_power_ctrl_t *power_info);

/**
 * @apply: Req mgr structure for applying request
 *
 * This API applies the request that is mentioned
 */
int32_t cam_aperture_apply_request(struct cam_req_mgr_apply_request *apply);

/**
 * @info: Sub device info to req mgr
 *
 * This API publish the subdevice info to req mgr
 */
int32_t cam_aperture_publish_dev_info(struct cam_req_mgr_device_info *info);

/**
 * @flush: Req mgr structure for flushing request
 *
 * This API flushes the request that is mentioned
 */
int cam_aperture_flush_request(struct cam_req_mgr_flush_request *flush);


/**
 * @link: Link setup info
 *
 * This API establishes link aperture subdevice with req mgr
 */
int32_t cam_aperture_establish_link(
	struct cam_req_mgr_core_dev_link_setup *link);

/**
 * @a_ctrl: Aperture ctrl structure
 * @arg:    Camera control command argument
 *
 * This API handles the camera control argument reached to aperture
 */
int32_t cam_aperture_driver_cmd(struct cam_aperture_ctrl_t *a_ctrl, void *arg);

/**
 * @a_ctrl: Aperture ctrl structure
 *
 * This API handles the shutdown ioctl/close
 */
void cam_aperture_shutdown(struct cam_aperture_ctrl_t *a_ctrl);

struct completion *cam_aperture_get_i3c_completion(uint32_t index);


#endif /* _CAM_APERTURE_CORE_H_ */