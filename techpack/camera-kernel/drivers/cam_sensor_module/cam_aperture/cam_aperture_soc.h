#ifndef _CAM_APERTURE_SOC_H_
#define _CAM_APERTURE_SOC_H_

#include "cam_aperture_dev.h"

/**
 * @a_ctrl: Aperture ctrl structure
 *
 * This API parses aperture device tree
 */
int cam_aperture_parse_dt(struct cam_aperture_ctrl_t *a_ctrl,
	struct device *dev);

#endif /* _CAM_APERTURE_SOC_H_ */