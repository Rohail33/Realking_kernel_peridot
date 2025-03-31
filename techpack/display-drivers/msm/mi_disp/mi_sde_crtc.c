/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "mi_sde_crtc:[%s:%d] " fmt, __func__, __LINE__

#include <drm/drm_crtc.h>
#include "mi_sde_connector.h"

void mi_sde_crtc_check_layer_flags(struct drm_crtc *crtc)
{
	struct drm_connector *conn;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(crtc->dev, &conn_iter);
	drm_for_each_connector_iter(conn, &conn_iter) {
		if (conn->state && (conn->state->crtc == crtc)
				&& (conn->connector_type == DRM_MODE_CONNECTOR_DSI)) {
			mi_sde_connector_check_layer_flags(conn);
		}
	}
	drm_connector_list_iter_end(&conn_iter);
}


