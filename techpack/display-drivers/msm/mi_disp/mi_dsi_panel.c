/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"mi-dsi-panel:[%s:%d] " fmt, __func__, __LINE__
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/rtc.h>
#include <linux/pm_wakeup.h>
#include <video/mipi_display.h>
#include <drm/mi_disp.h>

#include "dsi_panel.h"
#include "dsi_display.h"
#include "dsi_ctrl_hw.h"
#include "dsi_parser.h"
#include "internals.h"
#include "sde_connector.h"
#include "sde_trace.h"
#include "mi_dsi_panel.h"
#include "mi_disp_feature.h"
#include "mi_disp_print.h"
#include "mi_disp_parser.h"
#include "mi_dsi_display.h"
#include "mi_disp_lhbm.h"
#include "mi_panel_id.h"
#include "mi_disp_nvt_alpha_data.h"

#define to_dsi_display(x) container_of(x, struct dsi_display, host)

void mi_dsi_update_backlight_in_aod(struct dsi_panel *panel,
			bool restore_backlight);

static u64 g_panel_id[MI_DISP_MAX];

struct dsi_panel *g_panel;

typedef int (*mi_display_pwrkey_callback)(int);
extern void mi_display_pwrkey_callback_set(mi_display_pwrkey_callback);

static int mi_panel_id_init(struct dsi_panel *panel)
{
	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}
	g_panel = panel;
	g_panel_id[mi_get_disp_id(panel->type)] = panel->mi_cfg.mi_panel_id;

	return 0;
}

enum mi_project_panel_id mi_get_panel_id_by_dsi_panel(struct dsi_panel *panel)
{
	if (!panel) {
		DISP_ERROR("invalid params\n");
		return PANEL_ID_INVALID;
	}

	return mi_get_panel_id(panel->mi_cfg.mi_panel_id);
}

enum mi_project_panel_id mi_get_panel_id_by_disp_id(int disp_id)
{
	if (!is_support_disp_id(disp_id)) {
		DISP_ERROR("Unsupported display id\n");
		return PANEL_ID_INVALID;
	}

	return mi_get_panel_id(g_panel_id[disp_id]);
}

int mi_dsi_panel_pre_init(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = NULL;
	int rc = 0;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	mi_cfg->dsi_panel = panel;

	rc = panel->utils.read_u64(panel->utils.data, "mi,panel-id", &mi_cfg->mi_panel_id);
	if (rc) {
		mi_cfg->mi_panel_id = 0;
		DISP_ERROR("mi,panel-id not specified\n");
	} else {
		DISP_DEBUG("mi,panel-id is 0x%llx (%s)\n",
			mi_cfg->mi_panel_id, mi_get_panel_id_name(mi_cfg->mi_panel_id));
	}

	rc = mi_panel_id_init(panel);

	return rc;
}

int mi_dsi_panel_init(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;


	mutex_init(&mi_cfg->doze_lock);

	mi_cfg->disp_wakelock = wakeup_source_register(NULL, "disp_wakelock");
	if (!mi_cfg->disp_wakelock) {
		DISP_ERROR("doze_wakelock wake_source register failed");
		return -ENOMEM;
	}

	mi_dsi_panel_parse_config(panel);
	atomic_set(&mi_cfg->brightness_clone, 0);
	if ((mi_get_panel_id_by_dsi_panel(panel) == N3_PANEL_PA) ||
		(mi_get_panel_id_by_dsi_panel(panel) == N2_PANEL_PA) ||
		(mi_get_panel_id_by_dsi_panel(panel) == N1_PANEL_PA) ||
		(mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_PA) ||
		(mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_SA) ||
		(mi_get_panel_id_by_dsi_panel(panel) == N18_PANEL_PA) ||
		(mi_get_panel_id_by_dsi_panel(panel) == N18_PANEL_SA) ||
		(mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PA) ||
		(mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PB) ||
		(mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PC) ||
		(mi_get_panel_id_by_dsi_panel(panel) == O16U_PANEL_PA) ||
		(mi_get_panel_id_by_dsi_panel(panel) == O16U_PANEL_PB) ||
		(mi_get_panel_id_by_dsi_panel(panel) == O82_PANEL_PA) ||
		(mi_get_panel_id_by_dsi_panel(panel) == O82_PANEL_PB) ||
		(mi_get_panel_id_by_dsi_panel(panel) == O81_PANEL_PA) ||
		(mi_get_panel_id_by_dsi_panel(panel) == O81_PANEL_PB)) {
		mi_display_pwrkey_callback_set(mi_display_powerkey_callback);
	}
	return 0;
}

int mi_dsi_panel_deinit(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_cfg->disp_wakelock) {
		wakeup_source_unregister(mi_cfg->disp_wakelock);
	}
	return 0;
}

int mi_dsi_acquire_wakelock(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_cfg->disp_wakelock) {
		__pm_stay_awake(mi_cfg->disp_wakelock);
	}

	return 0;
}

int mi_dsi_release_wakelock(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_cfg->disp_wakelock) {
		__pm_relax(mi_cfg->disp_wakelock);
	}

	return 0;

}

bool is_aod_and_panel_initialized(struct dsi_panel *panel)
{
	if ((panel->power_mode == SDE_MODE_DPMS_LP1 ||
		panel->power_mode == SDE_MODE_DPMS_LP2) &&
		dsi_panel_initialized(panel)){
		return true;
	} else {
		return false;
	}
}

int mi_dsi_panel_esd_irq_ctrl(struct dsi_panel *panel,
				bool enable)
{
	int ret  = 0;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	ret = mi_dsi_panel_esd_irq_ctrl_locked(panel, enable);
	mutex_unlock(&panel->panel_lock);

	return ret;
}

int mi_dsi_panel_esd_irq_ctrl_locked(struct dsi_panel *panel,
				bool enable)
{
	struct mi_dsi_panel_cfg *mi_cfg;
	struct irq_desc *desc;

	if (!panel || !panel->panel_initialized) {
		DISP_ERROR("Panel not ready!\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	if (gpio_is_valid(mi_cfg->esd_err_irq_gpio)) {
		if (mi_cfg->esd_err_irq) {
			if (enable) {
				if (!mi_cfg->esd_err_enabled) {
					desc = irq_to_desc(mi_cfg->esd_err_irq);
					if (!irq_settings_is_level(desc))
						desc->istate &= ~IRQS_PENDING;
					enable_irq_wake(mi_cfg->esd_err_irq);
					enable_irq(mi_cfg->esd_err_irq);
					mi_cfg->esd_err_enabled = true;
					DISP_INFO("[%s] esd irq is enable\n", panel->type);
				}
			} else {
				if (mi_cfg->esd_err_enabled) {
					disable_irq_wake(mi_cfg->esd_err_irq);
					disable_irq_nosync(mi_cfg->esd_err_irq);
					mi_cfg->esd_err_enabled = false;
					DISP_INFO("[%s] esd irq is disable\n", panel->type);
				}
			}
		}
	} else {
		DISP_INFO("[%s] esd irq gpio invalid\n", panel->type);
	}

	if (mi_get_panel_id_by_dsi_panel(panel) == O82_PANEL_PA ||
		mi_get_panel_id_by_dsi_panel(panel) == O82_PANEL_PB ||
		mi_get_panel_id_by_dsi_panel(panel) == O81_PANEL_PA ||
		mi_get_panel_id_by_dsi_panel(panel) == O81_PANEL_PB) {
		if (gpio_is_valid(mi_cfg->esd_err_irq_gpio_second)) {
			if (mi_cfg->esd_err_irq_second) {
				if (enable) {
					if (!mi_cfg->esd_err_enabled_second) {
						desc = irq_to_desc(mi_cfg->esd_err_irq_second);
						if (!irq_settings_is_level(desc))
							desc->istate &= ~IRQS_PENDING;
						enable_irq_wake(mi_cfg->esd_err_irq_second);
						enable_irq(mi_cfg->esd_err_irq_second);
						mi_cfg->esd_err_enabled_second = true;
						DISP_INFO("[%s] esd irq second is enable\n", panel->type);
					}
				} else {
					if (mi_cfg->esd_err_enabled_second) {
						disable_irq_wake(mi_cfg->esd_err_irq_second);
						disable_irq_nosync(mi_cfg->esd_err_irq_second);
						mi_cfg->esd_err_enabled_second = false;
						DISP_INFO("[%s] esd irq second is disable\n", panel->type);
					}
				}
			}
		} else {
			DISP_INFO("[%s] esd irq second gpio invalid\n", panel->type);
		}
	}

	return 0;
}

static void mi_disp_set_dimming_delayed_work_handler(struct kthread_work *work)
{
	struct disp_delayed_work *delayed_work = container_of(work,
					struct disp_delayed_work, delayed_work.work);
	struct dsi_panel *panel = (struct dsi_panel *)(delayed_work->data);
	struct disp_feature_ctl ctl;

	memset(&ctl, 0, sizeof(struct disp_feature_ctl));
	ctl.feature_id = DISP_FEATURE_DIMMING;
	ctl.feature_val = FEATURE_ON;

	DISP_INFO("[%s] panel set backlight dimming on\n", panel->type);
	mi_dsi_acquire_wakelock(panel);
	mi_dsi_panel_set_disp_param(panel, &ctl);
	mi_dsi_release_wakelock(panel);

	kfree(delayed_work);
}

int mi_dsi_panel_tigger_dimming_delayed_work(struct dsi_panel *panel)
{
	int disp_id = 0;
	struct disp_feature *df = mi_get_disp_feature();
	struct disp_display *dd_ptr;
	struct disp_delayed_work *delayed_work;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	delayed_work = kzalloc(sizeof(*delayed_work), GFP_KERNEL);
	if (!delayed_work) {
		DISP_ERROR("failed to allocate delayed_work buffer\n");
		return -ENOMEM;
	}

	disp_id = mi_get_disp_id(panel->type);
	dd_ptr = &df->d_display[disp_id];

	kthread_init_delayed_work(&delayed_work->delayed_work,
			mi_disp_set_dimming_delayed_work_handler);
	delayed_work->dd_ptr = dd_ptr;
	delayed_work->wq = &dd_ptr->pending_wq;
	delayed_work->data = panel;

	return kthread_queue_delayed_work(dd_ptr->worker, &delayed_work->delayed_work,
				msecs_to_jiffies(panel->mi_cfg.panel_on_dimming_delay));
}


static void mi_disp_timming_switch_delayed_work_handler(struct kthread_work *work)
{
	struct disp_delayed_work *delayed_work = container_of(work,
					struct disp_delayed_work, delayed_work.work);
	struct dsi_panel *dsi_panel = (struct dsi_panel *)(delayed_work->data);

	mi_dsi_acquire_wakelock(dsi_panel);

	dsi_panel_acquire_panel_lock(dsi_panel);
	if (dsi_panel->panel_initialized) {
		dsi_panel_tx_cmd_set(dsi_panel, DSI_CMD_SET_MI_FRAME_SWITCH_MODE_SEC);
		DISP_INFO("DSI_CMD_SET_MI_FRAME_SWITCH_MODE_SEC\n");
	} else {
		DISP_ERROR("Panel not initialized, don't send DSI_CMD_SET_MI_FRAME_SWITCH_MODE_SEC\n");
	}
	dsi_panel_release_panel_lock(dsi_panel);

	mi_dsi_release_wakelock(dsi_panel);

	kfree(delayed_work);
}

int mi_dsi_panel_tigger_timming_switch_delayed_work(struct dsi_panel *panel)
{
	int disp_id = 0;
	struct disp_feature *df = mi_get_disp_feature();
	struct disp_display *dd_ptr;
	struct disp_delayed_work *delayed_work;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	delayed_work = kzalloc(sizeof(*delayed_work), GFP_KERNEL);
	if (!delayed_work) {
		DISP_ERROR("failed to allocate delayed_work buffer\n");
		return -ENOMEM;
	}

	disp_id = mi_get_disp_id(panel->type);
	dd_ptr = &df->d_display[disp_id];

	kthread_init_delayed_work(&delayed_work->delayed_work,
			mi_disp_timming_switch_delayed_work_handler);
	delayed_work->dd_ptr = dd_ptr;
	delayed_work->wq = &dd_ptr->pending_wq;
	delayed_work->data = panel;
	return kthread_queue_delayed_work(dd_ptr->worker, &delayed_work->delayed_work,
				msecs_to_jiffies(20));
}

int mi_dsi_update_switch_cmd_N3(struct dsi_panel *panel, u32 cmd_update_index, u32 index)
{
	struct dsi_cmd_update_info *info = NULL;
	struct dsi_display_mode *cur_mode = NULL;
	int i = 0;
	u8 dc_6C_cfg = 0;
	u8  cmd_update_count = 0;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;

	if (panel->mi_cfg.real_dc_state == FEATURE_ON) {
		dc_6C_cfg = cur_mode->priv_info->dc_on_6Creg;
	} else {
		dc_6C_cfg = cur_mode->priv_info->dc_off_6Creg;
	}

	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];

	for (i = 0; i < cmd_update_count; i++ ) {
		DISP_DEBUG("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
		if (info && info->mipi_address == 0x6C) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				index, info, &dc_6C_cfg, sizeof(dc_6C_cfg));
		}
		info++;
	}

	return 0;
}

int mi_dsi_update_switch_cmd_N8(struct dsi_panel *panel, u32 cmd_update_index, u32 index)
{
	struct dsi_cmd_update_info *info = NULL;
	struct dsi_display_mode *cur_mode = NULL;
	int i = 0;
	u8 doze_cmd[2] = {0};
	int size = 2;
	u8  cmd_update_count = 0;
	doze_cmd[0] = 0x00;
	doze_cmd[1] = 0x01;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;


	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];

	for (i = 0; i < cmd_update_count; i++ ) {
		DISP_ERROR("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
		if (info && info->mipi_address == 0x35) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				index, info, doze_cmd, size);
		}
		info++;
	}

	return 0;
}

int mi_dsi_set_switch_cmd_before(struct dsi_panel *panel, int fps_mode)
{
	int rc = 0;
	int last_fps = 0;
	int last_fps_mode = 0;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;
	struct mi_mode_info *mi_timing  = NULL;

	if (!panel || !panel->cur_mode) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}
	DISP_DEBUG("[%s] DSI_CMD_SET_MI_TIMING_SWITCH BEFOR, fps=%d\n",
		       panel->name, last_fps);
	mi_cfg = &panel->mi_cfg;
	last_fps = mi_cfg->last_refresh_rate;
	last_fps_mode = mi_cfg->last_fps_mode;
	mi_timing = &panel->cur_mode->mi_timing;

	if ((last_fps == 10 && (fps_mode == DDIC_MODE_AUTO))
		|| ((fps_mode == DDIC_MODE_IDLE) && (mi_timing->ddic_min_refresh_rate == 10)
		&& (last_fps_mode == DDIC_MODE_AUTO))) {
		DSI_DEBUG("[%s] N3 switch 10hz to auto 120hz remove delay cmd, rc=%d\n",
		       panel->name, rc);
		return rc;
	}

	if (last_fps == 120 && ((last_fps_mode == DDIC_MODE_AUTO) || (last_fps_mode == DDIC_MODE_QSYNC))) {
		mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_TIMING_SWITCH_FROM_AUTO_UPDATE,
					DSI_CMD_SET_MI_TIMING_SWITCH_FROM_AUTO);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_TIMING_SWITCH_FROM_AUTO);
	} else if (last_fps >= 90) {
		mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_TIMING_SWITCH_FROM_NORMAL_UPDATE,
					DSI_CMD_SET_MI_TIMING_SWITCH_FROM_NORMAL);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_TIMING_SWITCH_FROM_NORMAL);
	} else if (last_fps >= 24) {
		mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_TIMING_SWITCH_FROM_SKIP_UPDATE,
					DSI_CMD_SET_MI_TIMING_SWITCH_FROM_SKIP);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_TIMING_SWITCH_FROM_SKIP);
	} else if (last_fps >= 1) {
		mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_TIMING_SWITCH_FROM_AUTO_UPDATE,
					DSI_CMD_SET_MI_TIMING_SWITCH_FROM_AUTO);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_TIMING_SWITCH_FROM_AUTO);
	}
	if (rc)
		DSI_INFO("[%s] N3 failed to send DSI_CMD_SET_MI_TIMING_SWITCH BEFOR cmds, rc=%d\n",
		       panel->name, rc);

	return rc;
}

int mi_dsi_update_switch_cmd(struct dsi_panel *panel)
{
	struct dsi_cmd_update_info *info = NULL;
	struct dsi_display_mode *cur_mode = NULL;
	int i = 0;
	u8 gamma_26_cfg = 0;
	u8 gamma_c0_cfg = 0;
	u8 gamma_df_cfg = 0;
	u32 cmd_update_index = 0;
	u8  cmd_update_count = 0;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;

	if (panel->mi_cfg.feature_val[DISP_FEATURE_FLAT_MODE] == FEATURE_ON) {
		gamma_26_cfg = cur_mode->priv_info->flat_on_26reg_gamma;
		gamma_c0_cfg = cur_mode->priv_info->flat_on_C0reg_gamma;
		gamma_df_cfg = cur_mode->priv_info->flat_on_DFreg_gamma;
	} else {
		gamma_26_cfg = cur_mode->priv_info->flat_off_26reg_gamma;
		gamma_c0_cfg = cur_mode->priv_info->flat_off_C0reg_gamma;
		gamma_df_cfg = cur_mode->priv_info->flat_off_DFreg_gamma;
	}

	cmd_update_index = DSI_CMD_SET_TIMING_SWITCH_UPDATE;
	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];

	for (i = 0; i < cmd_update_count; i++ ) {
		DISP_DEBUG("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
		if (info && info->mipi_address == 0x26) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				DSI_CMD_SET_TIMING_SWITCH, info, &gamma_26_cfg, sizeof(gamma_26_cfg));
		} else if (info && info->mipi_address == 0xC0) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				DSI_CMD_SET_TIMING_SWITCH, info, &gamma_c0_cfg, sizeof(gamma_c0_cfg));
		} else if (info && info->mipi_address == 0xDF) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				DSI_CMD_SET_TIMING_SWITCH, info, &gamma_df_cfg, sizeof(gamma_df_cfg));
		}
		info++;
	}

	return 0;
}

static inline bool mi_dsi_update_BA_reg(struct dsi_panel *panel, u8 *val, u8 size)
{
	u32 last_framerate = 0;
	u32 cur_framerate = 0;
	u32 fps_mode = 0;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;
	struct mi_mode_info *mi_timing  = NULL;
	bool ret = false;
	u8 *framerate_val_ptr = NULL;

	u8 switch_framerate_40HZ_cfg[2][9] = {
		/*120HZ switch to 40HZ*/
		{0x91,0x01,0x01,0x00,0x01,0x02,0x02,0x00,0x00},
		/*60/30/24/1HZ switch to 40HZ*/
		{0x91,0x02,0x02,0x00,0x01,0x02,0x02,0x00,0x00}
	};
	u8 switch_framerate_30HZ_cfg[2][9] = {
		/*120HZ switch to 30HZ*/
		{0x91,0x01,0x01,0x00,0x01,0x03,0x03,0x00,0x00},
		/*60/30/24/1HZ switch to 30HZ*/
		{0x91,0x03,0x03,0x00,0x01,0x03,0x03,0x00,0x00}
	};
	u8 switch_framerate_24HZ_cfg[3][9] = {
		/*120HZ switch to 24HZ*/
		{0x91,0x04,0x04,0x00,0x01,0x04,0x04,0x00,0x00},
		/*60HZ switch to 24HZ*/
		{0x91,0x04,0x04,0x00,0x01,0x04,0x04,0x00,0x00},
		/*40/30/1HZ switch to 24HZ*/
		{0x91,0x04,0x04,0x00,0x01,0x04,0x04,0x00,0x00}
	};
	u8 switch_framerate_1HZ_cfg[3][9] = {
		/*120HZ switch to 1HZ*/
		{0x91,0x0B,0x01,0x00,0x11,0x77,0x77,0x00,0x05},
		/*60/40HZ switch to 1HZ*/
		{0x91,0x0B,0x03,0x00,0x11,0x77,0x77,0x00,0x05},
		/*30/24HZ switch to 1HZ*/
		{0x91,0x0B,0x0B,0x00,0x11,0x77,0x77,0x00,0x05}
	};

	if (!panel || !panel->cur_mode) {
		DISP_ERROR("invalid params\n");
		ret = false;
		goto exit;
	}

	mi_cfg = &panel->mi_cfg;
	mi_timing = &panel->cur_mode->mi_timing;
	last_framerate = mi_cfg->last_refresh_rate;

	if(mi_timing->ddic_mode == DDIC_MODE_NORMAL)
		cur_framerate = panel->cur_mode->timing.refresh_rate;
	else if (mi_timing->ddic_mode == DDIC_MODE_IDLE)
		cur_framerate = mi_timing->ddic_min_refresh_rate;
	else if ((mi_timing->ddic_mode == DDIC_MODE_AUTO) || (fps_mode == DDIC_MODE_QSYNC))
		cur_framerate = mi_timing->sf_refresh_rate;

	DISP_DEBUG("ddic mode:%d, last fps:%d, cur_fps:%d\n", mi_timing->ddic_mode, last_framerate, cur_framerate);

	switch (cur_framerate) {
		case 120:
		case 90:
		case 60:
			framerate_val_ptr = NULL;
			ret = false;
			break;
		case 40:
			if(last_framerate == 120 || last_framerate == 90)
				framerate_val_ptr = switch_framerate_40HZ_cfg[0];
			else
				framerate_val_ptr = switch_framerate_40HZ_cfg[1];
			break;
		case 30:
			if(last_framerate == 120 || last_framerate == 90)
				framerate_val_ptr = switch_framerate_30HZ_cfg[0];
			else
				framerate_val_ptr = switch_framerate_30HZ_cfg[1];
			break;
		case 24:
			if(last_framerate == 120 || last_framerate == 90)
				framerate_val_ptr = switch_framerate_24HZ_cfg[0];
			else if (last_framerate == 60)
				framerate_val_ptr = switch_framerate_24HZ_cfg[1];
			else
				framerate_val_ptr = switch_framerate_24HZ_cfg[2];
			break;
		case 1:
			if(last_framerate == 120 || last_framerate == 90)
				framerate_val_ptr = switch_framerate_1HZ_cfg[0];
			else if (last_framerate == 60 || last_framerate == 40)
				framerate_val_ptr = switch_framerate_1HZ_cfg[1];
			else
				framerate_val_ptr = switch_framerate_1HZ_cfg[2];
			break;
		default:
			framerate_val_ptr = NULL;
			break;
	}

	if(framerate_val_ptr != NULL) {
		memcpy(val, framerate_val_ptr, size);
		ret = true;
	}

exit:
	return ret;
}

int mi_dsi_update_flat_mode_on_cmd(struct dsi_panel *panel,
		enum dsi_cmd_set_type type)
{
	struct dsi_cmd_update_info *info = NULL;
	struct dsi_display_mode *cur_mode = NULL;
	u8 update_D5_cfg = 0;
	u32 cmd_update_index = 0;
	u8  cmd_update_count = 0;
	int i = 0;

	if (type != DSI_CMD_SET_MI_FLAT_MODE_ON) {
		DISP_ERROR("invalid type parameter\n");
		return -EINVAL;
	}

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;

	cmd_update_index = DSI_CMD_SET_MI_FLAT_MODE_ON_UPDATE;
	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for (i = 0; i < cmd_update_count; i++ ) {
		if (info && info->mipi_address == 0xD5) {
			update_D5_cfg = 0x73;
			DISP_INFO("panel id is 0x%x, update_D5_cfg is 0x73", panel->id_config.build_id);
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				type, info,
				&update_D5_cfg, sizeof(update_D5_cfg));
		}

		info++;
	}

	return 0;
}


int mi_dsi_update_timing_switch_and_flat_mode_cmd(struct dsi_panel *panel,
		enum dsi_cmd_set_type type)
{
	struct dsi_cmd_update_info *info = NULL;
	struct dsi_display_mode *cur_mode = NULL;
	int i = 0;
	u8 gamma_2F_cfg = 0;
	u8 gamma_26_cfg = 0;
	u32 cmd_update_index = 0;
	u8  cmd_update_count = 0;
	bool is_update_BA_reg = false;
	u8 update_BA_reg_val[9] = {0};
	bool flat_mode_enable = false;

	if (mi_get_panel_id_by_dsi_panel(panel) != N2_PANEL_PA && mi_get_panel_id_by_dsi_panel(panel) != N1_PANEL_PA) {
		DISP_DEBUG("this project not need to dynamically update these parameters\n");
		return 0;
	}

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;

	switch (type) {
		case DSI_CMD_SET_TIMING_SWITCH:
			cmd_update_index = DSI_CMD_SET_TIMING_SWITCH_UPDATE;
			flat_mode_enable = panel->mi_cfg.feature_val[DISP_FEATURE_FLAT_MODE];
			is_update_BA_reg = mi_dsi_update_BA_reg(panel, update_BA_reg_val, sizeof(update_BA_reg_val));
			break;
		case DSI_CMD_SET_MI_FLAT_MODE_ON:
			cmd_update_index = DSI_CMD_SET_MI_FLAT_MODE_ON_UPDATE;
			flat_mode_enable = true;
			break;
		case DSI_CMD_SET_MI_FLAT_MODE_OFF:
			cmd_update_index = DSI_CMD_SET_MI_FLAT_MODE_OFF_UPDATE;
			flat_mode_enable = false;
			break;
		case DSI_CMD_SET_MI_DOZE_HBM:
			cmd_update_index = DSI_CMD_SET_MI_DOZE_HBM_UPDATE;
			flat_mode_enable = false;
			break;
		case DSI_CMD_SET_MI_DOZE_LBM:
			cmd_update_index = DSI_CMD_SET_MI_DOZE_LBM_UPDATE;
			flat_mode_enable = false;
			break;
		default:
			DISP_ERROR("[%s] unsupport cmd %s\n",
				panel->type, cmd_set_prop_map[type]);
			return -EINVAL;
	}

	if (flat_mode_enable) {
		gamma_2F_cfg = cur_mode->priv_info->flat_on_2Freg_gamma;
		gamma_26_cfg = cur_mode->priv_info->flat_on_26reg_gamma;
	} else {
		gamma_2F_cfg = cur_mode->priv_info->flat_off_2Freg_gamma;
		gamma_26_cfg = cur_mode->priv_info->flat_off_26reg_gamma;
	}

	DISP_DEBUG("panel id is 0x%x, gamma_2Freg_cfg is 0x%x, gamma_26reg_cfg is 0x%x",
			panel->id_config.build_id, gamma_2F_cfg, gamma_26_cfg);

	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for (i = 0; i < cmd_update_count; i++ ) {

		if (info && info->mipi_address == 0x2F) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				type, info,
				&gamma_2F_cfg, sizeof(gamma_2F_cfg));
		}

		if (info && info->mipi_address == 0x26) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				type, info,
				&gamma_26_cfg, sizeof(gamma_26_cfg));
		}

		if (info && info->mipi_address == 0xBA && is_update_BA_reg) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				type, info,
				update_BA_reg_val, sizeof(update_BA_reg_val));
		}

		info++;
	}

	return 0;
}

int mi_dsi_panel_set_gamma_update_reg(struct dsi_panel *panel, u32 bl_lvl)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;
	u32 last_refresh_rate;
	u32 vsync_period_us;
	ktime_t cur_time;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_cfg->panel_state != PANEL_STATE_ON) {
		DISP_INFO("%s exit when panel state(%d)\n", __func__, mi_cfg->panel_state);
		return 0;
	}

	last_refresh_rate = mi_cfg->last_refresh_rate;
	vsync_period_us = 1000000 / last_refresh_rate;

	if (mi_cfg->nedd_auto_update_gamma) {
		cur_time = ktime_get();
		if (last_refresh_rate && (ktime_to_us(cur_time - mi_cfg->last_mode_switch_time)
				<= vsync_period_us)) {
			DISP_INFO("sleep before update gamma, vsync_period = %d, diff_time = %llu",
					vsync_period_us,
					ktime_to_us(cur_time - mi_cfg->last_mode_switch_time));
			usleep_range(vsync_period_us, vsync_period_us + 10);
		}
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_AUTO_UPDATE_GAMMA);
		DISP_INFO("send auto update gamma cmd\n");
		if (rc)
			DISP_ERROR("failed to set DSI_CMD_SET_MI_AUTO_UPDATE_GAMMA\n");
		else
			mi_cfg->nedd_auto_update_gamma = false;
	}

	return rc;
}

int mi_dsi_panel_set_gamma_update_state(struct dsi_panel *panel)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_cfg->panel_state != PANEL_STATE_ON) {
		DISP_INFO("%s exit when panel state(%d)\n", __func__, mi_cfg->panel_state);
		return 0;
	}

	mutex_lock(&panel->panel_lock);

	mi_cfg->nedd_auto_update_gamma = true;
	DISP_DEBUG("set auto update gamma state to true\n");

	mutex_unlock(&panel->panel_lock);

	return rc;
}

int mi_dsi_first_timing_switch(struct dsi_panel *panel)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;
	u32 last_refresh_rate;
	u32 vsync_period_us;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_cfg->panel_state != PANEL_STATE_ON) {
		DISP_INFO("%s exit when panel state(%d)\n", __func__, mi_cfg->panel_state);
		return 0;
	}

	last_refresh_rate = mi_cfg->last_refresh_rate;
	vsync_period_us = 1000000 / last_refresh_rate;

	if (mi_cfg->first_timing_switch) {
		DISP_INFO("sleep after timing switch, vsync_period = %d", vsync_period_us);
		usleep_range(vsync_period_us, vsync_period_us + 10);

		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_AUTO_UPDATE_GAMMA);
		DISP_INFO("send auto update gamma cmd\n");
		if (rc)
			DISP_ERROR("failed to set DSI_CMD_SET_MI_AUTO_UPDATE_GAMMA\n");
		else
			mi_cfg->first_timing_switch = false;
	}

	return rc;
}

bool is_hbm_fod_on(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;
	int feature_val;
	bool is_fod_on = false;

	feature_val = mi_cfg->feature_val[DISP_FEATURE_LOCAL_HBM];
	switch (feature_val) {
	case LOCAL_HBM_NORMAL_WHITE_1000NIT:
	case LOCAL_HBM_NORMAL_WHITE_750NIT:
	case LOCAL_HBM_NORMAL_WHITE_500NIT:
	case LOCAL_HBM_NORMAL_WHITE_110NIT:
	case LOCAL_HBM_NORMAL_GREEN_500NIT:
	case LOCAL_HBM_HLPM_WHITE_1000NIT:
	case LOCAL_HBM_HLPM_WHITE_110NIT:
		is_fod_on = true;
		break;
	default:
		break;
	}

	if (mi_cfg->feature_val[DISP_FEATURE_HBM_FOD] == FEATURE_ON) {
		is_fod_on = true;
	}

	return is_fod_on;
}

bool is_dc_on_skip_backlight(struct dsi_panel *panel, u32 bl_lvl)
{
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;

	if (!mi_cfg->dc_feature_enable)
		return false;
	if (mi_cfg->feature_val[DISP_FEATURE_DC] == FEATURE_OFF)
		return false;
	if (mi_cfg->dc_type == TYPE_CRC_SKIP_BL && bl_lvl < mi_cfg->dc_threshold)
		return true;
	else
		return false;
}

bool is_backlight_set_skip(struct dsi_panel *panel, u32 bl_lvl)
{
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;

	if (mi_cfg->in_fod_calibration) {
		DISP_INFO("[%s] skip set backlight %d due to fod calibration\n", panel->type, bl_lvl);
		return true;
	} else if (is_hbm_fod_on(panel)) {
		if (bl_lvl != 0) {
			if ((mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N3_PANEL_PA || mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N9_PANEL_PA) && is_hbm_fod_on(panel)) {
				DISP_DEBUG("[%s] skip set backlight %d due to LHBM is on", panel->type, bl_lvl);
				return true;
			}
			DISP_INFO("[%s] update 51 reg to %d even LHBM is on,",
				panel->type, bl_lvl);
			return false;
		} else {
			DISP_INFO("[%s] skip set backlight %d due to fod hbm\n", panel->type, bl_lvl);
		}
		return true;
	} else if (bl_lvl != 0 && is_dc_on_skip_backlight(panel, bl_lvl)) {
		DISP_DEBUG("[%s] skip set backlight %d due to DC on\n", panel->type, bl_lvl);
		return true;
	} else if (panel->power_mode == SDE_MODE_DPMS_LP1) {
		if (bl_lvl == 0) {
			DISP_INFO("[%s] skip set backlight %d due to LP1 on\n", panel->type, bl_lvl);
			return true;
		}
		if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N3_PANEL_PA ||
			mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N2_PANEL_PA ||
			mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N11U_PANEL_PA ||
			mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N1_PANEL_PA ||
			mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N8_PANEL_PA ||
			mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N8_PANEL_SA ||
			mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N18_PANEL_PA ||
			mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N18_PANEL_SA) {
			DISP_INFO("[%s] skip set backlight %d due to LP1 on\n", panel->type, bl_lvl);
			return true;
		}
		return false;
	} else {
		return false;
	}
}

void mi_dsi_panel_update_last_bl_level(struct dsi_panel *panel, int brightness)
{
	struct mi_dsi_panel_cfg *mi_cfg;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return;
	}

	mi_cfg = &panel->mi_cfg;

	if ((mi_cfg->last_bl_level == 0 || mi_cfg->dimming_state == STATE_DIM_RESTORE) &&
		brightness > 0 && !is_hbm_fod_on(panel) && !mi_cfg->in_fod_calibration) {
		mi_dsi_panel_tigger_dimming_delayed_work(panel);

		if (mi_cfg->dimming_state == STATE_DIM_RESTORE)
			mi_cfg->dimming_state = STATE_NONE;
	}

	if (mi_cfg->last_bl_level == 0 && brightness && (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == O81_PANEL_PA ||
		mi_get_panel_id(panel->mi_cfg.mi_panel_id) == O81_PANEL_PB ||
		mi_get_panel_id(panel->mi_cfg.mi_panel_id) == O82_PANEL_PA ||
		mi_get_panel_id(panel->mi_cfg.mi_panel_id) == O82_PANEL_PB)){
			dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_DISABLE_INSERT_BLACK);
			DSI_INFO("disable insert black \n");
	}

	mi_cfg->last_bl_level = brightness;
	if (brightness != 0)
		mi_cfg->last_no_zero_bl_level = brightness;

	return;
}

int mi_dsi_print_51_backlight_log(struct dsi_panel *panel,
		struct dsi_cmd_desc *cmd)
{
	u8 *buf = NULL;
	u32 bl_lvl = 0;
	int i = 0;
	struct mi_dsi_panel_cfg *mi_cfg;
	static int use_count = 20;

	if (!panel || !cmd) {
		DISP_ERROR("Invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	buf = (u8 *)cmd->msg.tx_buf;
	if (buf && buf[0] == MIPI_DCS_SET_DISPLAY_BRIGHTNESS) {
		if (cmd->msg.tx_len >= 3) {
			if (panel->bl_config.bl_inverted_dbv)
				bl_lvl = (buf[1] << 8) | buf[2];
			else
				bl_lvl = buf[1] | (buf[2] << 8);
#ifndef CONFIG_FACTORY_BUILD
			if (use_count-- > 0)
#endif
				DISP_TIME_INFO("[%s] set 51 backlight %d\n", panel->type, bl_lvl);

			if (!mi_cfg->last_bl_level || !bl_lvl) {
					use_count = 20;
			}
		}

		if (mi_get_backlight_log_mask() & BACKLIGHT_LOG_ENABLE) {
			DISP_INFO("[%s] [0x51 backlight debug] tx_len = %d\n",
					panel->type, cmd->msg.tx_len);
			for (i = 0; i < cmd->msg.tx_len; i++) {
				DISP_INFO("[%s] [0x51 backlight debug] tx_buf[%d] = 0x%02X\n",
					panel->type, i, buf[i]);
			}

			if (mi_get_backlight_log_mask() & BACKLIGHT_LOG_DUMP_STACK)
				dump_stack();
		}
	}

	return 0;
}


int mi_dsi_panel_parse_sub_timing(struct mi_mode_info *mi_mode,
				  struct dsi_parser_utils *utils)
{
	int rc = 0;
	const char *ddic_mode;

	rc = utils->read_u32(utils->data,
				"qcom,mdss-dsi-panel-framerate",
				&mi_mode->timing_refresh_rate);
	if (rc) {
		DISP_ERROR("failed to read qcom,mdss-dsi-panel-framerate, rc=%d\n",
		       rc);
		goto error;
	}

	ddic_mode = utils->get_property(utils->data, "mi,mdss-dsi-ddic-mode", NULL);
	if (ddic_mode) {
		if (!strcmp(ddic_mode, "normal")) {
			mi_mode->ddic_mode = DDIC_MODE_NORMAL;
		} else if (!strcmp(ddic_mode, "idle")) {
			mi_mode->ddic_mode= DDIC_MODE_IDLE;
		} else if (!strcmp(ddic_mode, "auto")) {
			mi_mode->ddic_mode= DDIC_MODE_AUTO;
		} else if (!strcmp(ddic_mode, "qsync")) {
			mi_mode->ddic_mode = DDIC_MODE_QSYNC;
		} else if (!strcmp(ddic_mode, "diff")) {
			mi_mode->ddic_mode = DDIC_MODE_DIFF;
		} else if (!strcmp(ddic_mode, "test")) {
			mi_mode->ddic_mode = DDIC_MODE_TEST;
		} else {
			DISP_INFO("Unrecognized ddic mode, default is normal mode\n");
			mi_mode->ddic_mode = DDIC_MODE_NORMAL;
		}
	} else {
		DISP_DEBUG("Falling back ddic mode to default normal mode\n");
		mi_mode->ddic_mode = DDIC_MODE_NORMAL;
		mi_mode->sf_refresh_rate = mi_mode->timing_refresh_rate;
		mi_mode->ddic_min_refresh_rate = mi_mode->timing_refresh_rate;
	}

    if (mi_mode->ddic_mode != DDIC_MODE_NORMAL) {
		rc = utils->read_u32(utils->data,
				"mi,mdss-dsi-sf-framerate",
				  &mi_mode->sf_refresh_rate);
		if (rc) {
			DISP_ERROR("failed to read mi,mdss-dsi-sf-framerate, rc=%d\n", rc);
			goto error;
		}

		rc = utils->read_u32(utils->data,
				"mi,mdss-dsi-ddic-min-framerate",
				  &mi_mode->ddic_min_refresh_rate);
		if (rc) {
			DISP_ERROR("failed to read mi,mdss-dsi-ddic-min-framerate, rc=%d\n", rc);
			goto error;
		}
	}

	DISP_INFO("ddic_mode:%s, sf_refresh_rate:%d, ddic_min_refresh_rate:%d\n",
		get_ddic_mode_name(mi_mode->ddic_mode),
		mi_mode->sf_refresh_rate, mi_mode->ddic_min_refresh_rate);

error:
	return rc;
}


static int mi_dsi_panel_parse_cmd_sets_update_sub(struct dsi_panel *panel,
		struct dsi_display_mode *mode, enum dsi_cmd_set_type type)
{
	int rc = 0;
	int j = 0, k = 0;
	u32 length = 0;
	u32 count = 0;
	u32 size = 0;
	u32 *arr_32 = NULL;
	const u32 *arr;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_cmd_update_info *info;
	int type1 = -1;

	if (!mode || !mode->priv_info) {
		DISP_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	arr = utils->get_property(utils->data, cmd_set_update_map[type],
		&length);

	if (!arr) {
		DISP_DEBUG("[%s] commands not defined\n", cmd_set_update_map[type]);
		return rc;
	}

	if (length & 0x1) {
		DISP_ERROR("[%s] syntax error\n", cmd_set_update_map[type]);
		rc = -EINVAL;
		goto error;
	}

	DISP_INFO("%s length = %d\n", cmd_set_update_map[type], length);

	for (j = DSI_CMD_SET_PRE_ON; j < DSI_CMD_SET_MAX; j++) {
		if (strstr(cmd_set_update_map[type], cmd_set_prop_map[j])) {
			type1 = j;
			DISP_INFO("find type(%d) is [%s] commands\n", type1,
				cmd_set_prop_map[type1]);
			break;
		}
	}
	if (type1 < 0 || j == DSI_CMD_SET_MAX) {
		rc = -EINVAL;
		goto error;
	}

	length = length / sizeof(u32);
	size = length * sizeof(u32);

	arr_32 = kzalloc(size, GFP_KERNEL);
	if (!arr_32) {
		rc = -ENOMEM;
		goto error;
	}

	rc = utils->read_u32_array(utils->data, cmd_set_update_map[type],
						arr_32, length);
	if (rc) {
		DISP_ERROR("[%s] read failed\n", cmd_set_update_map[type]);
		goto error_free_arr_32;
	}

	count = length / 3;
	size = count * sizeof(*info);
	info = kzalloc(size, GFP_KERNEL);
	if (!info) {
		rc = -ENOMEM;
		goto error_free_arr_32;
	}

	mode->priv_info->cmd_update[type] = info;
	mode->priv_info->cmd_update_count[type] = count;

	for (k = 0; k < length; k += 3) {
		info->type = type1;
		info->mipi_address= arr_32[k];
		info->index= arr_32[k + 1];
		info->length= arr_32[k + 2];
		DISP_INFO("update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			cmd_set_update_map[type], info->mipi_address,
			info->index, info->length);
		info++;
	}
error_free_arr_32:
	kfree(arr_32);
error:
	return rc;
}


int mi_dsi_panel_parse_cmd_sets_update(struct dsi_panel *panel,
		struct dsi_display_mode *mode)
{
	int rc = 0;
	int i = 0;


	if (!mode || !mode->priv_info) {
		DISP_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	DISP_INFO("WxH: %dx%d, FPS: %d\n", mode->timing.h_active,
			mode->timing.v_active, mode->timing.refresh_rate);

	for (i = 0; i < DSI_CMD_UPDATE_MAX; i++) {
		rc = mi_dsi_panel_parse_cmd_sets_update_sub(panel, mode, i);
	}

	return rc;
}

int mi_dsi_panel_update_cmd_set(struct dsi_panel *panel,
			struct dsi_display_mode *mode, enum dsi_cmd_set_type type,
			struct dsi_cmd_update_info *info, u8 *payload, u32 size)
{
	int rc = 0;
	int i = 0;
	struct dsi_cmd_desc *cmds = NULL;
	u32 count;
	u8 *tx_buf = NULL;
	size_t tx_len;

	if (!panel || !mode || !mode->priv_info)
	{
		DISP_ERROR("invalid panel or cur_mode params\n");
		return -EINVAL;
	}

	if (!info || !payload) {
		DISP_ERROR("invalid info or payload params\n");
		return -EINVAL;
	}

	if (type != info->type || size != info->length) {
		DISP_ERROR("please check type(%d, %d) or update size(%d, %d)\n",
			type, info->type, info->length, size);
		return -EINVAL;
	}

	cmds = mode->priv_info->cmd_sets[type].cmds;
	count = mode->priv_info->cmd_sets[type].count;
	if (count == 0) {
		DISP_ERROR("[%s] No commands to be sent\n", cmd_set_prop_map[type]);
		return -EINVAL;
	}

	DISP_DEBUG("WxH: %dx%d, FPS: %d\n", mode->timing.h_active,
			mode->timing.v_active, mode->timing.refresh_rate);

	DISP_DEBUG("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
	for (i = 0; i < size; i++) {
		DISP_DEBUG("[%s] payload[%d] = 0x%02X\n", panel->type, i, payload[i]);
	}

	if (cmds && count >= info->index) {
		tx_buf = (u8 *)cmds[info->index].msg.tx_buf;
		tx_len = cmds[info->index].msg.tx_len;
		if (tx_buf && tx_buf[0] == info->mipi_address && tx_len >= info->length) {
			memcpy(&tx_buf[1], payload, info->length);
			for (i = 0; i < tx_len; i++) {
				DISP_DEBUG("[%s] tx_buf[%d] = 0x%02X\n",
					panel->type, i, tx_buf[i]);
			}
		} else {
			if (tx_buf) {
				DISP_ERROR("[%s] %s mipi address(0x%02X != 0x%02X)\n",
					panel->type, cmd_set_prop_map[type],
					tx_buf[0], info->mipi_address);
			} else {
				DISP_ERROR("[%s] panel tx_buf is NULL pointer\n",
					panel->type);
			}
			rc = -EINVAL;
		}
	} else {
		DISP_ERROR("[%s] panel cmd[%s] index error\n",
			panel->type, cmd_set_prop_map[type]);
		rc = -EINVAL;
	}

	return rc;
}

int mi_dsi_panel_parse_gamma_config(struct dsi_panel *panel,
		struct dsi_display_mode *mode)
{
	int rc;
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_parser_utils *utils = &panel->utils;
	u32 gamma_26_cfg[2] = {0, 0};
	u32 gamma_c0_cfg[2] = {0, 0};
	u32 gamma_df_cfg[2] = {0, 0};

	priv_info = mode->priv_info;

	rc = utils->read_u32_array(utils->data, "mi,mdss-flat-status-control-gamma-26-cfg",
			gamma_26_cfg, 2);
	if (rc) {
		DISP_DEBUG("mi,mdss-flat-status-control-gamma-26-cfg not defined rc=%d\n", rc);
	} else {
		DISP_INFO("FPS: %d, gamma 26 cfg: 0x%02X, 0x%02X\n",
			mode->timing.refresh_rate, gamma_26_cfg[0], gamma_26_cfg[1]);
		priv_info->flat_on_26reg_gamma = gamma_26_cfg[0];
		priv_info->flat_off_26reg_gamma = gamma_26_cfg[1];
	}

	rc = utils->read_u32_array(utils->data, "mi,mdss-flat-status-control-gamma-C0-cfg",
			gamma_c0_cfg, 2);
	if (rc) {
		DISP_DEBUG("mi,mdss-flat-status-control-gamma-C0-cfg not defined rc=%d\n", rc);
	} else {
		DISP_INFO("FPS: %d, gamma c0 cfg: 0x%02X, 0x%02X\n",
			mode->timing.refresh_rate, gamma_c0_cfg[0], gamma_c0_cfg[1]);
		priv_info->flat_on_C0reg_gamma = gamma_c0_cfg[0];
		priv_info->flat_off_C0reg_gamma = gamma_c0_cfg[1];
	}

	rc = utils->read_u32_array(utils->data, "mi,mdss-flat-status-control-gamma-DF-cfg",
			gamma_df_cfg, 2);
	if (rc) {
		DISP_DEBUG("mi,mdss-flat-status-control-gamma-DF-cfg not defined rc=%d\n", rc);
	} else {
		DISP_INFO("FPS: %d, gamma df cfg: 0x%02X, 0x%02X\n",
			mode->timing.refresh_rate, gamma_df_cfg[0], gamma_df_cfg[1]);
		priv_info->flat_on_DFreg_gamma = gamma_df_cfg[0];
		priv_info->flat_off_DFreg_gamma = gamma_df_cfg[1];
	}

	return 0;
}

int mi_dsi_panel_parse_dc_fps_config(struct dsi_panel *panel,
		struct dsi_display_mode *mode)
{
	int rc;
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_parser_utils *utils = &panel->utils;
	u32 dc_6c_cfg[2] = {0, 0};

	priv_info = mode->priv_info;

	rc = utils->read_u32_array(utils->data, "mi,mdss-dc-status-control-dc-6C-cfg",
			dc_6c_cfg, 2);
	if (rc) {
		DISP_DEBUG("mi,mdss-dc-status-control-dc-6C-cfg not defined rc=%d\n", rc);
	} else {
		DISP_INFO("FPS: %d, dc 6C cfg: 0x%02X, 0x%02X\n",
			mode->timing.refresh_rate, dc_6c_cfg[0], dc_6c_cfg[1]);
		priv_info->dc_on_6Creg = dc_6c_cfg[0];
		priv_info->dc_off_6Creg = dc_6c_cfg[1];
	}

	return 0;
}


int mi_dsi_panel_parse_2F26reg_gamma_config(struct dsi_panel *panel,
		struct dsi_display_mode *mode)
{
	int rc;
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_parser_utils *utils = &panel->utils;
	u32 gamma_2f_cfg[2] = {0, 0};
	u32 gamma_26_cfg[2] = {0, 0};

	priv_info = mode->priv_info;

	rc = utils->read_u32_array(utils->data, "mi,mdss-flat-status-control-gamma-2F-cfg",
			gamma_2f_cfg, 2);
	if (rc) {
		DISP_INFO("mi,mdss-flat-status-control-gamma-2F-cfg not defined rc=%d\n", rc);
	} else {
		DISP_INFO("FPS: %d, gamma 2F cfg: 0x%02X, 0x%02X\n",
			mode->timing.refresh_rate, gamma_2f_cfg[0], gamma_2f_cfg[1]);
		priv_info->flat_on_2Freg_gamma = gamma_2f_cfg[0];
		priv_info->flat_off_2Freg_gamma = gamma_2f_cfg[1];
	}

	rc = utils->read_u32_array(utils->data, "mi,mdss-flat-status-control-gamma-26-cfg",
			gamma_26_cfg, 2);
	if (rc) {
		DISP_INFO("mi,mdss-flat-status-control-gamma-26-cfg not defined rc=%d\n", rc);
	} else {
		DISP_INFO("FPS: %d, gamma 26 cfg: 0x%02X, 0x%02X\n",
			mode->timing.refresh_rate, gamma_26_cfg[0], gamma_26_cfg[1]);
		priv_info->flat_on_26reg_gamma = gamma_26_cfg[0];
		priv_info->flat_off_26reg_gamma = gamma_26_cfg[1];
	}

	return 0;
}

int mi_dsi_panel_parse_timing_fps_params(struct dsi_panel *panel, struct dsi_display_mode *mode,
				struct dsi_parser_utils *utils)
{
	int rc = 0;

	struct dsi_display_mode_priv_info *priv_info;

	if ( !mode || !mode->priv_info)
		return -EINVAL;

	if(panel->mi_cfg.multi_timing_enable == false) {
		DISP_INFO("mi-video-mode-multi-timing: %d , return \n", panel->mi_cfg.multi_timing_enable);
		return 0;
	}
	priv_info = mode->priv_info;

	priv_info->mi_per_timing_fps_len = utils->count_u32_elems(utils->data, "mi,mdss-dsi-supported-fps-list");
	if (priv_info->mi_per_timing_fps_len <= 0 ) {
		DSI_ERR("not support dfps-list for the panel, rc = %d\n", rc);
		return 0;
	}

	priv_info->mi_per_timing_fps = kcalloc(priv_info->mi_per_timing_fps_len, sizeof(u32), GFP_KERNEL);
	if (!priv_info->mi_per_timing_fps)
		return -ENOMEM;

	rc = utils->read_u32_array(utils->data,
			"mi,mdss-dsi-supported-fps-list", priv_info->mi_per_timing_fps, priv_info->mi_per_timing_fps_len);
	if (rc) {
		DSI_ERR("unable to read mi,mdss-dsi-supported-fps-list, rc = %d\n", rc);
		return -EINVAL;
	}

	return rc;
}

int mi_dsi_panel_write_cmd_set(struct dsi_panel *panel,
				struct dsi_panel_cmd_set *cmd_sets)
{
	int rc = 0, i = 0;
	ssize_t len;
	struct dsi_cmd_desc *cmds;
	u32 count;
	enum dsi_cmd_set_state state;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cmds = cmd_sets->cmds;
	count = cmd_sets->count;
	state = cmd_sets->state;

	if (count == 0) {
		DISP_DEBUG("[%s] No commands to be sent for state\n", panel->type);
		goto error;
	}

	for (i = 0; i < count; i++) {
		cmds->ctrl_flags = 0;

		if (state == DSI_CMD_SET_STATE_LP)
			cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;

		len = dsi_host_transfer_sub(panel->host, cmds);
		if (len < 0) {
			rc = len;
			DISP_ERROR("failed to set cmds, rc=%d\n", rc);
			goto error;
		}
		if (cmds->post_wait_ms)
			usleep_range(cmds->post_wait_ms * 1000,
					((cmds->post_wait_ms * 1000) + 10));
		cmds++;
	}
error:
	return rc;
}

int mi_dsi_panel_read_batch_number(struct dsi_panel *panel)
{
	int rc = 0;
	unsigned long mode_flags_backup = 0;
	u8 rdbuf[8];
	ssize_t read_len = 0;
	u8 read_batch_number = 0;

	int i = 0;
	struct panel_batch_info info[] = {
		{0x00, "P0.0"},
		{0x01, "P0.1"},
		{0x10, "P1.0"},
		{0x11, "P1.1"},
		{0x12, "P1.2"},
		{0x13, "P1.2"},
		{0x20, "P2.0"},
		{0x21, "P2.1"},
		{0x30, "MP"},
	};

	if (!panel || !panel->host) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}
	mutex_lock(&panel->panel_lock);

	mode_flags_backup = panel->mipi_device.mode_flags;
	panel->mipi_device.mode_flags |= MIPI_DSI_MODE_LPM;
	read_len = mipi_dsi_dcs_read(&panel->mipi_device, 0xDA, rdbuf, 1);
	panel->mipi_device.mode_flags = mode_flags_backup;
	if (read_len > 0) {
		read_batch_number = rdbuf[0];
		panel->mi_cfg.panel_batch_number = read_batch_number;
		for (i = 0; i < ARRAY_SIZE(info); i++) {
			if (read_batch_number == info[i].batch_number) {
				DISP_INFO("panel batch is %s\n", info[i].batch_name);
				break;
			}
		}
		rc = 0;
 		panel->mi_cfg.panel_batch_number_read_done = true;
	} else {
		DISP_ERROR("failed to read panel batch number\n");
		panel->mi_cfg.panel_batch_number = 0;
		rc = -EAGAIN;
		panel->mi_cfg.panel_batch_number_read_done = false;
	}

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int mi_dsi_panel_write_dsi_cmd_set(struct dsi_panel *panel,
			int type)
{
	int rc = 0;
	int i = 0, j = 0;
	u8 *tx_buf = NULL;
	u8 *buffer = NULL;
	int buf_size = 1024;
	u32 cmd_count = 0;
	int buf_count = 1024;
	struct dsi_cmd_desc *cmds;
	enum dsi_cmd_set_state state;
	struct dsi_display_mode *mode;

	if (!panel || !panel->cur_mode || type < 0 || type >= DSI_CMD_SET_MAX) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	buffer = kzalloc(buf_size, GFP_KERNEL);
	if (!buffer) {
		return -ENOMEM;
	}

	mutex_lock(&panel->panel_lock);

	mode = panel->cur_mode;
	cmds = mode->priv_info->cmd_sets[type].cmds;
	cmd_count = mode->priv_info->cmd_sets[type].count;
	state = mode->priv_info->cmd_sets[type].state;

	if (cmd_count == 0) {
		DISP_ERROR("[%s] No commands to be sent\n", cmd_set_prop_map[type]);
		rc = -EAGAIN;
		goto error;
	}

	DISP_INFO("set cmds [%s], count (%d), state(%s)\n",
		cmd_set_prop_map[type], cmd_count,
		(state == DSI_CMD_SET_STATE_LP) ? "dsi_lp_mode" : "dsi_hs_mode");

	for (i = 0; i < cmd_count; i++) {
		memset(buffer, 0, buf_size);
		buf_count = snprintf(buffer, buf_size, "%02X", cmds->msg.tx_len);
		tx_buf = (u8 *)cmds->msg.tx_buf;
		for (j = 0; j < cmds->msg.tx_len ; j++) {
			buf_count += snprintf(buffer + buf_count,
					buf_size - buf_count, " %02X", tx_buf[j]);
		}
		DISP_DEBUG("[%d] %s\n", i, buffer);
		cmds++;
	}

	rc = dsi_panel_tx_cmd_set(panel, type);

error:
	mutex_unlock(&panel->panel_lock);
	kfree(buffer);
	return rc;
}

ssize_t mi_dsi_panel_show_dsi_cmd_set_type(struct dsi_panel *panel,
			char *buf, size_t size)
{
	ssize_t count = 0;
	int type = 0;

	if (!panel || !buf) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	count = snprintf(buf, size, "%s: dsi cmd_set name\n", "id");

	for (type = DSI_CMD_SET_PRE_ON; type < DSI_CMD_SET_MAX; type++) {
		count += snprintf(buf + count, size - count, "%02d: %s\n",
				     type, cmd_set_prop_map[type]);
	}

	return count;
}

int mi_dsi_panel_update_doze_cmd_locked(struct dsi_panel *panel, u8 value)
{
	int rc = 0;
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	int i = 0;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;

	cmd_update_index = DSI_CMD_SET_MI_DOZE_LBM_UPDATE;
	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for (i = 0; i < cmd_update_count; i++){
		DISP_INFO("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
		mi_dsi_panel_update_cmd_set(panel, cur_mode,
			DSI_CMD_SET_MI_DOZE_LBM, info,
			&value, sizeof(value));
		info++;
	}

	return rc;
}

int mi_dsi_panel_set_doze_brightness(struct dsi_panel *panel,
			u32 doze_brightness)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg;

	mutex_lock(&panel->panel_lock);

	if (!dsi_panel_initialized(panel)) {
		DISP_ERROR("panel is not initialized!\n");
		rc = -EINVAL;
		goto exit;
	}

	mi_cfg = &panel->mi_cfg;

	if (is_hbm_fod_on(panel)) {
		mi_cfg->last_doze_brightness = mi_cfg->doze_brightness;
		mi_cfg->doze_brightness = doze_brightness;
		DISP_INFO("Skip! [%s] set doze brightness %d due to FOD_HBM_ON\n",
			panel->type, doze_brightness);
	} else if ((panel->mi_cfg.lhbm_gxzw && panel->mi_cfg.aod_to_normal_pending == true)
		||panel->mi_cfg.aod_to_normal_statue == true) {
		mi_cfg->last_doze_brightness = mi_cfg->doze_brightness;
		mi_cfg->doze_brightness = doze_brightness;
		mi_dsi_update_backlight_in_aod(panel, false);
		panel->mi_cfg.aod_to_normal_pending = false;
		panel->mi_cfg.aod_to_normal_statue = true;
	} else if (panel->mi_cfg.panel_state == PANEL_STATE_ON
		|| mi_cfg->doze_brightness != doze_brightness) {
		if (doze_brightness == DOZE_BRIGHTNESS_HBM) {
			panel->mi_cfg.panel_state = PANEL_STATE_DOZE_HIGH;
			if (mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PB
			|| mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PC) {
				if (panel->cur_mode->timing.refresh_rate == 30) {
					if (mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PB) {
						mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_DOZE_HBM, 1023); // 03 FF
					} else {
						mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_DOZE_HBM, 4095); // 0F FF
					}
				} else {
					mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_DOZE_HBM,
											mi_cfg->doze_hbm_dbv_level);
			}
				panel->mi_cfg.last_aod_state = doze_brightness;
			}
			if(mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_PA && (panel->id_config.build_id < 0x61)) {
				mi_dsi_update_switch_cmd_N8(panel, DSI_CMD_SET_MI_DOZE_HBM_UPDATE, DSI_CMD_SET_MI_DOZE_HBM);
			}
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
			if (rc) {
				DISP_ERROR("[%s] failed to send DOZE_HBM cmd, rc=%d\n",
					panel->type, rc);
			}
		} else if (doze_brightness == DOZE_BRIGHTNESS_LBM) {
			panel->mi_cfg.panel_state = PANEL_STATE_DOZE_LOW;
			if (mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PB
				|| mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PC) {
				if (panel->cur_mode->timing.refresh_rate == 30) {
					if (mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PB) {
						mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_DOZE_LBM, 511); // 01 FF
					} else {
						mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_DOZE_LBM, 341); // 01 55
					}
				} else {
					mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_DOZE_LBM,
											mi_cfg->doze_lbm_dbv_level);
				}
				panel->mi_cfg.last_aod_state = doze_brightness;
			}
			if(mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_PA && (panel->id_config.build_id < 0x61)) {
				mi_dsi_update_switch_cmd_N8(panel, DSI_CMD_SET_MI_DOZE_LBM_UPDATE, DSI_CMD_SET_MI_DOZE_LBM);
			}
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
			if (rc) {
				DISP_ERROR("[%s] failed to send DOZE_LBM cmd, rc=%d\n",
					panel->type, rc);
			}
		}
		mi_cfg->last_doze_brightness = mi_cfg->doze_brightness;
		mi_cfg->doze_brightness = doze_brightness;
		DISP_TIME_INFO("[%s] set doze brightness to %s\n",
			panel->type, get_doze_brightness_name(doze_brightness));
	} else {
		DISP_INFO("[%s] %s has been set, skip\n", panel->type,
			get_doze_brightness_name(doze_brightness));
	}

exit:
	mutex_unlock(&panel->panel_lock);

	return rc;
}

int mi_dsi_panel_get_doze_brightness(struct dsi_panel *panel,
			u32 *doze_brightness)
{
	struct mi_dsi_panel_cfg *mi_cfg;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	mi_cfg = &panel->mi_cfg;
	*doze_brightness =  mi_cfg->doze_brightness;

	mutex_unlock(&panel->panel_lock);

	return 0;
}

int mi_dsi_panel_get_brightness(struct dsi_panel *panel,
			u32 *brightness)
{
	struct mi_dsi_panel_cfg *mi_cfg;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	mi_cfg = &panel->mi_cfg;
	*brightness =  mi_cfg->last_bl_level;

	mutex_unlock(&panel->panel_lock);

	return 0;
}

int mi_dsi_panel_write_dsi_cmd(struct dsi_panel *panel,
			struct dsi_cmd_rw_ctl *ctl)
{
	struct dsi_panel_cmd_set cmd_sets = {0};
	u32 packet_count = 0;
	u32 dlen = 0;
	int rc = 0;

	mutex_lock(&panel->panel_lock);

	if (!panel || !panel->panel_initialized) {
		DISP_ERROR("Panel not initialized!\n");
		rc = -EAGAIN;
		goto exit_unlock;
	}

	if (!ctl->tx_len || !ctl->tx_ptr) {
		DISP_ERROR("[%s] invalid params\n", panel->type);
		rc = -EINVAL;
		goto exit_unlock;
	}

	rc = dsi_panel_get_cmd_pkt_count(ctl->tx_ptr, ctl->tx_len, &packet_count);
	if (rc) {
		DISP_ERROR("[%s] write dsi commands failed, rc=%d\n",
			panel->type, rc);
		goto exit_unlock;
	}

	DISP_DEBUG("[%s] packet-count=%d\n", panel->type, packet_count);

	rc = dsi_panel_alloc_cmd_packets(&cmd_sets, packet_count);
	if (rc) {
		DISP_ERROR("[%s] failed to allocate cmd packets, rc=%d\n",
			panel->type, rc);
		goto exit_unlock;
	}

	rc = dsi_panel_create_cmd_packets(ctl->tx_ptr, dlen, packet_count,
				cmd_sets.cmds);
	if (rc) {
		DISP_ERROR("[%s] failed to create cmd packets, rc=%d\n",
			panel->type, rc);
		goto exit_free1;
	}

	if (ctl->tx_state == MI_DSI_CMD_LP_STATE) {
		cmd_sets.state = DSI_CMD_SET_STATE_LP;
	} else if (ctl->tx_state == MI_DSI_CMD_HS_STATE) {
		cmd_sets.state = DSI_CMD_SET_STATE_HS;
	} else {
		DISP_ERROR("[%s] command state unrecognized-%s\n",
			panel->type, cmd_sets.state);
		goto exit_free1;
	}

	rc = mi_dsi_panel_write_cmd_set(panel, &cmd_sets);
	if (rc) {
		DISP_ERROR("[%s] failed to send cmds, rc=%d\n", panel->type, rc);
		goto exit_free2;
	}

exit_free2:
	if (ctl->tx_len && ctl->tx_ptr)
		dsi_panel_destroy_cmd_packets(&cmd_sets);
exit_free1:
	if (ctl->tx_len && ctl->tx_ptr)
		dsi_panel_dealloc_cmd_packets(&cmd_sets);
exit_unlock:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int mi_dsi_panel_set_brightness_clone(struct dsi_panel *panel,
			u32 brightness_clone)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg;
	int disp_id = MI_DISP_PRIMARY;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (brightness_clone > mi_cfg->max_brightness_clone)
		brightness_clone = mi_cfg->max_brightness_clone;

	atomic_set(&mi_cfg->brightness_clone, brightness_clone);

	disp_id = mi_get_disp_id(panel->type);
	mi_disp_feature_event_notify_by_type(disp_id,
			MI_DISP_EVENT_BRIGHTNESS_CLONE,
			sizeof(u32), atomic_read(&mi_cfg->brightness_clone));

	return rc;
}

int mi_dsi_panel_get_brightness_clone(struct dsi_panel *panel,
			u32 *brightness_clone)
{
	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	*brightness_clone = atomic_read(&panel->mi_cfg.brightness_clone);

	return 0;
}

int mi_dsi_panel_get_max_brightness_clone(struct dsi_panel *panel,
			u32 *max_brightness_clone)
{
	struct mi_dsi_panel_cfg *mi_cfg;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	*max_brightness_clone =  mi_cfg->max_brightness_clone;

	return 0;
}

void mi_dsi_update_backlight_in_aod(struct dsi_panel *panel, bool restore_backlight)
{
	int bl_lvl = 0;
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;
	struct mipi_dsi_device *dsi = &panel->mipi_device;

	if (restore_backlight) {
		bl_lvl = mi_cfg->last_bl_level;
	} else {
		switch (mi_cfg->doze_brightness) {
			case DOZE_BRIGHTNESS_HBM:
				bl_lvl = mi_cfg->doze_hbm_dbv_level;
				break;
			case DOZE_BRIGHTNESS_LBM:
				bl_lvl = mi_cfg->doze_lbm_dbv_level;
				break;
			default:
				return;
		}
	}
	DISP_INFO("[%s] mi_dsi_update_backlight_in_aod bl_lvl=%d\n",
			panel->type, bl_lvl);
	if (panel->bl_config.bl_inverted_dbv)
		bl_lvl = (((bl_lvl & 0xff) << 8) | (bl_lvl >> 8));
	mipi_dsi_dcs_set_display_brightness(dsi, bl_lvl);

	return;
}

int mi_dsi_update_51_mipi_cmd(struct dsi_panel *panel,
			enum dsi_cmd_set_type type, int bl_lvl)
{
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	u8 bl_buf[6] = {0};
	int j = 0;
	int size = 2;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;

	DISP_INFO("[%s] bl_lvl = %d\n", panel->type, bl_lvl);

	bl_buf[0] = (bl_lvl >> 8) & 0xff;
	bl_buf[1] = bl_lvl & 0xff;
	size = 2;

	switch (type) {
	case DSI_CMD_SET_NOLP:
		cmd_update_index = DSI_CMD_SET_NOLP_UPDATE;
		break;
	case DSI_CMD_SET_MI_HBM_ON:
		cmd_update_index = DSI_CMD_SET_MI_HBM_ON_UPDATE;
		break;
	case DSI_CMD_SET_MI_HBM_OFF:
		cmd_update_index = DSI_CMD_SET_MI_HBM_OFF_UPDATE;
		break;
	case DSI_CMD_SET_MI_HBM_FOD_ON:
		cmd_update_index = DSI_CMD_SET_MI_HBM_FOD_ON_UPDATE;
		break;
	case DSI_CMD_SET_MI_HBM_FOD_OFF:
		cmd_update_index = DSI_CMD_SET_MI_HBM_FOD_OFF_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_PRE:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_PRE_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_HBM:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_HBM_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL_UPDATE;
		break;
	case DSI_CMD_SET_MI_DOZE_HBM:
		cmd_update_index = DSI_CMD_SET_MI_DOZE_HBM_UPDATE;
		break;
	case DSI_CMD_SET_MI_DOZE_LBM:
		cmd_update_index = DSI_CMD_SET_MI_DOZE_LBM_UPDATE;
		break;
	case DSI_CMD_SET_MI_IP_ON:
		cmd_update_index = DSI_CMD_SET_MI_IP_ON_UPDATE;
		break;
	case DSI_CMD_SET_MI_IP_OFF:
		cmd_update_index = DSI_CMD_SET_MI_IP_OFF_UPDATE;
		break;
	case DSI_CMD_DBV_327_TO_326:
		cmd_update_index = DSI_CMD_DBV_327_TO_326_COMMAND_UPDATE;
		break;
	case DSI_CMD_DBV_326_TO_327:
		cmd_update_index = DSI_CMD_DBV_326_TO_327_COMMAND_UPDATE;
		break;
	default:
		DISP_ERROR("[%s] unsupport cmd %s\n",
				panel->type, cmd_set_prop_map[type]);
		return -EINVAL;
	}

	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for (j = 0; j < cmd_update_count; j++) {
		DISP_DEBUG("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
		if (info && info->mipi_address != 0x51) {
			DISP_DEBUG("[%s] error mipi address (0x%02X)\n", panel->type, info->mipi_address);
			info++;
			continue;
		} else {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
					type, info, bl_buf, size);
			break;
		}
	}

	return 0;
}

/* Note: Factory version need flat cmd send out immediately,
 * do not care it may lead panel flash.
 * Dev version need flat cmd send out send with te
 */
static int mi_dsi_send_flat_sync_with_te_locked(struct dsi_panel *panel,
			bool enable)
{
	int rc = 0;

#ifdef CONFIG_FACTORY_BUILD
	if (enable) {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_SEC_ON);
	} else {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_OFF);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_SEC_OFF);
	}
	DISP_INFO("send flat %s cmd immediately", enable ? "ON" : "OFF");
#else
	/*flat cmd will be send out at mi_sde_connector_flat_fence*/
	DISP_DEBUG("flat cmd should send out sync with te");
#endif
	return rc;
}

#define N3_LHBM1000NITS 6
#define N3_LHBM110NITS 0
#define N3_LHBMGREEN500NITS 12

#define N9_LHBM1000NITS 0
#define N9_LHBM110NITS 6
#define N9_LHBMGREEN500NITS 12

static inline void mi_dsi_panel_cal_lhbm_param_N3(int type,
		u8 *update_val, u8 size, int bl_lvl)
{
	int i = 0;
	u64 tmp;
	u16 w1000_dev[8] = {3590, 3590, 3610, 3625, 3635, 3645, 3650, 3660};
	u16 g500_dev[8] = {4240, 4210, 4190, 4170, 4150, 4140, 4130, 4120};
	u8 dev_index = 0;

	if (bl_lvl <= 0x1C2)
		dev_index = 0;
	else if (bl_lvl > 0x1C2 && bl_lvl <= 0x333)
		dev_index = 1;
	else if (bl_lvl > 0x333 && bl_lvl <= 0x484)
		dev_index = 2;
	else if (bl_lvl > 0x484 && bl_lvl <= 0x61A)
		dev_index = 3;
	else if (bl_lvl > 0x61A && bl_lvl <= 0x7FF)
		dev_index = 4;
	else if (bl_lvl > 0x7FF && bl_lvl <= 0x903)
		dev_index = 5;
	else if (bl_lvl > 0x903 && bl_lvl <= 0xA10)
		dev_index = 6;
	else if (bl_lvl > 0xA10 && bl_lvl <= 0xB32)
		dev_index = 7;

	for (i = 0; i < size / 2; i++) {
		tmp = (update_val[i*2] << 8) | update_val[i*2+1];

		if (type == N3_LHBMGREEN500NITS) {
			DISP_DEBUG("G500 gamma update tmp[%d] * 1000 / %d\n", tmp, g500_dev[dev_index]);
			tmp = tmp * 1000 / g500_dev[dev_index];
		} else if (type == N3_LHBM1000NITS) {
			DISP_DEBUG("W1000 gamma update tmp[%d] * 100 / %d\n", tmp, w1000_dev[dev_index]);
			tmp = tmp * 1000 / w1000_dev[dev_index];
		}

		update_val[i*2] = (tmp >> 8) & 0xFF;
		update_val[i*2 + 1] = tmp & 0xFF;
		DISP_DEBUG("gamma update tmp %d [%d] %d 0x%x  %x\n", i, bl_lvl, dev_index, update_val[i*2], update_val[i*2+ 1]);
	}
}

static int mi_dsi_panel_update_lhbm_param_N3(struct dsi_panel * panel,
		enum dsi_cmd_set_type type, int bl_lvl, bool lhbm_is_0size)
{
	int rc=0;
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	u8 *update_rgb_reg_ptr = NULL;
	u8 update_rgb_reg[24] = {0};
	u8 alpha_buf[4] = {0};
	int i = 0;
	int lhbm_data_index = 0;
	u8 length = 24;
	u8 size_set = 0;
	bool is_cal_lhbm_param = false;

	if (!panel->mi_cfg.read_lhbm_gamma_success ||
		mi_get_panel_id_by_dsi_panel(panel) != N3_PANEL_PA ) {
		DISP_DEBUG("don't need update white rgb config\n");
		return 0;
	}

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;
	mi_cfg = &panel->mi_cfg;

	if(bl_lvl <= panel->mi_cfg.lhbm_lbl_mode_threshold
		&& panel->id_config.build_id <= N3_PANEL_PA_P11) {
		alpha_buf[0] = (aa_alpha_N3_PANEL_PA_P11[bl_lvl] >> 8) & 0xFF;
		alpha_buf[1] = aa_alpha_N3_PANEL_PA_P11[bl_lvl] & 0xFF;
	} else if(bl_lvl <= panel->mi_cfg.lhbm_lbl_mode_threshold
		&& panel->id_config.build_id > N3_PANEL_PA_P11) {
		alpha_buf[0] = (aa_alpha_N3_PANEL_PA_P12[bl_lvl] >> 8) & 0xFF;
		alpha_buf[1] = aa_alpha_N3_PANEL_PA_P12[bl_lvl] & 0xFF;
	}

	if(bl_lvl <= mi_cfg->lhbm_lbl_mode_threshold) {

		alpha_buf[2] = 0x01;
		alpha_buf[3] = 0xC2;
	} else {
		alpha_buf[0] = 0x10;
		alpha_buf[1] = 0x00;
		alpha_buf[2] = (bl_lvl >> 8) & 0xff;
		alpha_buf[3] = bl_lvl & 0xff;
	}
	DISP_DEBUG("[%s] bl_lvl = %d, alpha = 0x%x%x type=%d\n",
			panel->type, bl_lvl, alpha_buf[0], alpha_buf[1], type);
	if(lhbm_is_0size)
		size_set = 0x01;
	else
		size_set = 0x03;

	switch (type) {
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
		lhbm_data_index = N3_LHBM1000NITS;
		is_cal_lhbm_param = true;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
		lhbm_data_index = N3_LHBM1000NITS;
		is_cal_lhbm_param = true;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT_UPDATE;
		lhbm_data_index = N3_LHBM110NITS;
		is_cal_lhbm_param = false;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
		lhbm_data_index = N3_LHBM110NITS;
		is_cal_lhbm_param = false;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_UPDATE;
		lhbm_data_index = N3_LHBMGREEN500NITS;
		is_cal_lhbm_param = true;
		break;
	default:
		DISP_ERROR("[%s] unsupport cmd %s\n", panel->type, cmd_set_prop_map[type]);
		return -EINVAL;
	}

	for ( i= 0; i< length; i++) {
		update_rgb_reg[i] = mi_cfg->lhbm_rgb_param[i % 6 + lhbm_data_index];
		DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, i, update_rgb_reg[i]);
	}
	update_rgb_reg_ptr = update_rgb_reg;
	if (is_cal_lhbm_param)
		mi_dsi_panel_cal_lhbm_param_N3(lhbm_data_index, update_rgb_reg_ptr, length, bl_lvl);

	DISP_DEBUG("[%s] lhbm_data_index = %d build_id=0x%02X\n", panel->type, lhbm_data_index, panel->id_config.build_id);

	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for ( i= 0; i< cmd_update_count; i++) {
		DISP_DEBUG("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
		if (info && info->mipi_address == 0x63 && info->length == sizeof(alpha_buf)) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				alpha_buf, sizeof(alpha_buf));
		} else if (info && info->mipi_address == 0xC5 && info->length == length) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == 0x62) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, &size_set, sizeof(size_set));
		}
		info++;
	}

	return rc;
}
static int mi_dsi_panel_update_lhbm_param_N16T(struct dsi_panel *panel,
			enum dsi_cmd_set_type type, int bl_lvl)
{
	struct dsi_display_mode_priv_info *priv_info = NULL;
	struct dsi_cmd_update_info *info = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	int j = 0;
	u8 d1_reg_buf[6] = {0};
	u8 a9_reg_buf[14] = {0x02,0x00,0xB5,0x2C,0x2C,0x00,0x01,0x00,0x87,0x00,0x02,0x25,0x20,0x67};
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;
	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE] == FEATURE_ON)
	{
		if (bl_lvl <= 2047)
		{
			a9_reg_buf [12] = (aa_alpha_n16t_pa_gir_on_set[bl_lvl ] >> 8) & 0xFF;
			a9_reg_buf [13] = aa_alpha_n16t_pa_gir_on_set[bl_lvl ] & 0xFF;
		} else {
			a9_reg_buf [12] = (aa_alpha_n16t_pa_gir_on_set[2048] >> 8) & 0xFF;
			a9_reg_buf [13] = aa_alpha_n16t_pa_gir_on_set[2048] & 0xFF;
		}
	} else {
		if (bl_lvl <= 2047)
		{
			a9_reg_buf [12] = (aa_alpha_n16t_pa_gir_off_set[bl_lvl ] >> 8) & 0xFF;
			a9_reg_buf [13] = aa_alpha_n16t_pa_gir_off_set[bl_lvl ] & 0xFF;
		} else {
			a9_reg_buf [12] = (aa_alpha_n16t_pa_gir_off_set[2048] >> 8) & 0xFF;
			a9_reg_buf [13] = aa_alpha_n16t_pa_gir_off_set[2048] & 0xFF;
		}
	}
	DISP_INFO("[%s] bl_lvl = %d, a9 reg alpha= 0x%02X 0x%02X \n",
			panel->type, bl_lvl, a9_reg_buf[12], a9_reg_buf[13]);
	switch (type) {
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
		memcpy(&d1_reg_buf[0], &panel->mi_cfg.lhbm_rgb_param[0],sizeof(u8)*6);
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
		memcpy(&d1_reg_buf[0], &panel->mi_cfg.lhbm_rgb_param[0],sizeof(u8)*6);
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_UPDATE;
		memcpy(&d1_reg_buf[0], &panel->mi_cfg.lhbm_rgb_param[6],sizeof(u8)*6);
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT_UPDATE;
		memcpy(&d1_reg_buf[0], &panel->mi_cfg.lhbm_rgb_param[12],sizeof(u8)*6);
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
		memcpy(&d1_reg_buf[0], &panel->mi_cfg.lhbm_rgb_param[12],sizeof(u8)*6);
		break;
	default :
		DISP_ERROR("[%s] unsupport cmd %s\n",
				panel->type, cmd_set_prop_map[type]);
		return -EINVAL;
	}

	priv_info = panel->cur_mode->priv_info;
	info = priv_info->cmd_update[cmd_update_index];
	cmd_update_count = priv_info->cmd_update_count[cmd_update_index];
	for (j = 0; j < cmd_update_count; j++) {
		if (info) {
			DISP_INFO("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
				panel->type, cmd_set_prop_map[info->type],
				info->mipi_address, info->index, info->length);
			if(info->mipi_address == 0xD1) {
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, d1_reg_buf, sizeof(d1_reg_buf));
			} else if (info->mipi_address == 0xA9) {
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, a9_reg_buf, sizeof(a9_reg_buf));
			}
			info++;
		}
	}
	return 0;
}

static int mi_dsi_panel_update_lhbm_param_N16T_PB(struct dsi_panel *panel,
			enum dsi_cmd_set_type type, int bl_lvl)
{
	struct dsi_display_mode_priv_info *priv_info = NULL;
	struct dsi_cmd_update_info *info = NULL;
	u32 cmd_update_index = 0;
	u8 lhbm_cmd_reg[4] = {0x11, 0xA0, 0x00, 0x00};

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info || bl_lvl < 0) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (bl_lvl <= 2047) {
		lhbm_cmd_reg[1] = lhbm_cmd_reg[1] + (aa_alpha_N16T_PANEL_PB[bl_lvl] >> 8) & 0xFF;
		lhbm_cmd_reg[2] = aa_alpha_N16T_PANEL_PB[bl_lvl] & 0xFF;
	} else {
		lhbm_cmd_reg[1] = 0xAF;
		lhbm_cmd_reg[2] = 0xFF;
	}

	switch (type) {
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
		lhbm_cmd_reg[3] = 0x00;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT_UPDATE;
		lhbm_cmd_reg[3] = 0x01;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_UPDATE;
		lhbm_cmd_reg[3] = 0x02;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
		lhbm_cmd_reg[3] = 0x00;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
		lhbm_cmd_reg[3] = 0x01;
		break;
	default:
		DISP_ERROR("unsupport cmd: %s\n", cmd_set_prop_map[type]);
		return -EINVAL;
	}
	DISP_INFO("bl_lvl = %d, update 97 reg lhbm_cmd_reg[97] = 0x11, 0x%2X, 0x%2X, 0x%2X.\n",
			bl_lvl, lhbm_cmd_reg[1], lhbm_cmd_reg[2], lhbm_cmd_reg[3]);

	priv_info = panel->cur_mode->priv_info;
	info = priv_info->cmd_update[cmd_update_index];
	if (info && info->mipi_address == 0x97) {
		mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
				type, info, lhbm_cmd_reg, sizeof(lhbm_cmd_reg));
	} else {
		DISP_ERROR("mipi address error: 0x%2X\n", info->mipi_address);
		return -EINVAL;
	}

	return 0;
}

static int mi_dsi_panel_update_lhbm_param_N16T_PC(struct dsi_panel *panel,
			enum dsi_cmd_set_type type, int bl_lvl)
{
	struct dsi_display_mode_priv_info *priv_info = NULL;
	struct dsi_cmd_update_info *info = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	int j = 0;
	u8 lhbm_cmd_reg[9] = {0x00,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00};
	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (type == DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL)
	{
		if (bl_lvl <= 2047) {
			lhbm_cmd_reg[7] = (aa_alpha_n16t_pc_lhbm_exit_set[bl_lvl] >> 8) & 0xFF;
			lhbm_cmd_reg[8] = aa_alpha_n16t_pc_lhbm_exit_set[bl_lvl] & 0xFF;
		} else {
			lhbm_cmd_reg[7] = (aa_alpha_n16t_pc_lhbm_exit_set[2048] >> 8) & 0xFF;
			lhbm_cmd_reg[8] = aa_alpha_n16t_pc_lhbm_exit_set[2048] & 0xFF;
		}
	} else {
		if (bl_lvl <= 2047) {
			lhbm_cmd_reg[7] = (aa_alpha_n16t_pc_lhbm_enter_set[bl_lvl ] >> 8) & 0xFF;
			lhbm_cmd_reg[8] = aa_alpha_n16t_pc_lhbm_enter_set[bl_lvl ] & 0xFF;
		} else {
			lhbm_cmd_reg[7] = (aa_alpha_n16t_pc_lhbm_enter_set[2048] >> 8) & 0xFF;
			lhbm_cmd_reg[8] = aa_alpha_n16t_pc_lhbm_enter_set[2048] & 0xFF;
		}
	}

	switch (type) {
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_HBM:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_HBM_UPDATE;
		break;
	default:
		DISP_ERROR("unsupport cmd: %s\n", cmd_set_prop_map[type]);
		return -EINVAL;
	}

	DISP_INFO("bl_lvl = %d, lhbm_cmd_reg[7] = 0x%2X, lhbm_cmd_reg[8] = 0x%2X\n",
			bl_lvl, lhbm_cmd_reg[7], lhbm_cmd_reg[8]);

	priv_info = panel->cur_mode->priv_info;
	info = priv_info->cmd_update[cmd_update_index];
	cmd_update_count = priv_info->cmd_update_count[cmd_update_index];
	for (j = 0; j < cmd_update_count; j++) {
		if (info) {
			DISP_INFO("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
				panel->type, cmd_set_prop_map[info->type],
				info->mipi_address, info->index, info->length);
			if(info->mipi_address == 0x86) {
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, lhbm_cmd_reg, sizeof(lhbm_cmd_reg));
			}
			info++;
		}
	}
    return 0;
}

static int mi_dsi_panel_update_lhbm_param_O16U(struct dsi_panel *panel,
			enum dsi_cmd_set_type type, int bl_lvl)
{
	struct dsi_display_mode_priv_info *priv_info = NULL;
	struct dsi_cmd_update_info *info = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	int j = 0;
	u8 d1_reg_buf[6] = {0};
	u8 a9_reg_buf[14] = {0x02,0x00,0xB5,0x2C,0x2C,0x00,0x01,0x00,0x87,0x00,0x02,0x25,0x20,0x67};
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;
	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE] == FEATURE_ON)
	{
		if (bl_lvl <= 2047)
		{
			a9_reg_buf [12] = (aa_alpha_o16u_pa_gir_on_set[bl_lvl ] >> 8) & 0xFF;
			a9_reg_buf [13] = aa_alpha_o16u_pa_gir_on_set[bl_lvl ] & 0xFF;
		} else {
			a9_reg_buf [12] = (aa_alpha_o16u_pa_gir_on_set[2048] >> 8) & 0xFF;
			a9_reg_buf [13] = aa_alpha_o16u_pa_gir_on_set[2048] & 0xFF;
		}
	} else {
		if (bl_lvl <= 2047)
		{
			a9_reg_buf [12] = (aa_alpha_o16u_pa_gir_off_set[bl_lvl ] >> 8) & 0xFF;
			a9_reg_buf [13] = aa_alpha_o16u_pa_gir_off_set[bl_lvl ] & 0xFF;
		} else {
			a9_reg_buf [12] = (aa_alpha_o16u_pa_gir_off_set[2048] >> 8) & 0xFF;
			a9_reg_buf [13] = aa_alpha_o16u_pa_gir_off_set[2048] & 0xFF;
		}
	}
	DISP_INFO("[%s] bl_lvl = %d, a9 reg alpha= 0x%02X 0x%02X \n",
			panel->type, bl_lvl, a9_reg_buf[12], a9_reg_buf[13]);
	switch (type) {
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
		memcpy(&d1_reg_buf[0], &panel->mi_cfg.lhbm_rgb_param[0],sizeof(u8)*6);
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
		memcpy(&d1_reg_buf[0], &panel->mi_cfg.lhbm_rgb_param[0],sizeof(u8)*6);
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_UPDATE;
		memcpy(&d1_reg_buf[0], &panel->mi_cfg.lhbm_rgb_param[6],sizeof(u8)*6);
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT_UPDATE;
		memcpy(&d1_reg_buf[0], &panel->mi_cfg.lhbm_rgb_param[12],sizeof(u8)*6);
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
		memcpy(&d1_reg_buf[0], &panel->mi_cfg.lhbm_rgb_param[12],sizeof(u8)*6);
		break;
	default :
		DISP_ERROR("[%s] unsupport cmd %s\n",
				panel->type, cmd_set_prop_map[type]);
		return -EINVAL;
	}

	priv_info = panel->cur_mode->priv_info;
	info = priv_info->cmd_update[cmd_update_index];
	cmd_update_count = priv_info->cmd_update_count[cmd_update_index];
	for (j = 0; j < cmd_update_count; j++) {
		if (info) {
			DISP_INFO("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
				panel->type, cmd_set_prop_map[info->type],
				info->mipi_address, info->index, info->length);
			if(info->mipi_address == 0xD1) {
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, d1_reg_buf, sizeof(d1_reg_buf));
			} else if (info->mipi_address == 0xA9) {
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, a9_reg_buf, sizeof(a9_reg_buf));
			}
			info++;
		}
	}
	return 0;
}

static int mi_dsi_panel_update_lhbm_param_O16U_PB(struct dsi_panel *panel,
			enum dsi_cmd_set_type type, int bl_lvl)
{
	struct dsi_display_mode_priv_info *priv_info = NULL;
	struct dsi_cmd_update_info *info = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	int j = 0;
	u32 rgb_gamma_num = 0;
	u8 reg_63_buf[4] = {0};
	u8 reg_C5_buf[18] = {0};
	u8 reg_51_buf[2] = {0};
	enum {
		lhbm_white_normal = 1,
		lhbm_green_normal = 2,
		lhbm_white_low = 3,
		lhbm_max = 4,
  	};

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info || bl_lvl < 0) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (bl_lvl >= 327) {
		if(bl_lvl > 2047){
			DISP_INFO("[%s] backlight %d in local hbm can't exceed 2047\n", panel->type, bl_lvl);
			bl_lvl = 2047;
		}
		reg_63_buf[0] = 0x10;
		reg_63_buf[1] = 0x00;
		reg_63_buf[2] = (bl_lvl >> 8) & 0xFF;
		reg_63_buf[3] = bl_lvl & 0xFF;
	} else {
		if(bl_lvl == 0){
			DISP_ERROR("backlight is zero, return\n");
			return -EINVAL;
		}
		reg_63_buf[0] = (lhbm_alpha_gain_O16U_PB[bl_lvl - 1] >> 8) & 0xFF;;
		reg_63_buf[1] = lhbm_alpha_gain_O16U_PB[bl_lvl - 1] & 0xFF;
		reg_63_buf[2] = 0x01;
		reg_63_buf[3] = 0x47;
	}
	reg_51_buf[0] = (bl_lvl >> 8) & 0xFF;
	reg_51_buf[1] = bl_lvl & 0xFF;

	switch (type) {
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
		rgb_gamma_num = lhbm_white_normal;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT_UPDATE;
		rgb_gamma_num = lhbm_white_low;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_UPDATE;
		rgb_gamma_num = lhbm_green_normal;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
		rgb_gamma_num = lhbm_white_normal;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
		rgb_gamma_num = lhbm_white_low;
		break;
	default:
		DISP_ERROR("unsupport cmd: %s\n", cmd_set_prop_map[type]);
		return -EINVAL;
	}
	DISP_INFO("[%s] bl_lvl = %d, 63 reg = 0x%02X 0x%02X 0x%02X 0x%02X\n",
			panel->type, bl_lvl, reg_63_buf[0], reg_63_buf[1], reg_63_buf[2], reg_63_buf[3]);

	memcpy(&reg_C5_buf[0], &panel->mi_cfg.lhbm_rgb_param[6 * (rgb_gamma_num - 1)], sizeof(u8) * 6);
	memcpy(&reg_C5_buf[6], &panel->mi_cfg.lhbm_rgb_param[6 * (rgb_gamma_num - 1)], sizeof(u8) * 6);
	memcpy(&reg_C5_buf[12], &panel->mi_cfg.lhbm_rgb_param[6 * (rgb_gamma_num - 1)], sizeof(u8) * 6);

	priv_info = panel->cur_mode->priv_info;
	info = priv_info->cmd_update[cmd_update_index];
	cmd_update_count = priv_info->cmd_update_count[cmd_update_index];
	for (j = 0; j < cmd_update_count; j++) {
		if (info) {
			DISP_INFO("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
				panel->type, cmd_set_prop_map[info->type],
				info->mipi_address, info->index, info->length);
			if(info->mipi_address == 0x63) {
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, reg_63_buf, sizeof(reg_63_buf));
			} else if (info->mipi_address == 0xC5) {
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, reg_C5_buf, sizeof(reg_C5_buf));
			} else if (info->mipi_address == 0x51) {
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, reg_51_buf, sizeof(reg_51_buf));
			}
			info++;
		}
	}

	return 0;
}

int mi_dsi_panel_update_gamma_param(struct dsi_panel * panel, u32 cmd_update_index,
		enum dsi_cmd_set_type type)
{
	int rc=0;
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;
	u32 cmd_update_count = 0;
	u8 *update_rgb_reg_ptr = NULL;
	u8 *update_rgb_reg_ptr2 = NULL;
	u8 update_rgb_reg[16] = {0};
	u8 update_rgb_reg2[2] = {0};
	int i = 0, j = 0;
	u8 length = 16;

	if (!panel->mi_cfg.read_gamma_success ||
		mi_get_panel_id_by_dsi_panel(panel) != N3_PANEL_PA ) {
		DISP_DEBUG("don't need update gamma config\n");
		return 0;
	}

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;
	mi_cfg = &panel->mi_cfg;
	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for ( i= 0; i< cmd_update_count; i++) {
		DISP_DEBUG("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
		if (info && info->mipi_address == mi_cfg->gamma_rgb_param[0] && info->length == length) {
			for (j = 0;j < length;j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 1];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[17] && info->length == length) {
			for (j = 0;j < length;j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 18];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[34] && info->length == length) {
			for (j = 0;j < length;j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 35];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[51] && info->length == 2) {
			for (j = 0; j < 2; j++) {
				update_rgb_reg2[j] = mi_cfg->gamma_rgb_param[j + 52];
				DISP_DEBUG("[%s] update_rgb_reg2[%d] = %d \n", panel->type, j, update_rgb_reg2[j]);
			}
			update_rgb_reg_ptr2 = update_rgb_reg2;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg2, 2);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[54] && info->length == length) {
			for (j = 0; j < length; j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 55];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[71] && info->length == length) {
			for (j = 0; j < length; j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 72];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[88] && info->length == length) {
			for (j = 0; j < length; j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 89];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[105] && info->length == 2) {
			for (j = 0; j < 2; j++) {
				update_rgb_reg2[j] = mi_cfg->gamma_rgb_param[j + 106];
				DISP_DEBUG("[%s] update_rgb_reg2[%d] = %d \n", panel->type, j, update_rgb_reg2[j]);
			}
			update_rgb_reg_ptr2 = update_rgb_reg2;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg2, 2);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[108] && info->length == length) {
			for (j = 0; j < length; j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 109];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[125] && info->length == length) {
			for (j = 0; j < length; j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 126];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[142] && info->length == length) {
			for (j = 0; j < length; j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 143];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[159] && info->length == 2) {
			for (j = 0; j < 2; j++) {
				update_rgb_reg2[j] = mi_cfg->gamma_rgb_param[j + 160];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg2[j]);
			}
			update_rgb_reg_ptr2 = update_rgb_reg2;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg2, 2);
		}
		info++;
	}

	return rc;
}

#define LHBM1000NITS 0
#define LHBM750NITS 6
#define LHBM500NITS 12
#define LHBM110NITS 18
#define LHBMGREEN500NITS 24
#define LHBMGIRONNITSOFFSET 26

static inline void mi_dsi_panel_cal_lhbm_param_N1(struct dsi_panel * panel, enum dsi_cmd_set_type type,
		u8 *update_val, u8 size , int bl_lvl, int type_index)
{
	int i = 0;
	int bl_index = 0;
	u16 tmp;

	for(i = 0; i < ARRAY_SIZE(lhbm_coefficient_backlight_range)-1; i++) {
		if (lhbm_coefficient_backlight_range[i] < bl_lvl &&
			bl_lvl <= lhbm_coefficient_backlight_range[i+1]) {
			bl_index = i;
			break;
		}
	}

	if (type_index < 0 || type_index > 4 || bl_index < 0 || bl_index > 22) {
		DISP_ERROR("invalid lhbm coefficient_index\n");
		return ;
	}

	for (i = 0; i < size / 2; i++) {
		tmp = (update_val[i*2] << 8) | update_val[i*2+1];
		if (panel->id_config.build_id == N1_PANEL_PA_P01_01) {
			DISP_DEBUG("gamma update:tmp[%d] * lhbm_coefficient_N1_PANEL_PA_P01_01[%d][%d] / 420\n",tmp, type_index, bl_index);
			tmp = tmp * lhbm_coefficient_N1_PANEL_PA_P01_01[type_index][bl_index] / 420;
		} else if (panel->id_config.build_id == N1_PANEL_PA_P01_02 ||
					panel->id_config.build_id == N1_PANEL_PA_P1_01) {
			DISP_DEBUG("gamma update:tmp[%d] * lhbm_coefficient_N1_PANEL_PA_P01_02[%d][%d] / 420\n",tmp,type_index, bl_index);
			tmp = tmp * lhbm_coefficient_N1_PANEL_PA_P01_02[type_index][bl_index] / 420;
		} else if(panel->id_config.build_id == N1_PANEL_PA_P1_02) {
			DISP_DEBUG("gamma update:tmp[%d] * lhbm_coefficient_N1_PANEL_PA_P1_02[%d][%d] / 420\n",tmp,type_index, bl_index);
			tmp = tmp * lhbm_coefficient_N1_PANEL_PA_P1_02[type_index][bl_index] / 420;
		} else {
			DISP_DEBUG("gamma update:tmp[%d] * lhbm_coefficient_N1_PANEL_PA_P11[%d][%d] / 420\n",tmp,type_index, bl_index);
			tmp = tmp * lhbm_coefficient_N1_PANEL_PA_P11[type_index][bl_index] / 420;
		}

		update_val[i*2] = (tmp >> 8) & 0xFF;
		update_val[i*2 + 1] = tmp & 0xFF;
	}
}



static int mi_dsi_panel_update_lhbm_param_N1(struct dsi_panel * panel,
		enum dsi_cmd_set_type type, int flat_mode, int bl_lvl)
{
	int rc = 0;
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	int i= 0;
	u8 DF_gir_reg_val[2] = {0x22, 0x20};
	u8 DF_bl_reg_val[2] = {0x05, 0x1C};
	u8 alpha_buf[2] = {0};
	u8 *update_D0_reg_ptr = NULL;
	u8 update_D0_reg[6] = {0};
	u32 tmp_bl;
	u8 length = 6;
	u8 lhbm_data_index = 0;
	bool is_cal_lhbm_param = false;
	int type_index = 0;

	if (mi_get_panel_id_by_dsi_panel(panel) != N1_PANEL_PA) {
		DISP_DEBUG("don't need update lhbm command\n");
		return 0;
	}

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;
	mi_cfg = &panel->mi_cfg;

	if(bl_lvl < 327) {
		if (panel->id_config.build_id == N1_PANEL_PA_P01_01) {
			alpha_buf[0] = (aa_alpha_N1_PANEL_PA_P01_01[bl_lvl] >> 8) & 0xFF;
			alpha_buf[1] = aa_alpha_N1_PANEL_PA_P01_01[bl_lvl] & 0xFF;
		} else if (panel->id_config.build_id == N1_PANEL_PA_P01_02 ||
					panel->id_config.build_id == N1_PANEL_PA_P1_01) {
			alpha_buf[0] = (aa_alpha_N1_PANEL_PA_P01_02[bl_lvl] >> 8) & 0xFF;
			alpha_buf[1] = aa_alpha_N1_PANEL_PA_P01_02[bl_lvl] & 0xFF;
		} else if (panel->id_config.build_id == N1_PANEL_PA_P1_02) {
			alpha_buf[0] = (aa_alpha_N1_PANEL_PA_P1_02[bl_lvl] >> 8) & 0xFF;
			alpha_buf[1] = aa_alpha_N1_PANEL_PA_P1_02[bl_lvl] & 0xFF;
		} else {
			alpha_buf[0] = (aa_alpha_N1_PANEL_PA_P11[bl_lvl] >> 8) & 0xFF;
			alpha_buf[1] = aa_alpha_N1_PANEL_PA_P11[bl_lvl] & 0xFF;
		}
	} else {
		alpha_buf[0] = 0x0F;
		alpha_buf[1] = 0xFF;
		tmp_bl = bl_lvl * 4;
		DF_bl_reg_val[0] = (tmp_bl>> 8) & 0xff;
		DF_bl_reg_val[1] = tmp_bl & 0xff;
	}
	DISP_DEBUG("[%s] bl_lvl = %d, alpha = 0x%x%x\n",
		panel->type, bl_lvl, alpha_buf[0], alpha_buf[1]);

	switch (type) {
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
		lhbm_data_index = LHBM1000NITS;
		is_cal_lhbm_param = true;
		type_index = 0;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_750NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_750NIT_UPDATE;
		lhbm_data_index = LHBM750NITS;
		is_cal_lhbm_param = true;
		type_index = 1;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_500NIT_UPDATE;
		lhbm_data_index = LHBM500NITS;
		is_cal_lhbm_param = true;
		type_index = 2;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT_UPDATE;
		lhbm_data_index = LHBM110NITS;
		type_index = 3;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
		lhbm_data_index = LHBM1000NITS;
		is_cal_lhbm_param = true;
		type_index = 0;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
		lhbm_data_index = LHBM110NITS;
		type_index = 3;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_UPDATE;
		lhbm_data_index = LHBMGREEN500NITS;
		is_cal_lhbm_param = true;
		type_index = 4;
		break;
	default:
		DISP_ERROR("[%s] unsupport cmd %s\n", panel->type, cmd_set_prop_map[type]);
		return -EINVAL;
	}

	if(mi_cfg->read_lhbm_gamma_success) {
		if(lhbm_data_index == LHBMGREEN500NITS) {
			length = 2;
			update_D0_reg_ptr = &update_D0_reg[2];
		} else {
			length = 6;
			update_D0_reg_ptr = update_D0_reg;
		}

		if (flat_mode)
			memcpy(update_D0_reg_ptr, &mi_cfg->lhbm_param[lhbm_data_index+LHBMGIRONNITSOFFSET], length);
		else
			memcpy(update_D0_reg_ptr, &mi_cfg->lhbm_param[lhbm_data_index], length);

		if (is_cal_lhbm_param) {
			mi_dsi_panel_cal_lhbm_param_N1(panel, type, update_D0_reg_ptr, length, bl_lvl, type_index);
		}
		DISP_DEBUG("update lbhm data index %d, calculate data %d, length %d",
				lhbm_data_index, is_cal_lhbm_param, length);
	}

	cur_mode = panel->cur_mode;
	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for (i = 0; i < cmd_update_count; i++) {
		if (info && info->mipi_address == 0xDF && info->length == 1) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				&DF_gir_reg_val[flat_mode], 1);
		} else if (info && info->mipi_address == 0xDF && info->length == 2) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				DF_bl_reg_val, sizeof(DF_bl_reg_val));
		}  else if (info && info->mipi_address == 0xD0 && info->length == length &&
			mi_cfg->read_lhbm_gamma_success){
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				update_D0_reg, sizeof(update_D0_reg));
		} else if (info && info->mipi_address == 0x87) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				alpha_buf, sizeof(alpha_buf));
		}
		info++;
	}

	return rc;
}


static inline void mi_dsi_panel_cal_lhbm_param(struct dsi_panel * panel, enum dsi_cmd_set_type type,
		u8 *update_val, u8 size , int bl_lvl, int type_index)
{
	int i = 0;
	int bl_index = 0;
	int coefficient_index = 0;
	u16 tmp;

	if (panel->id_config.build_id < N2_PANEL_PA_P11_02) {
		/*
	 * gamma update, R,G,B except 110nit & 550nit(Green:R/B)
	 * 1000nit/750nit/500nit 5.3/4.2
	 * 500nit(Green:G) 5/4.2
	*/
		for (i = 0; i < size / 2; i++) {
			tmp = (update_val[i*2] << 8) | update_val[i*2+1];

			if (type == DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT) {
				DISP_DEBUG("gamma update：tmp[%d] * 500 / 420\n",tmp);
				tmp = tmp * 500 / 420;
			} else {
				DISP_DEBUG("gamma update：tmp[%d] * 530 / 420\n",tmp);
				tmp = tmp * 530 / 420;
			}

			update_val[i*2] = (tmp >> 8) & 0xFF;
			update_val[i*2 + 1] = tmp & 0xFF;
		}
	} else {
		if (bl_lvl <= 327) bl_index = 0;
		else if (327 < bl_lvl && bl_lvl <= 573) bl_index = 1;
		else if (573 < bl_lvl && bl_lvl <= 819) bl_index = 2;
		else if (819 < bl_lvl && bl_lvl <= 1024) bl_index = 3;
		else if (1024 < bl_lvl && bl_lvl <= 1228) bl_index = 4;
		else if (1228 < bl_lvl && bl_lvl <= 1433) bl_index = 5;
		else if (1433 < bl_lvl && bl_lvl <= 1638) bl_index = 6;
		else if (1638 < bl_lvl && bl_lvl <= 1842) bl_index = 7;
		else if (1842 < bl_lvl && bl_lvl <= 2047) bl_index = 8;
		else if (2047 < bl_lvl && bl_lvl <= 2252) bl_index = 9;
		else if (2252 < bl_lvl && bl_lvl <= 2457) bl_index = 10;
		else if (2457 < bl_lvl && bl_lvl <= 2661) bl_index = 11;
		else if (2661 < bl_lvl && bl_lvl <= 2866) bl_index = 12;
		else if (2866 < bl_lvl && bl_lvl <= 2923) bl_index = 13;
		else if (2923 < bl_lvl && bl_lvl <= 2981) bl_index = 14;
		else if (2981 < bl_lvl && bl_lvl <= 3096) bl_index = 15;
		else if (3096 < bl_lvl && bl_lvl <= 3211) bl_index = 16;
		else if (3211 < bl_lvl && bl_lvl <= 3326) bl_index = 17;
		else if (3326 < bl_lvl && bl_lvl <= 3440) bl_index = 18;
		else if (3440 < bl_lvl && bl_lvl <= 3555) bl_index = 19;
		else if (3555 < bl_lvl && bl_lvl <= 3670) bl_index = 20;
		else if (3670 < bl_lvl && bl_lvl <= 3785) bl_index = 21;
		else if (3785 < bl_lvl && bl_lvl <= 3900) bl_index = 22;

		coefficient_index = type_index*23 + bl_index;
		DISP_DEBUG("type_index = %d,bl_index = %d,coefficient_index = %d\n",type_index,bl_index,coefficient_index);

		if (coefficient_index < 0 || coefficient_index > 114) {
			DISP_ERROR("invalid lhbm coefficient_index\n");
			return ;
		}
		for (i = 0; i < size / 2; i++) {
			tmp = (update_val[i*2] << 8) | update_val[i*2+1];
			if (panel->id_config.build_id == N2_PANEL_PA_P11_02) {
				DISP_DEBUG("gamma update：tmp[%d] * lhbm_coefficient_N2_PANEL_PA_P11_02[%d] / 420\n",tmp,coefficient_index);
				tmp = tmp * lhbm_coefficient_N2_PANEL_PA_P11_02[coefficient_index] / 420;
			} else if (panel->id_config.build_id >= N2_PANEL_PA_P2_01) {
				DISP_DEBUG("gamma update：tmp[%d] * lhbm_coefficient_N2_PANEL_PA_P2[%d] / 420\n",tmp,coefficient_index);
				tmp = tmp * lhbm_coefficient_N2_PANEL_PA_P2[coefficient_index] / 420;
			}

			update_val[i*2] = (tmp >> 8) & 0xFF;
			update_val[i*2 + 1] = tmp & 0xFF;
		}
	}
}

static int mi_dsi_panel_update_lhbm_param_N2(struct dsi_panel * panel,
		enum dsi_cmd_set_type type, int flat_mode, int bl_lvl)
{
	int rc = 0;
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	int i= 0;
	u8 DF_gir_reg_val[2] = {0x22, 0x20};
	u8 DF_bl_reg_val[2] = {0x05, 0x1C};
	u8 alpha_buf[2] = {0};
	u8 *update_D0_reg_ptr = NULL;
	u8 update_D0_reg[6] = {0};
	u32 tmp_bl;
	u8 length = 6;
	u8 lhbm_data_index = 0;
	bool is_cal_lhbm_param = false;
	int type_index = 0;

	if (mi_get_panel_id_by_dsi_panel(panel) != N2_PANEL_PA) {
		DISP_DEBUG("don't need update lhbm command\n");
		return 0;
	}

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;
	mi_cfg = &panel->mi_cfg;

	if(bl_lvl < 327) {
		if (panel->id_config.build_id >= N2_PANEL_PA_P11_02) {
			alpha_buf[0] = (aa_alpha_N2_PANEL_PA_P11_02[bl_lvl] >> 8) & 0xFF;
			alpha_buf[1] = aa_alpha_N2_PANEL_PA_P11_02[bl_lvl] & 0xFF;
		} else {
			alpha_buf[0] = (aa_alpha_N2_PANEL_PA[bl_lvl] >> 8) & 0xFF;
			alpha_buf[1] = aa_alpha_N2_PANEL_PA[bl_lvl] & 0xFF;
		}
	} else {
		alpha_buf[0] = 0x0F;
		alpha_buf[1] = 0xFF;
		tmp_bl = bl_lvl * 4;
		DF_bl_reg_val[0] = (tmp_bl>> 8) & 0xff;
		DF_bl_reg_val[1] = tmp_bl & 0xff;
	}
	DISP_DEBUG("[%s] bl_lvl = %d, alpha = 0x%x%x\n",
		panel->type, bl_lvl, alpha_buf[0], alpha_buf[1]);

	switch (type) {
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
		lhbm_data_index = LHBM1000NITS;
		is_cal_lhbm_param = true;
		type_index = 0;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_750NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_750NIT_UPDATE;
		lhbm_data_index = LHBM750NITS;
		is_cal_lhbm_param = true;
		type_index = 1;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_500NIT_UPDATE;
		lhbm_data_index = LHBM500NITS;
		is_cal_lhbm_param = true;
		type_index = 2;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT_UPDATE;
		lhbm_data_index = LHBM110NITS;
		type_index = 3;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
		lhbm_data_index = LHBM1000NITS;
		is_cal_lhbm_param = true;
		type_index = 0;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
		lhbm_data_index = LHBM110NITS;
		type_index = 3;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_UPDATE;
		lhbm_data_index = LHBMGREEN500NITS;
		is_cal_lhbm_param = true;
		type_index = 4;
		break;
	default:
		DISP_ERROR("[%s] unsupport cmd %s\n", panel->type, cmd_set_prop_map[type]);
		return -EINVAL;
	}

	if(mi_cfg->read_lhbm_gamma_success) {
		if(lhbm_data_index == LHBMGREEN500NITS) {
			length = 2;
			update_D0_reg_ptr = &update_D0_reg[2];
		} else {
			length = 6;
			update_D0_reg_ptr = update_D0_reg;
		}

		if (flat_mode)
			memcpy(update_D0_reg_ptr, &mi_cfg->lhbm_param[lhbm_data_index+LHBMGIRONNITSOFFSET], length);
		else
			memcpy(update_D0_reg_ptr, &mi_cfg->lhbm_param[lhbm_data_index], length);

		if (is_cal_lhbm_param || panel->id_config.build_id >= N2_PANEL_PA_P11_02) {
			mi_dsi_panel_cal_lhbm_param(panel,type, update_D0_reg_ptr, length, bl_lvl, type_index);
		}
		DISP_DEBUG("update lbhm data index %d, calculate data %d, length %d",
				lhbm_data_index, is_cal_lhbm_param, length);
	}

	cur_mode = panel->cur_mode;
	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for (i = 0; i < cmd_update_count; i++) {
		if (info && info->mipi_address == 0xDF && info->length == 1) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				&DF_gir_reg_val[flat_mode], 1);
		} else if (info && info->mipi_address == 0xDF && info->length == 2) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				DF_bl_reg_val, sizeof(DF_bl_reg_val));
		}  else if (info && info->mipi_address == 0xD0 && info->length == length &&
			mi_cfg->read_lhbm_gamma_success){
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				update_D0_reg, sizeof(update_D0_reg));
		} else if (info && info->mipi_address == 0x87) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				alpha_buf, sizeof(alpha_buf));
		}
		info++;
	}

	return rc;
}

int mi_dsi_panel_set_dbi_by_temp_locked(struct dsi_panel *panel,
			int temp_val)
{
	int rc = 0;
	int real_temp_val = temp_val;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}
	mi_cfg = &panel->mi_cfg;
	if (!(mi_get_panel_id_by_dsi_panel(panel) == N3_PANEL_PA &&
		(panel->id_config.build_id >= N3_PANEL_PA_P11))) {
		DISP_DEBUG("not support this dbi feature id:%d temp_val %d\n",
			panel->id_config.build_id, temp_val);
		return rc;
	}
	DISP_DEBUG("[%s] set temp_val %d \n", panel->type, temp_val);

	switch (temp_val) {
	case (-20):
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_0_20_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case (-18):
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_0_18_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case (-16):
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_0_16_MODE);
		rc |=dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case (-14):
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_0_14_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case (-12):
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_0_12_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case (-10):
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_0_10_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case (-8):
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_0_8_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case (-6):
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_0_6_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case (-4):
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_0_4_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case (-2):
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_0_2_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case 0:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_0_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case 2:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_2_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case 4:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_4_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case 6:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_6_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case 8:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_8_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case 10:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_10_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case 12:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_12_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case 14:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_14_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case 16:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_16_MODE);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case 18:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_18_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case 25:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_25_MODE);
		break;
	case 28:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_28_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_25_MODE);
		break;
	case 32:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_32_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_25_MODE);
		break;
	case 36:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_36_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_25_MODE);
		break;
	case 40:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_40_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_25_MODE);
		break;
	default:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_25_MODE);
		real_temp_val = 25;
		DISP_INFO("[%s] set temp_val default 25 \n", panel->type);
		break;
	}
	mi_cfg->real_dbi_state = real_temp_val;
	return rc;
}

int mi_dsi_panel_set_dbi_by_temp(struct dsi_panel *panel, int temp_val)
{
	int rc = 0;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = mi_dsi_panel_set_dbi_by_temp_locked(panel, temp_val);

	mutex_unlock(&panel->panel_lock);

	return rc;
}

int mi_dsi_panel_set_csc_by_temp_locked(struct dsi_panel *panel,
			int temp_val)
{
	int rc = 0;
	int real_temp_val = temp_val;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	u8 csc_reg_val[2] = {0x02, 0x1C};
	int csc_min_temp = 28, csc_max_temp = 45;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (real_temp_val < csc_min_temp)
		real_temp_val = csc_min_temp;
	else if (real_temp_val > csc_max_temp)
		real_temp_val = csc_max_temp;

	mi_cfg = &panel->mi_cfg;
	cur_mode = panel->cur_mode;
	info = cur_mode->priv_info->cmd_update[DSI_CMD_SET_CSC_UPDATE];
	if (!info) {
		DISP_ERROR("invalid cmd update info\n");
		return -EINVAL;
	}
	csc_reg_val[1] += (real_temp_val - csc_min_temp);
	rc = mi_dsi_panel_update_cmd_set(panel, cur_mode, DSI_CMD_SET_CSC,
	                            info, csc_reg_val, sizeof(csc_reg_val));
	if (rc) {
		DSI_ERR("[%s] failed to update DSI_CMD_SET_CSC cmds, rc=%d\n",
		       panel->name, rc);
	}
	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_CSC);
	if (rc) {
		DSI_ERR("[%s] failed to send DSI_CMD_SET_CSC cmds, rc=%d\n",
		       panel->name, rc);
	}
	DISP_INFO("set csc reg[1] 0x%x real_temp_val %d\n",
	                          csc_reg_val[1], real_temp_val);
	mi_cfg->real_dbi_state = real_temp_val;
	return rc;
}

int mi_dsi_panel_set_dbi_3dlut_by_temp(struct dsi_panel *panel, int temp_level)
{
    int rc = 0;
    if (!panel) {
        DISP_ERROR("invalid params\n");
        return -EINVAL;
    }
    mutex_lock(&panel->panel_lock);
    rc = mi_dsi_panel_set_dbi_3dlut_by_temp_locked(panel, temp_level);
    mutex_unlock(&panel->panel_lock);
    return rc;
}
int mi_dsi_panel_set_dbi_3dlut_by_temp_locked(struct dsi_panel *panel, int temp_level)
{
    int rc = 0;
	int real_temp_val = temp_level;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}
	mi_cfg = &panel->mi_cfg;
    DISP_DEBUG("[%s] set temp_val %d \n", panel->type, temp_level);
    switch (temp_level) {
    case TEMP_INDEX_25:
        rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
        break;
    case TEMP_INDEX_28:
        rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_28_MODE);
        break;
    case TEMP_INDEX_32:
        rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_32_MODE);
        break;
    case TEMP_INDEX_36:
        rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_36_MODE);
        break;
    default:
        rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
        break;
    }
    mi_cfg->real_dbi_state = real_temp_val;
    return rc;
}


#define N11U_LHBM1000NITS 0
#define N11U_LHBM110NITS 6
#define N11U_LHBMGREEN500NITS 12
#define N11U_LHBMGIRONNITSOFFSET 14

static inline void mi_dsi_panel_cal_lhbm_param_N11U(enum dsi_cmd_set_type type,
               u8 *update_val, u8 size, int bl_lvl, int type_index)
{
       int i = 0;
	   int bl_index = 0;
	   int coefficient_index = 0;
       u16 tmp;

	   if (bl_lvl <= 2047) bl_index = 0;
	   else if (2047 < bl_lvl && bl_lvl <= 2293) bl_index = 1;
	   else if (2293 < bl_lvl && bl_lvl <= 2440) bl_index = 2;
	   else if (2440 < bl_lvl && bl_lvl <= 2637) bl_index = 3;
	   else if (2637 < bl_lvl && bl_lvl <= 2858) bl_index = 4;
	   else if (2858 < bl_lvl && bl_lvl <= 2957) bl_index = 5;
	   else if (2957 < bl_lvl && bl_lvl <= 3055) bl_index = 6;
	   else if (3055 < bl_lvl && bl_lvl <= 3154) bl_index = 7;
	   else if (3154 < bl_lvl && bl_lvl <= 3275) bl_index = 8;
	   else if (3275 < bl_lvl && bl_lvl <= 3375) bl_index = 9;
	   else if (3375 < bl_lvl && bl_lvl <= 3476) bl_index = 10;
	   else if (3476 < bl_lvl && bl_lvl <= 3590) bl_index = 11;
	   else if (3590 < bl_lvl && bl_lvl <= 3791) bl_index = 12;
	   else if (3791 < bl_lvl && bl_lvl <= 3900) bl_index = 13;

	   coefficient_index = type_index*14 + bl_index;
	   DISP_DEBUG("type_index = %d,bl_index = %d,coefficient_index = %d\n",type_index,bl_index,coefficient_index);

	   if (coefficient_index < 0 || coefficient_index > 41) {
			DISP_ERROR("invalid lhbm coefficient_index\n");
			return ;
		}
		for (i = 0; i < size / 2; i++) {
               tmp = (update_val[i*2] << 8) | update_val[i*2+1];
			   DISP_DEBUG("gamma update：tmp[%d] * lhbm_coefficient_N11U_PANEL_PA[%d] / 10000\n",tmp,coefficient_index);
			   tmp = tmp * lhbm_coefficient_N11U_PANEL_PA[coefficient_index] / 10000;
               update_val[i*2] = (tmp >> 8) & 0xFF;
               update_val[i*2 + 1] = tmp & 0xFF;
       }
}

static int mi_dsi_panel_update_lhbm_param_N11U(struct dsi_panel * panel,
		enum dsi_cmd_set_type type, int flat_mode, int bl_lvl)
{
	int rc = 0;
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	int i= 0;
	u8 DF_bl_reg_val[2] = {0x05, 0x1C};
	u8 alpha_buf[2] = {0};
	u8 *update_D0_reg_ptr = NULL;
	u8 update_D0_reg[6] = {0};
	u32 tmp_bl;
	u8 length = 6;
	u8 lhbm_data_index = 0;
	bool is_cal_lhbm_param = false;
	int type_index = 0;

	if (mi_get_panel_id_by_dsi_panel(panel) != N11U_PANEL_PA) {
		DISP_DEBUG("don't need update lhbm command\n");
		rc = 0;
	}

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;
	mi_cfg = &panel->mi_cfg;

	if(bl_lvl < 327) {
		alpha_buf[0] = (aa_alpha_N11U_PANEL_PA[bl_lvl] >> 8) & 0xFF;
		alpha_buf[1] = aa_alpha_N11U_PANEL_PA[bl_lvl] & 0xFF;
	} else {
		alpha_buf[0] = 0x07;
		alpha_buf[1] = 0xFF;
		tmp_bl = bl_lvl * 4;
		DF_bl_reg_val[0] = (tmp_bl>> 8) & 0xff;
		DF_bl_reg_val[1] = tmp_bl & 0xff;
	}
	DISP_INFO("[%s] bl_lvl = %d, alpha = 0x%x%x\n",
			panel->type, bl_lvl, alpha_buf[0], alpha_buf[1]);

	switch (type) {
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
		lhbm_data_index = N11U_LHBM1000NITS;
		is_cal_lhbm_param = true;
		type_index = 0;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT_UPDATE;
		lhbm_data_index = N11U_LHBM110NITS;
		is_cal_lhbm_param = true;
		type_index = 1;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
		lhbm_data_index = N11U_LHBM1000NITS;
		is_cal_lhbm_param = true;
		type_index = 0;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
		lhbm_data_index = N11U_LHBM110NITS;
		is_cal_lhbm_param = true;
		type_index = 1;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_UPDATE;
		lhbm_data_index = N11U_LHBMGREEN500NITS;
		is_cal_lhbm_param = true;
		type_index = 2;
		break;
	default:
		DISP_ERROR("[%s] unsupport cmd %s\n", panel->type, cmd_set_prop_map[type]);
		return -EINVAL;
	}

	if(mi_cfg->read_lhbm_gamma_success) {
		if(lhbm_data_index == N11U_LHBMGREEN500NITS) {
			length = 2;
			update_D0_reg_ptr = &update_D0_reg[2];
		} else {
			length = 6;
			update_D0_reg_ptr = update_D0_reg;
		}

		if (flat_mode)
			memcpy(update_D0_reg_ptr, &mi_cfg->lhbm_gamma_param[lhbm_data_index + N11U_LHBMGIRONNITSOFFSET], length);
		else
			memcpy(update_D0_reg_ptr, &mi_cfg->lhbm_gamma_param[lhbm_data_index], length);

		if (is_cal_lhbm_param) {
			mi_dsi_panel_cal_lhbm_param_N11U(type, update_D0_reg_ptr, length, bl_lvl, type_index);
		}
		DISP_DEBUG("update lbhm data index %d, calculate data %d, length %d",
				lhbm_data_index, is_cal_lhbm_param, length);
	}

	cur_mode = panel->cur_mode;
	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for (i = 0; i < cmd_update_count; i++) {
		DISP_DEBUG("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
		if (info && info->mipi_address == 0xDF && info->length == 2) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				DF_bl_reg_val, sizeof(DF_bl_reg_val));
		}  else if (info && info->mipi_address == 0xD0 &&
					mi_cfg->read_lhbm_gamma_success) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				update_D0_reg, sizeof(update_D0_reg));
		} else if (info && info->mipi_address == 0x87) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				alpha_buf, sizeof(alpha_buf));
		}
		info++;
	}

	return rc;
}

static inline void mi_dsi_panel_cal_lhbm_param_N9(int type,
		u8 *update_val, u8 size, int bl_lvl)
{
	int i = 0;
	u64 tmp;
	u16 w1000_dev[3] = {110,109,108};
	u8 dev_index = 0;

	if (bl_lvl <= 0x0E1B)
		dev_index = 0;
	else if (bl_lvl > 0x0E1B && bl_lvl <= 0x1A09)
		dev_index = 1;
	else if (bl_lvl > 0x1A09)
		dev_index = 2;

	for (i = 0; i < size / 2; i++) {
		tmp = (update_val[i*2] << 8) | update_val[i*2+1];

	if (type == N9_LHBM1000NITS) {
			DISP_DEBUG("W1000 gamma update tmp[%d] * %d /100\n", tmp, w1000_dev[dev_index]);
			tmp = tmp * w1000_dev[dev_index] / 100;
		}

		update_val[i*2] = (tmp >> 8) & 0xFF;
		update_val[i*2 + 1] = tmp & 0xFF;
		DISP_DEBUG("gamma update tmp %d [%d] %d 0x%x  %x\n", i, bl_lvl, dev_index, update_val[i*2], update_val[i*2+ 1]);
	}
}

static int mi_dsi_panel_update_lhbm_param_N9(struct dsi_panel * panel,
		enum dsi_cmd_set_type type, int flat_mode, int bl_lvl)
{
	int rc = 0;
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	int i= 0;
	u8 *update_D0_reg_ptr = NULL;
	u8 update_D0_reg[6] = {0};
	u8 length = 6;
	u8 lhbm_data_index = 0;
	u8 alpha_buf[3] = {0};
	u8 update_DF_reg = 0;
	u8 update_DF_reg_offset[2] = {0};
	u8 update_B9_reg = 0;
	bool is_cal_lhbm_param = false;
	int type_index = 0;

	if (mi_get_panel_id_by_dsi_panel(panel) != N9_PANEL_PA) {
		DISP_DEBUG("don't need update lhbm command\n");
		rc = 0;
	}

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;
	mi_cfg = &panel->mi_cfg;

	if (panel->id_config.build_id < N9_PANEL_PA_P11) {
		if ( bl_lvl < 8192 ) {
			alpha_buf[0] = 0x25;
			alpha_buf[1] = (aa_alpha_N9_PANEL_PA_P01[bl_lvl] >> 8) & 0xFF;
			alpha_buf[2] = aa_alpha_N9_PANEL_PA_P01[bl_lvl] & 0xFF;
		} else {
			alpha_buf[0] = 0x25;
			alpha_buf[1] = (aa_alpha_N9_PANEL_PA_P01[8191] >> 8) & 0xFF;
			alpha_buf[2] = aa_alpha_N9_PANEL_PA_P01[8191] & 0xFF;
		}
	} else if (panel->id_config.build_id < N9_PANEL_PA_P2) {
		if ( bl_lvl < 985 ) {
			alpha_buf[0] = 0x25;
			alpha_buf[1] = (aa_alpha_N9_PANEL_PA_P11[bl_lvl] >> 8) & 0xFF;
			alpha_buf[2] = aa_alpha_N9_PANEL_PA_P11[bl_lvl] & 0xFF;
			update_DF_reg = 0x09;
			update_DF_reg_offset[1] = 0x16;
			update_B9_reg = 0xE0;
		} else {
			alpha_buf[0] = 0x25;
			alpha_buf[1] = (aa_alpha_N9_PANEL_PA_P11[984] >> 8) & 0xFF;
			alpha_buf[2] = aa_alpha_N9_PANEL_PA_P11[984] & 0xFF;
			update_DF_reg = 0x01;
			update_DF_reg_offset[1] = 0x09;
			update_B9_reg = 0x00;
		}
	} else {
		if ( bl_lvl < 1312 ) {
			bl_lvl = (8*bl_lvl)/10;
			alpha_buf[0] = 0x25;
			alpha_buf[1] = (aa_alpha_N9_PANEL_PA_P2[bl_lvl] >> 8) & 0xFF;
			alpha_buf[2] = aa_alpha_N9_PANEL_PA_P2[bl_lvl] & 0xFF;
			update_DF_reg = 0x09;
			update_DF_reg_offset[1] = 0x16;
			update_B9_reg = 0xE0;
		} else {
			alpha_buf[0] = 0x25;
			alpha_buf[1] = (aa_alpha_N9_PANEL_PA_P2[1311] >> 8) & 0xFF;
			alpha_buf[2] = aa_alpha_N9_PANEL_PA_P2[1311] & 0xFF;
			update_DF_reg = 0x01;
			update_DF_reg_offset[1] = 0x09;
			update_B9_reg = 0x00;
		}
	}

	DISP_INFO("[%s] bl_lvl = %d, alpha_buf[0] = 0x%x, alpha_buff[1] = 0x%x, alpha_buff[2] = 0x%x\n", panel->type, bl_lvl, alpha_buf[0], alpha_buf[1], alpha_buf[2]);

	switch (type) {
		case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
			cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
			lhbm_data_index = N9_LHBM1000NITS;
			is_cal_lhbm_param = true;
			type_index = 0;
			break;
		case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT:
			cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT_UPDATE;
			lhbm_data_index = N9_LHBM110NITS;
			is_cal_lhbm_param = true;
			type_index = 1;
			break;
		case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
			cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
			lhbm_data_index = N9_LHBM1000NITS;
			is_cal_lhbm_param = true;
			type_index = 0;
			break;
		case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
			cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
			lhbm_data_index = N9_LHBM110NITS;
			is_cal_lhbm_param = true;
			type_index = 1;
			break;
		case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT:
			cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_UPDATE;
			lhbm_data_index = N9_LHBMGREEN500NITS;
			is_cal_lhbm_param = true;
			type_index = 2;
			break;
		default:
			DISP_ERROR("[%s] unsupport cmd %s\n", panel->type, cmd_set_prop_map[type]);
			return -EINVAL;
	}

	if(mi_cfg->read_lhbm_gamma_success) {
		if(lhbm_data_index == N9_LHBMGREEN500NITS) {
			length = 2;
			update_D0_reg_ptr = &update_D0_reg[2];
		} else {
			length = 6;
			update_D0_reg_ptr = update_D0_reg;
		}
		memcpy(update_D0_reg_ptr, &mi_cfg->lhbm_rgb_param[lhbm_data_index], length);

		if (lhbm_data_index == N9_LHBM1000NITS && panel->id_config.build_id >= N9_PANEL_PA_P2) {
			mi_dsi_panel_cal_lhbm_param_N9(lhbm_data_index, update_D0_reg_ptr, length, bl_lvl);
		}
		DISP_DEBUG("update lbhm data index %d, calculate data %d, length %d",
				lhbm_data_index, is_cal_lhbm_param, length);
	}

	cur_mode = panel->cur_mode;
	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for (i = 0; i < cmd_update_count; i++) {
		DISP_DEBUG("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
			if (info && info->mipi_address == 0xD1 &&
					mi_cfg->read_lhbm_gamma_success) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				update_D0_reg, sizeof(update_D0_reg));
			} else if (info && info->mipi_address == 0x87) {
				mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
					alpha_buf, sizeof(alpha_buf));
			} else if (info && info->mipi_address == 0xDF && info->length == 1) {
				mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
					&update_DF_reg, sizeof(update_DF_reg));
			} else if (info && info->mipi_address == 0xDF && info->length == 2) {
				mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
					update_DF_reg_offset, sizeof(update_DF_reg_offset));
			} else if (info && info->mipi_address == 0xB9) {
				mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
					&update_B9_reg, sizeof(update_B9_reg));
			}
		info++;
	}

	return rc;
}

int mi_dsi_panel_set_round_corner_locked(struct dsi_panel *panel,
			bool enable)
{
	int rc = 0;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (panel->mi_cfg.ddic_round_corner_enabled) {
		if (enable)
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ROUND_CORNER_ON);
		else
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ROUND_CORNER_OFF);

		if (rc)
			DISP_ERROR("[%s] failed to send ROUND_CORNER(%s) cmds, rc=%d\n",
				panel->type, enable ? "On" : "Off", rc);
	} else {
		DISP_INFO("[%s] ddic round corner feature not enabled\n", panel->type);
	}

	return rc;
}

int mi_dsi_panel_set_round_corner(struct dsi_panel *panel, bool enable)
{
	int rc = 0;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = mi_dsi_panel_set_round_corner_locked(panel, enable);

	mutex_unlock(&panel->panel_lock);

	return rc;
}

int mi_dsi_panel_set_dc_mode(struct dsi_panel *panel, bool enable)
{
	int rc = 0;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = mi_dsi_panel_set_dc_mode_locked(panel, enable);

	mutex_unlock(&panel->panel_lock);

	return rc;
}


int mi_dsi_panel_set_dc_mode_locked(struct dsi_panel *panel, bool enable)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_get_panel_id(mi_cfg->mi_panel_id) == N3_PANEL_PA &&
		panel->id_config.build_id < N3_PANEL_PA_P20){
		DISP_INFO("N3 not support DC for  build_id %d \n",panel->id_config.build_id);
		return rc ;
	}

	if (mi_cfg->dc_feature_enable) {
		if (enable) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DC_ON);
			mi_cfg->real_dc_state = FEATURE_ON;
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DC_OFF);
			mi_cfg->real_dc_state = FEATURE_OFF;
		}
		if (rc)
			DISP_ERROR("failed to set DC mode: %d\n", enable);
		else
			DISP_INFO("DC mode: %s\n", enable ? "On" : "Off");
	} else {
		DISP_INFO("DC mode: TODO\n");
	}

	return rc;
}

int mi_dsi_panel_set_ltmp_cmpst_locked(struct dsi_panel *panel, bool enable)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	if (mi_get_panel_id(mi_cfg->mi_panel_id) != N3_PANEL_PA) {
		return rc ;
	}

	if (mi_cfg->ltmp_cmpst_on_threshold) {
		if (enable) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_ON);
			mi_cfg->real_ltmp_cmpst_state = FEATURE_ON;
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LTEMP_CMPST_OFF);
			mi_cfg->real_ltmp_cmpst_state = FEATURE_OFF;
		}

		if (rc)
			DISP_ERROR("failed to set ltmp compensation mode: %d\n", enable);
		else
			DISP_DEBUG("ltmp compensation mode: %s\n", enable ? "On" : "Off");
	} else {
		DISP_DEBUG("ltmp compensation mode: TODO\n");
	}

	return rc;
}

int mi_dsi_panel_set_lhbm_0size_locked(struct dsi_panel *panel)
{

	int rc = 0;
	int update_bl_lvl = 0;
	bool lhbm_is_0size = false;
	enum dsi_cmd_set_type lhbm_type = DSI_CMD_SET_MAX;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	DISP_DEBUG("LOCAL_HBM_OFF_TO_0SIZE_LHBM\n");

	mi_cfg->dimming_state = STATE_DIM_BLOCK;
	lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT;
	lhbm_is_0size = true;
	if (mi_cfg->last_bl_level > mi_cfg->lhbm_hbm_mode_threshold) {
		DISP_DEBUG("LOCAL_HBM_OFF_TO_0SIZE_LHBM in HBM\n");
		update_bl_lvl = mi_cfg->lhbm_hbm_mode_threshold;
		mi_dsi_update_51_mipi_cmd(panel,
				DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_PRE, update_bl_lvl);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_PRE);
	} else {
		DISP_DEBUG("LOCAL_HBM_OFF_TO_0SIZE_LHBM\n");
		if (is_aod_and_panel_initialized(panel)) {
			switch (mi_cfg->doze_brightness) {
			case DOZE_BRIGHTNESS_HBM:
				update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
				DISP_DEBUG("LOCAL_HBM_OFF_TO_0SIZE_LHBM in doze_hbm_dbv_level\n");
				break;
			case DOZE_BRIGHTNESS_LBM:
				update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
				DISP_DEBUG("LOCAL_HBM_OFF_TO_0SIZE_LHBM in doze_lbm_dbv_level\n");
				break;
			default:
				update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
				DISP_DEBUG("LOCAL_HBM_OFF_TO_0SIZE_LHBM use doze_hbm_dbv_level as defaults\n");
				break;
			}
		} else {
			update_bl_lvl = mi_cfg->last_bl_level;
		}
	}
	mi_dsi_update_51_mipi_cmd(panel, lhbm_type, update_bl_lvl);
	mi_dsi_panel_update_lhbm_param_N3(panel, lhbm_type, update_bl_lvl, lhbm_is_0size);
	rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
	mi_cfg->lhbm_0size_on = true;

	return rc;
}

int mi_dsi_panel_set_lhbm_fod_locked(struct dsi_panel *panel,
		struct disp_feature_ctl *ctl)
{
	int rc = 0;
	int update_bl_lvl = 0;
	bool lhbm_is_0size = false;
	u32 last_bl_level_store = 0;
	enum dsi_cmd_set_type lhbm_type = DSI_CMD_SET_MAX;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel || !ctl || ctl->feature_id != DISP_FEATURE_LOCAL_HBM) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS] > 0) {
		DISP_TIME_INFO("fod calibration brightness, update alpha,"
				"last bl (%d)-->fod calibration bl(%d)",
				mi_cfg->last_bl_level,
				mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS]);
		last_bl_level_store = mi_cfg->last_bl_level;
		mi_cfg->last_bl_level = mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS];
	}

	DISP_TIME_INFO("[%s] Local HBM: %s\n", panel->type,
			get_local_hbm_state_name(ctl->feature_val));

	switch (ctl->feature_val) {
	case LOCAL_HBM_OFF_TO_NORMAL:
		if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N16T_PANEL_PC
			&& mi_cfg->last_bl_level > mi_cfg->lhbm_hbm_mode_threshold) {
			DISP_INFO("N16T_PANEL_PC skip update backlight\n");
		} else {
			mi_dsi_update_backlight_in_aod(panel, true);
		}
		if (mi_cfg->last_bl_level > mi_cfg->lhbm_hbm_mode_threshold) {
			DISP_INFO("LOCAL_HBM_OFF_TO_HBM\n");
			mi_cfg->dimming_state = STATE_DIM_BLOCK;
			lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_HBM;
			mi_dsi_update_51_mipi_cmd(panel, lhbm_type,
					mi_cfg->last_bl_level);
			if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N16T_PANEL_PC) {
				if(mi_cfg->last_bl_level == 4095) {
					lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL_4095;
				} else {
					update_bl_lvl = mi_cfg->last_bl_level;
					mi_dsi_panel_update_lhbm_param_N16T_PC(panel, lhbm_type, update_bl_lvl);
				}
			}
			rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		} else if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N3_PANEL_PA &&
			mi_cfg->lhbm_gxzw &&
			mi_cfg->feature_val[DISP_FEATURE_FP_STATUS] != AUTH_STOP) {
			rc = mi_dsi_panel_set_lhbm_0size_locked(panel);
			DISP_DEBUG("LOCAL_HBM_TO_0SIZE_LHBM in off\n");
		} else {
			DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL\n");
			panel->mi_cfg.lhbm_0size_on = false;
			lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL;
			update_bl_lvl = mi_cfg->last_bl_level;
			mi_dsi_update_51_mipi_cmd(panel, lhbm_type,
				mi_cfg->last_bl_level);
			if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N16T_PANEL_PC) {
				mi_dsi_panel_update_lhbm_param_N16T_PC(panel, lhbm_type, update_bl_lvl);
			}
			rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
			mi_cfg->lhbm_0size_on = false;

			mi_cfg->dimming_state = STATE_DIM_RESTORE;
			mi_dsi_panel_update_last_bl_level(panel, mi_cfg->last_bl_level);
			if (mi_get_panel_id_by_dsi_panel(panel) == N3_PANEL_PA &&
				panel->mi_cfg.ltmp_cmpst_on_threshold &&
				mi_cfg->last_bl_level > panel->mi_cfg.ltmp_cmpst_on_threshold) {
				mi_dsi_panel_set_ltmp_cmpst_locked(panel, true);
			} else if (mi_get_panel_id_by_dsi_panel(panel) == N3_PANEL_PA &&
				panel->mi_cfg.ltmp_cmpst_on_threshold &&
				mi_cfg->last_bl_level <= panel->mi_cfg.ltmp_cmpst_on_threshold) {
				mi_dsi_panel_set_ltmp_cmpst_locked(panel, false);
			}
		}
		/*normal fod, restore dbi by dbv*/
		if (mi_get_panel_id(mi_cfg->mi_panel_id) == N11U_PANEL_PA
		|| mi_get_panel_id(mi_cfg->mi_panel_id) == N2_PANEL_PA
		|| mi_get_panel_id(mi_cfg->mi_panel_id) == N1_PANEL_PA
		|| mi_get_panel_id(mi_cfg->mi_panel_id) == N9_PANEL_PA) {
  			mi_cfg->dbi_bwg_type = DSI_CMD_SET_MAX;
			dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
  		}
		break;
	case LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT:
		DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT\n");
		mi_dsi_update_backlight_in_aod(panel, false);
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL;
		/* display backlight value should equal AOD brightness */
		if (is_aod_and_panel_initialized(panel)) {
			switch (mi_cfg->doze_brightness) {
			case DOZE_BRIGHTNESS_HBM:
				if (mi_cfg->last_no_zero_bl_level < mi_cfg->doze_hbm_dbv_level
					&& mi_cfg->feature_val[DISP_FEATURE_FP_STATUS] == AUTH_STOP) {
					update_bl_lvl = mi_cfg->last_no_zero_bl_level;
					mi_dsi_update_51_mipi_cmd(panel, lhbm_type,
						mi_cfg->last_no_zero_bl_level);
					dsi_panel_send_em_cycle_setting(panel, mi_cfg->last_no_zero_bl_level, true);
				} else {
					update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
					mi_dsi_update_51_mipi_cmd(panel, lhbm_type,
						mi_cfg->doze_hbm_dbv_level);
					dsi_panel_send_em_cycle_setting(panel, mi_cfg->doze_hbm_dbv_level, true);
				}
				DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT in doze_hbm_dbv_level\n");
				break;
			case DOZE_BRIGHTNESS_LBM:
				if (mi_cfg->last_no_zero_bl_level < mi_cfg->doze_lbm_dbv_level) {
					update_bl_lvl = mi_cfg->last_no_zero_bl_level;
					mi_dsi_update_51_mipi_cmd(panel, lhbm_type,
						mi_cfg->last_no_zero_bl_level);
					dsi_panel_send_em_cycle_setting(panel, mi_cfg->last_no_zero_bl_level, true);
				} else {
					update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
					mi_dsi_update_51_mipi_cmd(panel, lhbm_type,
						mi_cfg->doze_lbm_dbv_level);
					dsi_panel_send_em_cycle_setting(panel, mi_cfg->doze_lbm_dbv_level, true);
				}
				DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT in doze_lbm_dbv_level\n");
				break;
			default:
				DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT defaults\n");
				break;
			}
		} else {
			update_bl_lvl = mi_cfg->last_bl_level;
			mi_dsi_update_51_mipi_cmd(panel, lhbm_type, mi_cfg->last_bl_level);
			dsi_panel_send_em_cycle_setting(panel, mi_cfg->last_bl_level, true);
		}
		if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N16T_PANEL_PC) {
			if(update_bl_lvl == 4095) {
				lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL_4095;
			} else {
				mi_dsi_panel_update_lhbm_param_N16T_PC(panel, lhbm_type, update_bl_lvl);
			}
		}
		DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL\n");
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		mi_cfg->lhbm_0size_on = false;
		if (mi_cfg->need_fod_animal_in_normal)
			panel->mi_cfg.aod_to_normal_statue = true;
		if (mi_get_panel_id_by_dsi_panel(panel) == N3_PANEL_PA &&
			panel->mi_cfg.ltmp_cmpst_on_threshold &&
			mi_cfg->last_bl_level > panel->mi_cfg.ltmp_cmpst_on_threshold) {
			mi_dsi_panel_set_ltmp_cmpst_locked(panel, true);
		} else if (mi_get_panel_id_by_dsi_panel(panel) == N3_PANEL_PA &&
			panel->mi_cfg.ltmp_cmpst_on_threshold &&
			mi_cfg->last_bl_level <= panel->mi_cfg.ltmp_cmpst_on_threshold) {
			mi_dsi_panel_set_ltmp_cmpst_locked(panel, false);
		}
		mi_cfg->dimming_state = STATE_DIM_RESTORE;
		break;
	case LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT_RESTORE:
		/* display backlight value should equal unlock brightness */
		DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT_RESTORE\n");
		mi_dsi_update_backlight_in_aod(panel, true);
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL;
		mi_dsi_update_51_mipi_cmd(panel, lhbm_type,mi_cfg->last_bl_level);
		update_bl_lvl = mi_cfg->last_bl_level;
		if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N16T_PANEL_PC) {
			if(update_bl_lvl == 4095) {
				lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL_4095;
			} else {
				mi_dsi_panel_update_lhbm_param_N16T_PC(panel, lhbm_type, update_bl_lvl);
			}
		}
		DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL\n");
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		panel->mi_cfg.lhbm_0size_on = false;

		/*mi_dsi_update_backlight_in_aod will change 51's value */
		if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N11U_PANEL_PA) {
			dsi_panel_send_em_cycle_setting(panel, mi_cfg->last_bl_level, true);
		}
		if (mi_get_panel_id_by_dsi_panel(panel) == N3_PANEL_PA &&
			panel->mi_cfg.ltmp_cmpst_on_threshold &&
			mi_cfg->last_bl_level > panel->mi_cfg.ltmp_cmpst_on_threshold) {
			mi_dsi_panel_set_ltmp_cmpst_locked(panel, true);
		} else if (mi_get_panel_id_by_dsi_panel(panel) == N3_PANEL_PA &&
			panel->mi_cfg.ltmp_cmpst_on_threshold &&
			mi_cfg->last_bl_level <= panel->mi_cfg.ltmp_cmpst_on_threshold) {
			mi_dsi_panel_set_ltmp_cmpst_locked(panel, false);
		}
		mi_cfg->dimming_state = STATE_DIM_RESTORE;
		break;
	case LOCAL_HBM_NORMAL_WHITE_1000NIT:
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT;
		if (mi_cfg->last_bl_level > mi_cfg->lhbm_hbm_mode_threshold) {
			DISP_INFO("LOCAL_HBM_NORMAL_WHITE_1000NIT in HBM\n");
			if (mi_get_panel_id(mi_cfg->mi_panel_id) == N11U_PANEL_PA ||
				mi_get_panel_id(mi_cfg->mi_panel_id) == N2_PANEL_PA ||
				mi_get_panel_id(mi_cfg->mi_panel_id) == N1_PANEL_PA)
				update_bl_lvl = mi_cfg->last_bl_level;
			else if (mi_get_panel_id(mi_cfg->mi_panel_id) == N3_PANEL_PA ||
					 mi_get_panel_id(mi_cfg->mi_panel_id) == N9_PANEL_PA) {
				update_bl_lvl = mi_cfg->lhbm_hbm_mode_threshold;
				mi_dsi_update_51_mipi_cmd(panel,
					DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_PRE, update_bl_lvl);
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_PRE);
			} else if (mi_get_panel_id(mi_cfg->mi_panel_id) == N16T_PANEL_PC) {
				if(mi_cfg->last_bl_level == 4095) {
					update_bl_lvl = mi_cfg->last_bl_level;
				} else {
					update_bl_lvl = mi_cfg->lhbm_hbm_mode_threshold;
				}
			} else {
				update_bl_lvl = panel->bl_config.bl_max_level;
			}
		} else {
			DISP_DEBUG("LOCAL_HBM_NORMAL_WHITE_1000NIT\n");
			if (is_aod_and_panel_initialized(panel)) {
				switch (mi_cfg->doze_brightness) {
				case DOZE_BRIGHTNESS_HBM:
					update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
					DISP_INFO("LOCAL_HBM_NORMAL_WHITE_1000NIT in doze_hbm_dbv_level\n");
					break;
				case DOZE_BRIGHTNESS_LBM:
					update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
					DISP_INFO("LOCAL_HBM_NORMAL_WHITE_1000NIT in doze_lbm_dbv_level\n");
					break;
				default:
					update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
					DISP_INFO("LOCAL_HBM_NORMAL_WHITE_1000NIT use doze_hbm_dbv_level as defaults\n");
					break;
				}
			} else {
				update_bl_lvl = mi_cfg->last_bl_level;
			}
		}
		mi_dsi_update_51_mipi_cmd(panel, lhbm_type, update_bl_lvl);
		switch (mi_get_panel_id_by_dsi_panel(panel)) {
		case N1_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N1(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N2_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N2(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N3_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N3(panel, lhbm_type, update_bl_lvl, lhbm_is_0size);
			break;
		case N11U_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N11U(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N9_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N9(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N16T_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N16T(panel, lhbm_type, update_bl_lvl);
			break;
		case N16T_PANEL_PB:
			mi_dsi_panel_update_lhbm_param_N16T_PB(panel, lhbm_type, update_bl_lvl);
			break;
		case N16T_PANEL_PC:
			if(update_bl_lvl == 4095) {
				lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_4095;
			} else {
				mi_dsi_panel_update_lhbm_param_N16T_PC(panel, lhbm_type, update_bl_lvl);
			}
			break;
		case O16U_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_O16U(panel, lhbm_type, update_bl_lvl);
			break;
		case O16U_PANEL_PB:
			mi_dsi_panel_update_lhbm_param_O16U_PB(panel, lhbm_type, update_bl_lvl);
			break;
		default:
			DISP_ERROR("unknown panel name\n");
			break;
		}
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		panel->mi_cfg.lhbm_0size_on = false;
		break;
	case LOCAL_HBM_NORMAL_WHITE_750NIT:
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_750NIT;
		DISP_INFO("LOCAL_HBM_NORMAL_WHITE_750NIT\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		break;
	case LOCAL_HBM_NORMAL_WHITE_500NIT:
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_500NIT;
		DISP_INFO("LOCAL_HBM_NORMAL_WHITE_500NIT\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		break;
	case LOCAL_HBM_NORMAL_WHITE_110NIT:
		DISP_DEBUG("LOCAL_HBM_NORMAL_WHITE_110NIT\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT;

		update_bl_lvl = mi_cfg->last_bl_level;

		if (is_aod_and_panel_initialized(panel)) {
			switch (mi_cfg->doze_brightness) {
			case DOZE_BRIGHTNESS_HBM:
				update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
				DISP_INFO("LOCAL_HBM_NORMAL_WHITE_110NIT in doze_hbm_dbv_level\n");
				break;
			case DOZE_BRIGHTNESS_LBM:
				update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
				DISP_INFO("LOCAL_HBM_NORMAL_WHITE_110NIT in doze_lbm_dbv_level\n");
				break;
			default:
				update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
				DISP_INFO("LOCAL_HBM_NORMAL_WHITE_110NIT use doze_lbm_dbv_level as defaults\n");
				break;
			}
		}
		switch (mi_get_panel_id_by_dsi_panel(panel)) {
		case N1_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N1(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N2_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N2(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N3_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N3(panel, lhbm_type, update_bl_lvl, lhbm_is_0size);
			break;
		case N11U_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N11U(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N9_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N9(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N16T_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N16T(panel, lhbm_type, update_bl_lvl);
			break;
		case N16T_PANEL_PB:
			mi_dsi_panel_update_lhbm_param_N16T_PB(panel, lhbm_type, update_bl_lvl);
			break;
		case N16T_PANEL_PC:
			mi_dsi_panel_update_lhbm_param_N16T_PC(panel, lhbm_type, update_bl_lvl);
			break;
		case O16U_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_O16U(panel, lhbm_type, update_bl_lvl);
			break;
		case O16U_PANEL_PB:
			mi_dsi_panel_update_lhbm_param_O16U_PB(panel, lhbm_type, update_bl_lvl);
			break;
		default:
			DISP_ERROR("unknown panel name\n");
			break;
		}
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		panel->mi_cfg.lhbm_0size_on = false;
		break;
	case LOCAL_HBM_NORMAL_GREEN_500NIT:
		DISP_INFO("LOCAL_HBM_NORMAL_GREEN_500NIT\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT;
		update_bl_lvl = mi_cfg->last_bl_level;
		switch (mi_get_panel_id_by_dsi_panel(panel)) {
		case N1_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N1(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N2_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N2(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N3_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N3(panel, lhbm_type, update_bl_lvl, lhbm_is_0size);
			break;
		case N11U_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N11U(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N9_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N9(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N16T_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N16T(panel, lhbm_type, update_bl_lvl);
			break;
		case N16T_PANEL_PB:
			mi_dsi_panel_update_lhbm_param_N16T_PB(panel, lhbm_type, update_bl_lvl);
			break;
		case N16T_PANEL_PC:
			if(update_bl_lvl > 2047 && update_bl_lvl < 4095) {
				update_bl_lvl = 2047;
				mi_dsi_update_51_mipi_cmd(panel, lhbm_type, update_bl_lvl);
			}
			if(update_bl_lvl == 4095) {
				lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_4095;
			} else {
				mi_dsi_panel_update_lhbm_param_N16T_PC(panel, lhbm_type, update_bl_lvl);
				mi_dsi_update_51_mipi_cmd(panel, lhbm_type, update_bl_lvl);
			}
			break;
		case O16U_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_O16U(panel, lhbm_type, update_bl_lvl);
			break;
		case O16U_PANEL_PB:
			mi_dsi_panel_update_lhbm_param_O16U_PB(panel, lhbm_type, update_bl_lvl);
			break;
		default:
			DISP_ERROR("unknown panel name\n");
			break;
		}
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		panel->mi_cfg.lhbm_0size_on = false;
		break;
	case LOCAL_HBM_HLPM_WHITE_1000NIT:
		DISP_DEBUG("LOCAL_HBM_HLPM_WHITE_1000NIT\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT;
		update_bl_lvl = mi_cfg->last_bl_level;

		if (is_aod_and_panel_initialized(panel)) {
			switch (mi_cfg->doze_brightness) {
			case DOZE_BRIGHTNESS_HBM:
				update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
				DISP_INFO("LOCAL_HBM_HLPM_WHITE_1000NIT in doze_hbm_dbv_level\n");
				break;
			case DOZE_BRIGHTNESS_LBM:
				update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
				DISP_INFO("LOCAL_HBM_HLPM_WHITE_1000NIT in doze_lbm_dbv_level\n");
				break;
			default:
				update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
				DISP_INFO("LOCAL_HBM_HLPM_WHITE_1000NIT use doze_hbm_dbv_level as defaults\n");
				break;
			}
		}
		mi_dsi_update_51_mipi_cmd(panel, lhbm_type, update_bl_lvl);
		mi_dsi_update_backlight_in_aod(panel, false);
		switch (mi_get_panel_id_by_dsi_panel(panel)) {
		case N1_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N1(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N2_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N2(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N3_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N3(panel, lhbm_type, update_bl_lvl, lhbm_is_0size);
			break;
		case N11U_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N11U(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N9_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N9(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N16T_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N16T(panel, lhbm_type, update_bl_lvl);
			break;
		case N16T_PANEL_PB:
			mi_dsi_panel_update_lhbm_param_N16T_PB(panel, lhbm_type, update_bl_lvl);
			break;
		case N16T_PANEL_PC:
			mi_dsi_panel_update_lhbm_param_N16T_PC(panel, lhbm_type, update_bl_lvl);
			break;
		case O16U_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_O16U(panel, lhbm_type, update_bl_lvl);
			break;
		case O16U_PANEL_PB:
			mi_dsi_panel_update_lhbm_param_O16U_PB(panel, lhbm_type, update_bl_lvl);
			break;
		default:
			DISP_ERROR("unknown panel name\n");
			break;
		}
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		panel->mi_cfg.panel_state = PANEL_STATE_ON;
		break;
	case LOCAL_HBM_HLPM_WHITE_110NIT:
		DISP_DEBUG("LOCAL_HBM_HLPM_WHITE_110NIT\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT;
		update_bl_lvl = mi_cfg->last_bl_level;

		if (is_aod_and_panel_initialized(panel)) {
			switch (mi_cfg->doze_brightness) {
			case DOZE_BRIGHTNESS_HBM:
				update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
				DISP_INFO("LOCAL_HBM_HLPM_WHITE_110NIT in doze_hbm_dbv_level\n");
				break;
			case DOZE_BRIGHTNESS_LBM:
				update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
				DISP_INFO("LOCAL_HBM_HLPM_WHITE_110NIT in doze_lbm_dbv_level\n");
				break;
			default:
				update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
				DISP_INFO("LOCAL_HBM_HLPM_WHITE_1000NIT use doze_lbm_dbv_level as defaults\n");
				break;
			}
		}
		mi_dsi_update_51_mipi_cmd(panel,lhbm_type, update_bl_lvl);

		mi_dsi_update_backlight_in_aod(panel, false);
		switch (mi_get_panel_id_by_dsi_panel(panel)) {
		case N1_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N1(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N2_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N2(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N3_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N3(panel, lhbm_type, update_bl_lvl, lhbm_is_0size);
			break;
		case N11U_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N11U(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N9_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N9(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N16T_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N16T(panel, lhbm_type, update_bl_lvl);
			break;
		case N16T_PANEL_PB:
			mi_dsi_panel_update_lhbm_param_N16T_PB(panel, lhbm_type, update_bl_lvl);
			break;
		case N16T_PANEL_PC:
			mi_dsi_panel_update_lhbm_param_N16T_PC(panel, lhbm_type, update_bl_lvl);
			break;
		case O16U_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_O16U(panel, lhbm_type, update_bl_lvl);
			break;
		case O16U_PANEL_PB:
			mi_dsi_panel_update_lhbm_param_O16U_PB(panel, lhbm_type, update_bl_lvl);
			break;
		default:
			DISP_ERROR("unknown panel name\n");
			break;
		}
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		panel->mi_cfg.panel_state = PANEL_STATE_ON;
		break;
	case LOCAL_HBM_OFF_TO_HLPM:
		DISP_INFO("LOCAL_HBM_OFF_TO_HLPM\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		panel->mi_cfg.panel_state = PANEL_STATE_DOZE_HIGH;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_HLPM;
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		break;
	case LOCAL_HBM_OFF_TO_LLPM:
		DISP_INFO("LOCAL_HBM_OFF_TO_LLPM\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		panel->mi_cfg.panel_state = PANEL_STATE_DOZE_LOW;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_LLPM;
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		break;
	default:
		DISP_ERROR("invalid local hbm value\n");
		break;
	}

	if (mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS] > 0)
		mi_cfg->last_bl_level = last_bl_level_store;

	return rc;
}

int mi_dsi_panel_aod_to_normal_optimize_locked(struct dsi_panel *panel,
		bool enable)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel || !panel->panel_initialized) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (!mi_cfg->need_fod_animal_in_normal)
		return 0;

	DISP_TIME_DEBUG("[%s] fod aod_to_normal: %d\n", panel->type, enable);

	if (is_hbm_fod_on(panel)) {
		DSI_INFO("fod hbm on, skip fod aod_to_normal: %d\n", enable);
		return 0;
	}

	if (enable && mi_cfg->panel_state != PANEL_STATE_ON) {
		switch (mi_cfg->doze_brightness) {
		case DOZE_BRIGHTNESS_HBM:
			DISP_INFO("enter DOZE HBM NOLP\n");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM_NOLP);
			if (rc) {
				DISP_ERROR("[%s] failed to send MI_DOZE_HBM_NOLP cmd, rc=%d\n",
					panel->type, rc);
			} else {
				panel->mi_cfg.panel_state = PANEL_STATE_ON;
				mi_cfg->aod_to_normal_statue = true;
				if (mi_get_panel_id_by_dsi_panel(panel) == N11U_PANEL_PA)
					dsi_panel_send_em_cycle_setting(panel, 0xFC, true);
			}
			break;
		case DOZE_BRIGHTNESS_LBM:
			DISP_INFO("enter DOZE LBM NOLP\n");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM_NOLP);
			if (rc) {
				DISP_ERROR("[%s] failed to send MI_DOZE_LBM_NOLP cmd, rc=%d\n",
					panel->type, rc);
			} else {
				panel->mi_cfg.panel_state = PANEL_STATE_ON;
				mi_cfg->aod_to_normal_statue = true;
				if (mi_get_panel_id_by_dsi_panel(panel) == N11U_PANEL_PA) {
					dsi_panel_send_em_cycle_setting(panel, 0x19, true);
				}
			}
			break;
		default:
			break;
		}
	} else if (!enable && mi_cfg->panel_state == PANEL_STATE_ON &&
		panel->power_mode != SDE_MODE_DPMS_ON &&
		mi_cfg->feature_val[DISP_FEATURE_FP_STATUS] != AUTH_STOP) {
		switch (mi_cfg->doze_brightness) {
		case DOZE_BRIGHTNESS_HBM:
			DISP_INFO("enter DOZE HBM\n");
			mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_DOZE_HBM,
				mi_cfg->doze_hbm_dbv_level);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
			if (rc) {
				DISP_ERROR("[%s] failed to send MI_DOZE_HBM_NOLP cmd, rc=%d\n",
					panel->type, rc);
			}
			panel->mi_cfg.panel_state = PANEL_STATE_DOZE_HIGH;
			mi_cfg->aod_to_normal_statue = false;
			break;
		case DOZE_BRIGHTNESS_LBM:
			DISP_INFO("enter DOZE LBM\n");
			mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_DOZE_LBM,
					mi_cfg->doze_lbm_dbv_level);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
			if (rc) {
				DISP_ERROR("[%s] failed to send MI_DOZE_LBM_NOLP cmd, rc=%d\n",
					panel->type, rc);
			}
			panel->mi_cfg.panel_state = PANEL_STATE_DOZE_LOW;
			mi_cfg->aod_to_normal_statue = false;
			break;
		default:
			break;
		}
	} else {
		rc = -EAGAIN;
	}

	return rc;
}

bool mi_dsi_panel_is_need_tx_cmd(u32 feature_id)
{
	switch (feature_id) {
	case DISP_FEATURE_SENSOR_LUX:
	case DISP_FEATURE_LOW_BRIGHTNESS_FOD:
	case DISP_FEATURE_FP_STATUS:
	case DISP_FEATURE_FOLD_STATUS:
		return false;
	default:
		return true;
	}
}

int mi_dsi_panel_set_disp_param(struct dsi_panel *panel, struct disp_feature_ctl *ctl)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;
	struct dsi_display *display;

	if (!panel || !ctl) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}
	if (!panel->host) {
		DISP_ERROR("invalid panel->host ptr\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	DISP_TIME_INFO("[%s] feature: %s, value: %d\n", panel->type,
			get_disp_feature_id_name(ctl->feature_id), ctl->feature_val);


	mi_cfg = &panel->mi_cfg;
	display = to_dsi_display(panel->host);
	if (!display->enabled &&
		mi_dsi_panel_is_need_tx_cmd(ctl->feature_id)) {
		if (ctl->feature_id == DISP_FEATURE_DC)
			mi_cfg->feature_val[DISP_FEATURE_DC] = ctl->feature_val;
		if (ctl->feature_id == DISP_FEATURE_DBI)
			mi_cfg->feature_val[DISP_FEATURE_DBI] = ctl->feature_val;
		DISP_WARN("[%s] panel not initialized!\n", panel->type);
		rc = -ENODEV;
		goto exit;
	}


	switch (ctl->feature_id) {
	case DISP_FEATURE_DIMMING:
		if (!mi_cfg->disable_ic_dimming){
			if (mi_cfg->dimming_state != STATE_DIM_BLOCK) {
				if (ctl->feature_val == FEATURE_ON )
					rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGON);
				else
					rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
				mi_cfg->feature_val[DISP_FEATURE_DIMMING] = ctl->feature_val;
			} else {
				DISP_INFO("skip dimming %s\n", ctl->feature_val ? "on" : "off");
			}
		} else{
			DISP_INFO("disable_ic_dimming is %d\n", mi_cfg->disable_ic_dimming);
		}
		break;
	case DISP_FEATURE_HBM:
		mi_cfg->feature_val[DISP_FEATURE_HBM] = ctl->feature_val;
#ifdef CONFIG_FACTORY_BUILD
		if (ctl->feature_val == FEATURE_ON) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_ON);
			mi_cfg->dimming_state = STATE_DIM_BLOCK;
		} else {
			mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_HBM_OFF,
					mi_cfg->last_bl_level);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_OFF);
			mi_cfg->dimming_state = STATE_DIM_RESTORE;
		}
#endif
		mi_disp_feature_event_notify_by_type(mi_get_disp_id(panel->type),
				MI_DISP_EVENT_HBM, sizeof(ctl->feature_val), ctl->feature_val);
		break;
	case DISP_FEATURE_HBM_FOD:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_DOZE_BRIGHTNESS:
#ifdef CONFIG_FACTORY_BUILD
		if (dsi_panel_initialized(panel) &&
			is_aod_brightness(ctl->feature_val)) {
			if (ctl->feature_val == DOZE_BRIGHTNESS_HBM) {
				if(mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_PA && (panel->id_config.build_id < 0x61)) {
					mi_dsi_update_switch_cmd_N8(panel, DSI_CMD_SET_MI_DOZE_HBM_UPDATE, DSI_CMD_SET_MI_DOZE_HBM);
				}
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
			}
			else {
				if(mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_PA && (panel->id_config.build_id < 0x61)) {
					mi_dsi_update_switch_cmd_N8(panel, DSI_CMD_SET_MI_DOZE_LBM_UPDATE, DSI_CMD_SET_MI_DOZE_LBM);
				}
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
			}
		} else {
			if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N3_PANEL_PA) {
				mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_NOLP_UPDATE,
							DSI_CMD_SET_NOLP);
			}
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NOLP);

			if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N18_PANEL_PA ||
			        mi_get_panel_id(panel->mi_cfg.mi_panel_id) == O11_PANEL_PA)
				rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_TIMING_SWITCH);

			dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
		}
#else
		if (is_aod_and_panel_initialized(panel) &&
			is_aod_brightness(ctl->feature_val)) {
			if (ctl->feature_val == DOZE_BRIGHTNESS_HBM) {
				if(mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_PA && (panel->id_config.build_id < 0x61)) {
					mi_dsi_update_switch_cmd_N8(panel, DSI_CMD_SET_MI_DOZE_HBM_UPDATE, DSI_CMD_SET_MI_DOZE_HBM);
				}
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
			} else {
				if(mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_PA && (panel->id_config.build_id < 0x61)) {
					mi_dsi_update_switch_cmd_N8(panel, DSI_CMD_SET_MI_DOZE_LBM_UPDATE, DSI_CMD_SET_MI_DOZE_LBM);
				}
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
			}
		} else {
			if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N3_PANEL_PA) {
				mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_NOLP_UPDATE,
							DSI_CMD_SET_NOLP);
			}
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NOLP);

			if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N18_PANEL_PA ||
			        mi_get_panel_id(panel->mi_cfg.mi_panel_id) == O11_PANEL_PA)
				rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_TIMING_SWITCH);
		}
#endif
		mi_cfg->feature_val[DISP_FEATURE_DOZE_BRIGHTNESS] = ctl->feature_val;
		break;
	case DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS:
		if (ctl->feature_val == -1) {
			DISP_INFO("FOD calibration brightness restore last_bl_level=%d\n",
				mi_cfg->last_bl_level);
			dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
			mi_cfg->in_fod_calibration = false;
		} else {
			if (ctl->feature_val >= 0 &&
				ctl->feature_val <= panel->bl_config.bl_max_level) {
				mi_cfg->in_fod_calibration = true;
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
				dsi_panel_update_backlight(panel, ctl->feature_val);
				mi_cfg->dimming_state = STATE_NONE;
			} else {
				mi_cfg->in_fod_calibration = false;
				DISP_ERROR("FOD calibration invalid brightness level:%d\n",
						ctl->feature_val);
			}
		}
		mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS] = ctl->feature_val;
		break;
	case DISP_FEATURE_FOD_CALIBRATION_HBM:
		if (ctl->feature_val == -1) {
			DISP_INFO("FOD calibration HBM restore last_bl_level=%d\n",
					mi_cfg->last_bl_level);
			mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_HBM_FOD_OFF,
					mi_cfg->last_bl_level);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD_OFF);
			mi_cfg->dimming_state = STATE_DIM_RESTORE;
			mi_cfg->in_fod_calibration = false;
		} else {
			mi_cfg->in_fod_calibration = true;
			mi_cfg->dimming_state = STATE_DIM_BLOCK;
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD_ON);
		}
		mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_HBM] = ctl->feature_val;
		break;
	case DISP_FEATURE_FLAT_MODE:
		if (!mi_cfg->flat_sync_te) {
			if (ctl->feature_val == FEATURE_ON) {
				DISP_INFO("flat mode on\n");

				if (mi_get_panel_id_by_dsi_panel(panel) == N2_PANEL_PA || mi_get_panel_id_by_dsi_panel(panel) == N1_PANEL_PA ||
					mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_PA || mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_SA)  
					mi_dsi_update_timing_switch_and_flat_mode_cmd(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);
				else if (mi_get_panel_id_by_dsi_panel(panel) == N3_PANEL_PA &&
					panel->id_config.build_id >= N3_PANEL_PA_P11)
					mi_dsi_update_flat_mode_on_cmd(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);

				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);
				rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_SEC_ON);
			}
			else {
				DISP_INFO("flat mode off\n");
				if (mi_get_panel_id_by_dsi_panel(panel) == N2_PANEL_PA ||
					mi_get_panel_id_by_dsi_panel(panel) == N1_PANEL_PA)
					mi_dsi_update_timing_switch_and_flat_mode_cmd(panel, DSI_CMD_SET_MI_FLAT_MODE_OFF);

				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_OFF);
				rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_SEC_OFF);
			}
			mi_disp_feature_event_notify_by_type(mi_get_disp_id(panel->type),
				MI_DISP_EVENT_FLAT_MODE, sizeof(ctl->feature_val), ctl->feature_val);
		} else {
			rc = mi_dsi_send_flat_sync_with_te_locked(panel,
					ctl->feature_val == FEATURE_ON);
		}
		mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE] = ctl->feature_val;
		break;
	case DISP_FEATURE_CRC:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_DC:
		DISP_INFO("DC mode state:%d\n", ctl->feature_val);
		if (mi_cfg->dc_feature_enable) {
			rc = mi_dsi_panel_set_dc_mode_locked(panel, ctl->feature_val == FEATURE_ON);
		}
		mi_cfg->feature_val[DISP_FEATURE_DC] = ctl->feature_val;
		mi_disp_feature_event_notify_by_type(mi_get_disp_id(panel->type),
				MI_DISP_EVENT_DC, sizeof(ctl->feature_val), ctl->feature_val);
		break;
	case DISP_FEATURE_LOCAL_HBM:
		rc = mi_dsi_panel_set_lhbm_fod_locked(panel, ctl);
		mi_cfg->feature_val[DISP_FEATURE_LOCAL_HBM] = ctl->feature_val;
		break;
	case DISP_FEATURE_SENSOR_LUX:
		DISP_DEBUG("DISP_FEATURE_SENSOR_LUX=%d\n", ctl->feature_val);
		mi_cfg->feature_val[DISP_FEATURE_SENSOR_LUX] = ctl->feature_val;
		break;
	case DISP_FEATURE_FP_STATUS:
		mi_cfg->feature_val[DISP_FEATURE_FP_STATUS] = ctl->feature_val;
		mi_disp_feature_event_notify_by_type(mi_get_disp_id(panel->type),
				MI_DISP_EVENT_FP, sizeof(ctl->feature_val), ctl->feature_val);
		if (mi_get_panel_id_by_dsi_panel(panel) == O11_PANEL_PA &&
			(panel->power_mode == SDE_MODE_DPMS_LP1 || panel->power_mode == SDE_MODE_DPMS_LP2) &&
			ctl->feature_val == AUTH_STOP && mi_cfg->last_no_zero_bl_level > 0 &&
			mi_cfg->last_no_zero_bl_level < mi_cfg->doze_hbm_dbv_level) {
			dsi_panel_update_backlight(panel, mi_cfg->last_no_zero_bl_level);
			DISP_INFO("update backlight %d in aod status", mi_cfg->last_no_zero_bl_level);
		}
		break;
	case DISP_FEATURE_FOLD_STATUS:
		DISP_INFO("DISP_FEATURE_FOLD_STATUS=%d\n", ctl->feature_val);
		mi_cfg->feature_val[DISP_FEATURE_FOLD_STATUS] = ctl->feature_val;
		break;
	case DISP_FEATURE_NATURE_FLAT_MODE:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_SPR_RENDER:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_AOD_TO_NORMAL:
		rc = mi_dsi_panel_aod_to_normal_optimize_locked(panel,
				ctl->feature_val == FEATURE_ON);
		mi_cfg->feature_val[DISP_FEATURE_AOD_TO_NORMAL] = ctl->feature_val;
		break;
	case DISP_FEATURE_COLOR_INVERT:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_DC_BACKLIGHT:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_GIR:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_DBI:
		if (mi_get_panel_id_by_dsi_panel(panel) == N11U_PANEL_PA ||
				mi_get_panel_id_by_dsi_panel(panel) == N9_PANEL_PA ||
				mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_PA ||
				mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_SA) {
			DISP_INFO("by temp: %d, bl_lvl: %x\n", ctl->feature_val,
						panel->mi_cfg.last_bl_level);
			mi_cfg->feature_val[DISP_FEATURE_DBI] = ctl->feature_val;
			if (panel->mi_cfg.last_bl_level && !is_aod_and_panel_initialized(panel)) {
				dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
			}

		} else if (mi_get_panel_id_by_dsi_panel(panel) == N2_PANEL_PA ||
				mi_get_panel_id_by_dsi_panel(panel) == N1_PANEL_PA) {
			DISP_INFO("by temp: %d, bl_lvl: %x\n", ctl->feature_val,
						panel->mi_cfg.last_bl_level);
			mi_cfg->feature_val[DISP_FEATURE_DBI] = ctl->feature_val;
			if (panel->mi_cfg.last_bl_level < 0x84 && panel->mi_cfg.last_bl_level
			&& !is_aod_and_panel_initialized(panel)) {
				dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
			}
		} else if (mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PA ||
					mi_get_panel_id_by_dsi_panel(panel) == O16U_PANEL_PA) {
			DISP_INFO("by temp gray_level:%d\n", ctl->feature_val);
			mi_dsi_panel_set_dbi_3dlut_by_temp_locked(panel, ctl->feature_val);
			mi_cfg->feature_val[DISP_FEATURE_DBI] = ctl->feature_val;
		} else if (mi_get_panel_id_by_dsi_panel(panel) == O11_PANEL_PA) {
			mi_dsi_panel_set_csc_by_temp_locked(panel, ctl->feature_val);
			mi_cfg->feature_val[DISP_FEATURE_DBI] = ctl->feature_val;
		} else {
			DISP_INFO("by temp gray_level:%d\n", ctl->feature_val);
			if (mi_cfg->feature_val[DISP_FEATURE_DBI] == ctl->feature_val) {
				DISP_INFO("gray_level is the same, return\n");
				break;
			}
			mi_dsi_panel_set_dbi_by_temp_locked(panel, ctl->feature_val);
			mi_cfg->feature_val[DISP_FEATURE_DBI] = ctl->feature_val;
		}
		break;
	case DISP_FEATURE_DDIC_ROUND_CORNER:
		DISP_INFO("DDIC round corner state:%d\n", ctl->feature_val);
		rc = mi_dsi_panel_set_round_corner_locked(panel, ctl->feature_val == FEATURE_ON);
		mi_cfg->feature_val[DISP_FEATURE_DDIC_ROUND_CORNER] = ctl->feature_val;
		break;
	case DISP_FEATURE_HBM_BACKLIGHT:
		DISP_INFO("hbm backlight:%d\n", ctl->feature_val);
		panel->mi_cfg.last_bl_level = ctl->feature_val;
		dsi_panel_update_backlight(panel, panel->mi_cfg.last_bl_level);
		break;
	case DISP_FEATURE_BACKLIGHT:
		DISP_INFO("backlight:%d\n", ctl->feature_val);
		panel->mi_cfg.last_bl_level = ctl->feature_val;
		dsi_panel_update_backlight(panel, panel->mi_cfg.last_bl_level);
		break;
	case DISP_FEATURE_CABC:
		DISP_INFO("lcd panel cabc feature_val: %d\n", ctl->feature_val );
		if (ctl->feature_val == LCD_CABC_OFF) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_CABC_OFF);
			DISP_INFO("lcd cabc off\n");
		}
		if (ctl->feature_val == LCD_CABC_UI_ON) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_CABC_UI_ON);
			DISP_INFO("lcd cabc on\n");
		}
		if (ctl->feature_val == LCD_CABC_MOVIE_ON) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_CABC_MOVIE_ON);
			DISP_INFO("lcd cabc movie on\n");
		}
		if (ctl->feature_val == LCD_CABC_STILL_ON) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_CABC_STILL_ON);
			DISP_INFO("lcd cabc still on\n");
		}
		break;
	default:
		DISP_ERROR("invalid feature argument: %d\n", ctl->feature_id);
		break;
	}
exit:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int mi_dsi_panel_get_disp_param(struct dsi_panel *panel,
			struct disp_feature_ctl *ctl)
{
	struct mi_dsi_panel_cfg *mi_cfg;
	int i = 0;

	if (!panel || !ctl) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (!is_support_disp_feature_id(ctl->feature_id)) {
		DISP_ERROR("unsupported disp feature id\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	mi_cfg = &panel->mi_cfg;
	for (i = DISP_FEATURE_DIMMING; i < DISP_FEATURE_MAX; i++) {
		if (i == ctl->feature_id) {
			ctl->feature_val =  mi_cfg->feature_val[i];
			DISP_INFO("%s: %d\n", get_disp_feature_id_name(ctl->feature_id),
				ctl->feature_val);
		}
	}
	mutex_unlock(&panel->panel_lock);

	return 0;
}

ssize_t mi_dsi_panel_show_disp_param(struct dsi_panel *panel,
			char *buf, size_t size)
{
	struct mi_dsi_panel_cfg *mi_cfg;
	ssize_t count = 0;
	int i = 0;

	if (!panel || !buf || !size) {
		DISP_ERROR("invalid params\n");
		return -EAGAIN;
	}

	count = snprintf(buf, size, "%040s: feature vaule\n", "feature name[feature id]");

	mutex_lock(&panel->panel_lock);
	mi_cfg = &panel->mi_cfg;
	for (i = DISP_FEATURE_DIMMING; i < DISP_FEATURE_MAX; i++) {
		count += snprintf(buf + count, size - count, "%036s[%02d]: %d\n",
				get_disp_feature_id_name(i), i, mi_cfg->feature_val[i]);

	}
	mutex_unlock(&panel->panel_lock);

	return count;
}

int dsi_panel_parse_build_id_read_config(struct dsi_panel *panel)
{
	struct drm_panel_build_id_config *id_config;
	struct dsi_parser_utils *utils = &panel->utils;
	int rc = 0;

	if (!panel) {
		DISP_ERROR("Invalid Params\n");
		return -EINVAL;
	}

	id_config = &panel->id_config;
	id_config->build_id = 0;
	if (!id_config)
		return -EINVAL;

	rc = utils->read_u32(utils->data, "mi,panel-build-id-read-length",
                                &id_config->id_cmds_rlen);
	if (rc) {
		id_config->id_cmds_rlen = 0;
		return -EINVAL;
	}

	dsi_panel_parse_cmd_sets_sub(&id_config->id_cmd,
			DSI_CMD_SET_MI_PANEL_BUILD_ID, utils);
	if (!(id_config->id_cmd.count)) {
		DISP_ERROR("panel build id read command parsing failed\n");
		return -EINVAL;
	}
	return 0;
}

int dsi_panel_send_em_cycle_setting(struct dsi_panel *panel, u32 bl_lvl, bool no_backlight)
{
	int rc = 0;
	struct drm_panel_build_id_config *config;
	struct mi_dsi_panel_cfg *mi_cfg;
	ktime_t start_ktime;
	s64 elapsed_us;

	if (!panel)
		return -EINVAL;

	if (mi_get_panel_id_by_dsi_panel(panel) != N11U_PANEL_PA)
		return 0;

	start_ktime = ktime_get();

	mi_cfg = &panel->mi_cfg;
	config = &(panel->id_config);

	if (!config->build_id || bl_lvl == 0) {
		return 0;
	} else {
		if (mi_cfg->last_bl_level == 0 && bl_lvl != 0) {
			if (bl_lvl <= mi_cfg->panel_3840pwm_dbv_threshold) {
				mi_cfg->is_em_cycle_32_pulse = false;
			} else {
				mi_cfg->is_em_cycle_32_pulse = true;
			}
		}
		if (bl_lvl <= mi_cfg->panel_3840pwm_dbv_threshold && !mi_cfg->is_em_cycle_32_pulse) {
			if (no_backlight) {
				rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_32PULSE_NO51);
			} else {
				rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_32PULSE);
			}
			mi_cfg->is_em_cycle_32_pulse = true;
			if (rc) {
				DISP_ERROR("[%s] DSI_CMD_SET_MI_32PULSE rc = %d, bl_lvl[%d]\n",
						panel->name, rc, bl_lvl);
				return rc;
			} else {
				DSI_INFO("[%s] DSI_CMD_SET_MI_32PULSE rc = %d, bl_lvl[%d],32_pulse: %d\n",
						panel->name, rc, bl_lvl, mi_cfg->is_em_cycle_32_pulse);
			}
		} else if (bl_lvl > mi_cfg->panel_3840pwm_dbv_threshold && mi_cfg->is_em_cycle_32_pulse) {
			mi_cfg->is_em_cycle_32_pulse = false;
			if (no_backlight) {
				rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_16PULSE_NO51);
			} else {
				rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_16PULSE);
			}
			if (rc) {
				DISP_ERROR("[%s] DSI_CMD_SET_MI_16PULSE rc = %d, bl_lvl[%d]\n",
						panel->name, rc, bl_lvl);
				return rc;
			} else {
				DSI_INFO("[%s] DSI_CMD_SET_MI_16PULSE rc = %d, bl_lvl[%d],16_pulse: %d\n",
						panel->name, rc, bl_lvl, mi_cfg->is_em_cycle_32_pulse);
			}
		}
	}

	elapsed_us = ktime_us_delta(ktime_get(), start_ktime);
	DISP_DEBUG("elapsed time - %d.%d(ms)\n", (int)(elapsed_us / 1000), (int)(elapsed_us % 1000));
	return rc;
}

int dsi_panel_parse_mura_reg_read_config(struct dsi_panel *panel)
{
	struct drm_panel_mura_info_config *mura_config;
	struct dsi_parser_utils *utils = &panel->utils;

	if (!panel) {
		DISP_ERROR("Invalid Params\n");
		return -EINVAL;
	}

	mura_config = &(panel->mura_info_config);
	if (!mura_config)
		return -EINVAL;
	mura_config->ischecked = false;

	dsi_panel_parse_cmd_sets_sub(&mura_config->batch_cmd,
			DSI_CMD_SET_MI_PANEL_MURA_BATCH_WRITE, utils);
	if (!mura_config->batch_cmd.count) {
		DISP_ERROR("mura_config batch_cmd command parsing failed\n");
		return -EINVAL;
	}

	dsi_panel_parse_cmd_sets_sub(&mura_config->offset_cmd,
			DSI_CMD_SET_MI_PANEL_MURA_OFFSET_WRITE, utils);
	if (!mura_config->offset_cmd.count) {
		DISP_ERROR("mura_config offset_cmd command parsing failed\n");
		return -EINVAL;
	}

	dsi_panel_parse_cmd_sets_sub(&mura_config->checksum_cmd,
			DSI_CMD_SET_MI_PANEL_MURA_CHECKSUM_WRITE, utils);
	if (!mura_config->checksum_cmd.count){
		DISP_INFO("mura_config checksum_cmd command parsing failed\n");
		return -EINVAL;
	}

	dsi_panel_parse_cmd_sets_sub(&mura_config->batch_read_cmd,
			DSI_CMD_SET_MI_PANEL_MURA_BATCH_READ, utils);
	if (!mura_config->batch_read_cmd.count) {
		DISP_INFO("mura_config checksum_cmd command parsing failed\n");
		return -EINVAL;
	}

	dsi_panel_parse_cmd_sets_sub(&mura_config->offset_read_cmd,
			DSI_CMD_SET_MI_PANEL_MURA_OFFSET_READ, utils);
	if (!mura_config->offset_read_cmd.count) {
		DISP_INFO("mura_config checksum_cmd command parsing failed\n");
		return -EINVAL;
	}

	dsi_panel_parse_cmd_sets_sub(&mura_config->checksum_read_cmd,
			DSI_CMD_SET_MI_PANEL_MURA_CHECKSUM_READ, utils);
	if (!mura_config->checksum_read_cmd.count){
		DISP_INFO("mura_config checksum_cmd command parsing failed\n");
		return -EINVAL;
	}

	return 0;
}

int dsi_panel_parse_wp_reg_read_config(struct dsi_panel *panel)
{
	struct drm_panel_wp_config *wp_config;
	struct dsi_parser_utils *utils = &panel->utils;
	int rc = 0;

	if (!panel) {
		DISP_ERROR("Invalid Params\n");
		return -EINVAL;
	}

	wp_config = &panel->wp_config;
	if (!wp_config)
		return -EINVAL;

	dsi_panel_parse_cmd_sets_sub(&wp_config->wp_cmd,
			DSI_CMD_SET_MI_PANEL_WP_READ, utils);
	if (!wp_config->wp_cmd.count) {
		DISP_ERROR("wp_info read command parsing failed\n");
		return -EINVAL;
	}

	dsi_panel_parse_cmd_sets_sub(&wp_config->pre_tx_cmd,
			DSI_CMD_SET_MI_PANEL_WP_READ_PRE_TX, utils);
	if (!wp_config->pre_tx_cmd.count)
		DISP_INFO("wp_info pre command parsing failed\n");

	rc = utils->read_u32(utils->data, "mi,mdss-dsi-panel-wp-read-length",
				&wp_config->wp_cmds_rlen);
	if (rc || !wp_config->wp_cmds_rlen) {
		wp_config->wp_cmds_rlen = 0;
		return -EINVAL;
	}

	rc = utils->read_u32(utils->data, "mi,mdss-dsi-panel-wp-read-index",
				&wp_config->wp_read_info_index);
	if (rc || !wp_config->wp_read_info_index) {
		wp_config->wp_read_info_index = 0;
	}

	wp_config->return_buf = kcalloc(wp_config->wp_cmds_rlen,
		sizeof(unsigned char), GFP_KERNEL);
	if (!wp_config->return_buf) {
		return -ENOMEM;
	}
	return 0;
}

int dsi_panel_parse_cell_id_read_config(struct dsi_panel *panel)
{
	struct drm_panel_cell_id_config *cell_id_config;
	struct dsi_parser_utils *utils = &panel->utils;
	int rc = 0;

	if (!panel) {
		DISP_ERROR("Invalid Params\n");
		return -EINVAL;
	}

	cell_id_config = &panel->cell_id_config;
	if (!cell_id_config)
		return -EINVAL;

	dsi_panel_parse_cmd_sets_sub(&cell_id_config->cell_id_cmd,
			DSI_CMD_SET_MI_PANEL_CELL_ID_READ, utils);
	if (!cell_id_config->cell_id_cmd.count) {
		DISP_ERROR("cell_id_info read command parsing failed\n");
		return -EINVAL;
	}

	dsi_panel_parse_cmd_sets_sub(&cell_id_config->pre_tx_cmd,
			DSI_CMD_SET_MI_PANEL_CELL_ID_READ_PRE_TX, utils);
	if (!cell_id_config->pre_tx_cmd.count)
		DISP_INFO("cell_id_info pre command parsing failed\n");

	dsi_panel_parse_cmd_sets_sub(&cell_id_config->after_tx_cmd,
			DSI_CMD_SET_MI_PANEL_CELL_ID_READ_AFTER_TX, utils);
	if (!cell_id_config->after_tx_cmd.count)
		DISP_INFO("cell_id_info after command parsing failed\n");

	rc = utils->read_u32(utils->data, "mi,mdss-dsi-panel-cell-id-read-length",
				&cell_id_config->cell_id_cmds_rlen);
	if (rc || !cell_id_config->cell_id_cmds_rlen) {
		cell_id_config->cell_id_cmds_rlen = 0;
		return -EINVAL;
	}

	rc = utils->read_u32(utils->data, "mi,mdss-dsi-panel-cell-id-read-index",
				&cell_id_config->cell_id_read_info_index);
	if (rc || !cell_id_config->cell_id_read_info_index) {
		cell_id_config->cell_id_read_info_index = 0;
	}

	cell_id_config->return_buf = kcalloc(cell_id_config->cell_id_cmds_rlen,
		sizeof(unsigned char), GFP_KERNEL);
	if (!cell_id_config->return_buf) {
		return -ENOMEM;
	}
	return 0;
}

static inline void mi_dsi_panel_lgs_cie_by_temperature(enum dsi_cmd_set_type *dbi_bwg_type, int bl_lvl, int temp)
{
	if (temp >= 36100) {
		if (0x84 > bl_lvl && bl_lvl >= 0x4C) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_41_MODE;
		} else if (0x4C > bl_lvl && bl_lvl >= 0x28) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_42_MODE;
		} else if (0x28 > bl_lvl && bl_lvl >= 0x19) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_43_MODE;
		}
	}else if ( 36100 > temp && temp >= 32100) {
		if (0x84 > bl_lvl && bl_lvl >= 0x4C) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_44_MODE;
		} else if (0x4C > bl_lvl && bl_lvl >= 0x28) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_45_MODE;
		} else if (0x28 > bl_lvl && bl_lvl >= 0x19) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_46_MODE;
		}
	} else if (32100 > temp && temp >= 28100) {
		if (0x84 > bl_lvl && bl_lvl >= 0x4C) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_47_MODE;
		} else if (0x4C > bl_lvl && bl_lvl >= 0x28) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_48_MODE;
		} else if (0x28 > bl_lvl && bl_lvl >= 0x19) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_49_MODE;
		}
	}
}

int mi_dsi_panel_vrr_set_by_dbv(struct dsi_panel *panel, u32 bl_lvl)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg;
	enum dsi_cmd_set_type skip_source_type, dbi_bwg_type;
	int temp = 0;

	if (!panel)
		return -EINVAL;

	if (mi_get_panel_id_by_dsi_panel(panel) != N2_PANEL_PA && mi_get_panel_id_by_dsi_panel(panel) != N1_PANEL_PA)
		return 0;

	mi_cfg = &panel->mi_cfg;

	skip_source_type = dbi_bwg_type = DSI_CMD_SET_MAX;
	if (bl_lvl == 0) {
		mi_cfg->skip_source_type = DSI_CMD_SET_MAX;
		mi_cfg->dbi_bwg_type = DSI_CMD_SET_MAX;
		return -EINVAL;
	}

	if (bl_lvl  >= 0x7FF) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_500_1400NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_500_1400NIT_MODE;
	} else if (0x7FF > bl_lvl && bl_lvl >= 0x333) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_200_500NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_200_500NIT_MODE;
	} else if (0x333 > bl_lvl && bl_lvl >= 0x1C2) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_110_200NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_110_200NIT_MODE;
	} else if (0x1C2 > bl_lvl && bl_lvl >= 0x147) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_80_110NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_80_110NIT_MODE;
	} else if (0x147 > bl_lvl && bl_lvl >= 0x12B) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_72_79NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_72_79NIT_MODE;
	} else if (0x12B > bl_lvl && bl_lvl >= 0x110) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_65_72NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_65_72NIT_MODE;
	} else if (0x110 > bl_lvl && bl_lvl >= 0xF5) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_60_65NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_60_65NIT_MODE;
	} else if (0xF5 > bl_lvl && bl_lvl >= 0xDC) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_53_60NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_53_60NIT_MODE;
	} else if (0xDC > bl_lvl && bl_lvl >= 0x84) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_30_53NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_30_53NIT_MODE;
	} else if ( 0x84 > bl_lvl && bl_lvl && panel->id_config.build_id == N2_PANEL_PA_P11_02 ) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_30_53NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_0_30NIT_MODE;
	} else if (0x84 > bl_lvl && bl_lvl >= 0x4C && panel->id_config.build_id > N2_PANEL_PA_P11_02) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_19_30NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_19_30NIT_MODE;
		temp = mi_cfg->feature_val[DISP_FEATURE_DBI];
		mi_dsi_panel_lgs_cie_by_temperature(&dbi_bwg_type, bl_lvl, temp);
	} else if (0x4C > bl_lvl && bl_lvl >= 0x28&& panel->id_config.build_id > N2_PANEL_PA_P11_02) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_10_19NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_10_19NIT_MODE;
		temp = mi_cfg->feature_val[DISP_FEATURE_DBI];
		mi_dsi_panel_lgs_cie_by_temperature(&dbi_bwg_type, bl_lvl, temp);
	} else if (0x28 > bl_lvl && bl_lvl >= 0x19&& panel->id_config.build_id > N2_PANEL_PA_P11_02) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_6_10NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_6_10NIT_MODE;
		temp = mi_cfg->feature_val[DISP_FEATURE_DBI];
		mi_dsi_panel_lgs_cie_by_temperature(&dbi_bwg_type, bl_lvl, temp);
	}

	if ((mi_cfg->skip_source_type != skip_source_type) && (skip_source_type != DSI_CMD_SET_MAX)) {
		rc = dsi_panel_tx_cmd_set(panel, skip_source_type);
		if (rc) {
			DISP_ERROR("(%s) send  cmds(%s) failed! bl_lvl(%d),rc(%d)\n",
					panel->name, cmd_set_prop_map[skip_source_type], bl_lvl, rc);
			return rc;
		}
		mi_cfg->skip_source_type = skip_source_type;
		DISP_DEBUG("Send SKip Source(%s), temp = %d, bl_lvl = 0x%x\n",
				cmd_set_prop_map[skip_source_type], temp, bl_lvl);
	}

	if ((mi_cfg->dbi_bwg_type != dbi_bwg_type) && (dbi_bwg_type != DSI_CMD_SET_MAX)) {
		rc = dsi_panel_tx_cmd_set(panel, dbi_bwg_type);
		if (rc) {
			DISP_ERROR("(%s) send  cmds(%s) failed! bl_lvl(%d),rc(%d)\n",
					panel->name, cmd_set_prop_map[dbi_bwg_type], bl_lvl, rc);
			return rc;
		}
		mi_cfg->dbi_bwg_type = dbi_bwg_type;
		DISP_DEBUG("Send DBI BWG(%s), temp = %d, bl_lvl = 0x%x\n",
				cmd_set_prop_map[dbi_bwg_type], temp, bl_lvl);
	}

	return rc;
}

int mi_dsi_panel_set_nolp_locked(struct dsi_panel *panel)
{
	int rc = 0;
	u32 doze_brightness = panel->mi_cfg.doze_brightness;
	int update_bl = 0;

	if (panel->mi_cfg.panel_state == PANEL_STATE_ON) {
		DISP_INFO("panel already PANEL_STATE_ON, skip nolp");
		if ((mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N8_PANEL_PA) ||
			(mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N8_PANEL_SA) ||
			(mi_get_panel_id(panel->mi_cfg.mi_panel_id) == O11_PANEL_PA) ||
			(mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N18_PANEL_PA) ||
			(mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N18_PANEL_SA)) {
				panel->mi_cfg.aod_status = false;
		}
		return rc;
	}

	if (doze_brightness == DOZE_TO_NORMAL)
		doze_brightness = panel->mi_cfg.last_doze_brightness;

	switch (doze_brightness) {
		case DOZE_BRIGHTNESS_HBM:
			DISP_INFO("set doze_hbm_dbv_level in nolp");
			update_bl = panel->mi_cfg.doze_hbm_dbv_level;
			break;
		case DOZE_BRIGHTNESS_LBM:
			DISP_INFO("set doze_lbm_dbv_level in nolp");
			update_bl = panel->mi_cfg.doze_lbm_dbv_level;
			break;
		default:
			break;
	}

	if ((mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N8_PANEL_PA) ||
		(mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N8_PANEL_SA) ||
		(mi_get_panel_id(panel->mi_cfg.mi_panel_id) == O11_PANEL_PA) ||
		(mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N18_PANEL_PA) ||
		(mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N18_PANEL_SA)) {
		if(panel->mi_cfg.aod_status) {
			update_bl = panel->mi_cfg.last_bl_level;
			panel->mi_cfg.aod_status = false;
		}
	}

	mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_NOLP, update_bl);

	if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N3_PANEL_PA)
		mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_NOLP_UPDATE, DSI_CMD_SET_NOLP);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NOLP);

	if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N18_PANEL_PA ||
	        mi_get_panel_id(panel->mi_cfg.mi_panel_id) == O11_PANEL_PA)
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_TIMING_SWITCH);

	panel->power_mode = SDE_MODE_DPMS_ON;

	return rc;
}

int mi_dsi_panel_set_dbi_by_temp_bl(struct dsi_panel *panel, u32 bl_lvl)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg;
	enum dsi_cmd_set_type dbi_bwg_type;
	int temp = 0;
	int cur_framerate = 0;

	if (!panel) {
		return -EINVAL;
	}

	if (mi_get_panel_id_by_dsi_panel(panel) != N11U_PANEL_PA) {
		return 0;
	}

	mi_cfg = &panel->mi_cfg;
	temp = mi_cfg->feature_val[DISP_FEATURE_DBI];
	cur_framerate = panel->cur_mode->timing.refresh_rate;

	dbi_bwg_type = DSI_CMD_SET_MAX;
	if (bl_lvl == 0) {
		mi_cfg->dbi_bwg_type = DSI_CMD_SET_MAX;
		return -EINVAL;
	}

	if (temp == 45) {
		if (0x31 > bl_lvl && bl_lvl >= 0x19) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP45_BL19_MODE;
		} else if (0x62 > bl_lvl && bl_lvl >= 0x31) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP45_BL31_MODE;
		} else if (0xC0 > bl_lvl && bl_lvl >= 0x62) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP45_BL62_MODE;
		} else if (0x146 >= bl_lvl && bl_lvl >= 0xC0) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP45_BLC0_MODE;
		} else if (bl_lvl > 0x146){
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP45_BL147_MODE;
		}
	} else if (temp == 35) {
		if (0x31 > bl_lvl && bl_lvl >= 0x19) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP35_BL19_MODE;
		} else if (0x62 > bl_lvl && bl_lvl >= 0x31) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP35_BL31_MODE;
		} else if (0xC0 > bl_lvl && bl_lvl >= 0x62) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP35_BL62_MODE;
		} else if (0x146 >= bl_lvl && bl_lvl >= 0xC0) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP35_BLC0_MODE;
		} else if (bl_lvl > 0x146){
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP35_BL147_MODE;
		}
	} else if (temp == 30) {
		if (0x31 > bl_lvl && bl_lvl >= 0x19) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP30_BL19_MODE;
		} else if (0x62 > bl_lvl && bl_lvl >= 0x31) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP30_BL31_MODE;
		} else if (0xC0 > bl_lvl && bl_lvl >= 0x62) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP30_BL62_MODE;
		} else if (0x146 >= bl_lvl && bl_lvl >= 0xC0) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP30_BLC0_MODE;
		} else if (bl_lvl > 0x146){
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP30_BL147_MODE;
		}
	} else {
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_0_28TEMP_MODE;
	}

	if ((dbi_bwg_type != mi_cfg->dbi_bwg_type) && (dbi_bwg_type != DSI_CMD_SET_MAX)) {
		rc = dsi_panel_tx_cmd_set(panel, dbi_bwg_type);
  		if (rc) {
  			DISP_ERROR("(%s) send  cmds(%s) failed! bl_lvl(%d),rc(%d)\n",
  					panel->name, cmd_set_prop_map[dbi_bwg_type], bl_lvl, rc);
  			return rc;
  		}
  		mi_cfg->dbi_bwg_type = dbi_bwg_type;
		DISP_DEBUG("Send DBI BWG(%s), temp = %d, bl_lvl = 0x%x, cur_fps = %d\n",
				cmd_set_prop_map[dbi_bwg_type], temp, bl_lvl, cur_framerate);
  	}
 
	return rc;
}

int mi_dsi_panel_set_dbi_by_temp_bl_N9(struct dsi_panel *panel, u32 bl_lvl)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg;
	enum dsi_cmd_set_type dbi_bwg_type;
	int temp = 0;
	int cur_framerate = 0;

	if (!panel) {
		return -EINVAL;
	}

	if (mi_get_panel_id_by_dsi_panel(panel) != N9_PANEL_PA || panel->id_config.build_id < N9_PANEL_PA_P2 || is_hbm_fod_on(panel)) {
		return 0;
	}

	mi_cfg = &panel->mi_cfg;
	temp = mi_cfg->feature_val[DISP_FEATURE_DBI];
	cur_framerate = panel->cur_mode->timing.refresh_rate;

	dbi_bwg_type = DSI_CMD_SET_MAX;
	if (bl_lvl == 0) {
		mi_cfg->dbi_bwg_type = DSI_CMD_SET_MAX;
		return -EINVAL;
	}

	if (temp == 36) {
		if (0x30 > bl_lvl && bl_lvl >= 0x0F) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP36_BL0F_MODE;
		} else if (0x7B > bl_lvl && bl_lvl >= 0x30) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP36_BL30_MODE;
		} else if (0x12A > bl_lvl && bl_lvl >= 0x7B) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP36_BL7B_MODE;
		} else if (0x255 >= bl_lvl && bl_lvl >= 0x12A) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP36_BL12A_MODE;
		} else if (0x51F >= bl_lvl && bl_lvl >= 255) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP36_BL255_MODE;
		} else if (bl_lvl > 0x51F) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP36_BL51F_MODE;
		}
	} else if (temp == 32) {
		if (0x30 > bl_lvl && bl_lvl >= 0x0F) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP32_BL0F_MODE;
		} else if (0x7B > bl_lvl && bl_lvl >= 0x30) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP32_BL30_MODE;
		} else if (0x12A > bl_lvl && bl_lvl >= 0x7B) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP32_BL7B_MODE;
		} else if (0x255 >= bl_lvl && bl_lvl >= 0x12A) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP32_BL12A_MODE;
		} else if (0x51F >= bl_lvl && bl_lvl >= 255) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP32_BL255_MODE;
		} else if (bl_lvl > 0x51F) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP32_BL51F_MODE;
		}
	} else if (temp == 28) {
		if (0x30 > bl_lvl && bl_lvl >= 0x0F) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP28_BL0F_MODE;
		} else if (0x7B > bl_lvl && bl_lvl >= 0x30) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP28_BL30_MODE;
		} else if (0x12A > bl_lvl && bl_lvl >= 0x7B) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP28_BL7B_MODE;
		} else if (0x255 >= bl_lvl && bl_lvl >= 0x12A) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP28_BL12A_MODE;
		} else if (0x51F >= bl_lvl && bl_lvl >= 255) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP28_BL255_MODE;
		} else if (bl_lvl > 0x51F) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP28_BL51F_MODE;
		}
	} else if (temp == 25) {
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP20_28_MODE;
	}

	if ((dbi_bwg_type != mi_cfg->dbi_bwg_type) && (dbi_bwg_type != DSI_CMD_SET_MAX)) {
		rc = dsi_panel_tx_cmd_set(panel, dbi_bwg_type);
		if (rc)
		{
			DISP_ERROR("(%s) send  cmds(%s) failed! bl_lvl(%d),rc(%d)\n",
					   panel->name, cmd_set_prop_map[dbi_bwg_type], bl_lvl, rc);
			return rc;
		}
		mi_cfg->dbi_bwg_type = dbi_bwg_type;
	}

	return rc;
}

int mi_dsi_panel_set_dbi_by_temp_bl_N8(struct dsi_panel *panel, u32 bl_lvl)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg;
	enum dsi_cmd_set_type dbi_bwg_type;
	int temp = 0;
	int cur_framerate = 0;

	if (!panel) {
		return -EINVAL;
	}

	if (mi_get_panel_id_by_dsi_panel(panel) != N8_PANEL_PA && mi_get_panel_id_by_dsi_panel(panel) != N8_PANEL_SA) {
		return 0;
	}

	mi_cfg = &panel->mi_cfg;
	temp = mi_cfg->feature_val[DISP_FEATURE_DBI];
	cur_framerate = panel->cur_mode->timing.refresh_rate;

	dbi_bwg_type = DSI_CMD_SET_MAX;
	if (bl_lvl == 0) {
		mi_cfg->dbi_bwg_type = DSI_CMD_SET_MAX;
		return -EINVAL;
	}

	if (mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_PA) {
		if (temp == 0) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMPLW30_BL_MODE;
		}else if (temp == 30) {
			if (0x30 > bl_lvl && bl_lvl >= 0x0F) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP30_BL0F_MODE;
			} else if (0x63 > bl_lvl && bl_lvl >= 0x30) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP30_BL30_MODE;
			} else if (0xB6 > bl_lvl && bl_lvl >= 0x63) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP30_BL63_MODE;
			} else if (0x1E2 > bl_lvl && bl_lvl >= 0xB6) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP30_BLB6_MODE;
			} else if (0x3D7 >= bl_lvl && bl_lvl >= 0x1E2) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP30_BL1E2_MODE;
			} else if (bl_lvl == 0x3D7) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP30_BLEQ3D7_MODE;
			}else if (bl_lvl > 0x3D7) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP30_BL3D7_MODE;
			}
		} else if (temp == 35) {
			if (0x30 > bl_lvl && bl_lvl >= 0x0F) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP35_BL0F_MODE;
			} else if (0x63 > bl_lvl && bl_lvl >= 0x30) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP35_BL30_MODE;
			} else if (0xB6 > bl_lvl && bl_lvl >= 0x63) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP35_BL63_MODE;
			} else if (0x1E2 > bl_lvl && bl_lvl >= 0xB6) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP35_BLB6_MODE;
			} else if (0x3D7 >= bl_lvl && bl_lvl >= 0x1E2) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP35_BL1E2_MODE;
			} else if (bl_lvl == 0x3D7) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP35_BLEQ3D7_MODE;
			}else if (bl_lvl > 0x3D7) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP35_BL3D7_MODE;
			}
		} else if (temp == 45) {
			if (0x30 > bl_lvl && bl_lvl >= 0x0F) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP45_BL0F_MODE;
			} else if (0x63 > bl_lvl && bl_lvl >= 0x30) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP45_BL30_MODE;
			} else if (0xB6 > bl_lvl && bl_lvl >= 0x63) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP45_BL63_MODE;
			} else if (0x1E2 > bl_lvl && bl_lvl >= 0xB6) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP45_BLB6_MODE;
			} else if (0x3D7 >= bl_lvl && bl_lvl >= 0x1E2) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP45_BL1E2_MODE;
			} else if (bl_lvl == 0x3D7) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP45_BLEQ3D7_MODE;
			}else if (bl_lvl > 0x3D7) {
				dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_TEMP45_BL3D7_MODE;
			}
		}
	}

	if (mi_get_panel_id_by_dsi_panel(panel) == N8_PANEL_SA && panel->id_config.build_id >= N8_PANEL_SA_P11) {

		if (temp == 0){
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_OFF_MODE;
		} else if (temp == TEMP_INDEX_30){
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_30_MODE;
		} else if (temp == TEMP_INDEX_35){
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_35_MODE;
		} else if (temp == TEMP_INDEX_45){
			dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_45_MODE;
		}
	}


	if ((dbi_bwg_type != mi_cfg->dbi_bwg_type) && (dbi_bwg_type != DSI_CMD_SET_MAX)) {
		rc = dsi_panel_tx_cmd_set(panel, dbi_bwg_type);
		if (rc)
		{
			DISP_ERROR("(%s) send  cmds(%s) failed! bl_lvl(%d),rc(%d)\n",
					panel->name, cmd_set_prop_map[dbi_bwg_type], bl_lvl, rc);
			return rc;
		}
		mi_cfg->dbi_bwg_type = dbi_bwg_type;
	}

	return rc;
}


int mi_dsi_panel_read_peak_hdr_gamma(struct dsi_panel *panel)
{
	struct gamma_cfg *gamma_cfg;
	struct dsi_display_mode_priv_info *priv_info = NULL;
	u8 gamma_reg_data[18] = {0};
	u8 gamma_data[300];
	u8 peak_gamma_reg[9] = {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8};
	unsigned long mode_flags_backup = 0;
	int i = 0;
	int j = 0;
	int rc = 0;
	int rd_length = 0;
	int rd_length_num1 = 0;
	int rd_length_num2 = 0;
	int cnt = 2;
	u32 cmd_update_index = 0;
	struct dsi_cmd_update_info *info = NULL;
	u8 reg_BF = 0x0B;
	int cnt_rgb[12] = {0};
	int use_offset = 0;
	int gamma_data_half = 0;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}
	mutex_lock(&panel->panel_lock);

	DISP_INFO("[%s] read peak hdr gamma start\n", panel->type);

	gamma_cfg = &panel->mi_cfg.gamma_cfg;
	mode_flags_backup = panel->mipi_device.mode_flags;
	panel->mipi_device.mode_flags |= MIPI_DSI_MODE_LPM;
	priv_info = panel->cur_mode->priv_info;

	memset(gamma_data, 0, sizeof(gamma_data));
	cmd_update_index = DSI_CMD_SET_MI_PEAK_GAMMA_READ_PRE_COMMAND_UPDATE;
	info = priv_info->cmd_update[cmd_update_index];

	while (cnt --) {
		if (info && info->mipi_address == 0xBF) {
			reg_BF = (cnt==1) ? 0x0B:0x1B;
			mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
				DSI_CMD_SET_MI_PEAK_GAMMA_READ_PRE, info, &reg_BF, sizeof(reg_BF));
		}

		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_PEAK_GAMMA_READ_PRE);
		if (rc) {
			DISP_ERROR("[%s] pre read peak hdr gamma  send cmd failed, rc=%d\n", panel->type, rc);
			goto exit;
		}

		for(i = 0; i < 9; i++){
			if(i % 3 != 2)
				rd_length = 18;
			else
				rd_length = 14;
			memset(gamma_reg_data, 0, sizeof(gamma_reg_data));
			rc = mipi_dsi_dcs_read(&panel->mipi_device, peak_gamma_reg[i], gamma_reg_data, rd_length);

			DISP_DEBUG("read data = %i, %x %x %x %x %x %x %x %x %x\n", i,gamma_reg_data[0],
				gamma_reg_data[1],gamma_reg_data[2],gamma_reg_data[3],gamma_reg_data[4],
				gamma_reg_data[5],gamma_reg_data[6],gamma_reg_data[7],gamma_reg_data[8]);
			DISP_DEBUG("read data = %i, %x %x %x %x %x %x %x %x %x\n", i,gamma_reg_data[9],
				gamma_reg_data[10],gamma_reg_data[11],gamma_reg_data[12],gamma_reg_data[13],
				gamma_reg_data[14],gamma_reg_data[15],gamma_reg_data[16],gamma_reg_data[17]);

			memcpy(&gamma_data[18 * rd_length_num1 + rd_length_num2 * 14], gamma_reg_data, rd_length);

			if(rd_length == 18)
				rd_length_num1++;
			else
				rd_length_num2++;
			if (rc < 0 || rc != rd_length) {
				DISP_ERROR("[%s] read  reg  failed, rc=%d\n", panel->type, rc);
				goto exit;
			}
		}

	}

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_PEAK_GAMMA_READ_PRE_DISABLE);
	if (rc) {
		DISP_ERROR("[%s] disable send cmd failed, rc=%d\n", panel->type, rc);
		goto exit;
	}
	panel->mipi_device.mode_flags = mode_flags_backup;
	gamma_cfg->read_done = true;
	gamma_cfg->update_done = false;
	gamma_cfg->peak_hdr_gamma = true;

	/* cnt_rgb[12] is :
	 r255_12+offset4_12, g255_12+offset4_12, b255_12+offset4_12, //4000nit 120HZ
	 r255_60+offset4_60, g255_60+offset4_60, b255_60+offset4_60, //4000nit 60HZ
	 r255_12+offset3_12, g255_12+offset3_12, b255_12+offset3_12, //3600nit 120HZ
	 r255_60+offset3_60, g255_60+offset3_60, b255_60+offset3_60, //3600nit 60HZ
	*/
	for(i=1; i<13; i++) {
		j = (i < 7) ? i:(i-6);
		cnt_rgb[i-1] = (int)(gamma_data[j*50-2] << 8 | gamma_data[j*50 - 1]);
		if (i>0 && i<=6) {
			cnt_rgb[i-1] += gamma_offset_peak4000[i*25-1];
			DISP_DEBUG("rgb255--i=%d, %d, offset=%d\n", i, cnt_rgb[i-1],
				gamma_offset_peak4000[i*25-1]);
		}
		else if (i>6 && i<=12) {
			cnt_rgb[i-1] += gamma_offset_peak3600[(i-6)*25-1];
			DISP_DEBUG("rgb255--i=%d, %d, offset=%d\n", i, cnt_rgb[i-1],
				gamma_offset_peak3600[(i-6)*25-1]);
		}
	}

	for (i=0; i<2; i++)
		for (j=0; j<6; j++) {
			if (cnt_rgb[i*6 + j] >= 4095)
				use_offset |= 1 << i;
	}


	for(i = 0; i < 150; i++){

		if (use_offset == 0 || use_offset == 2) {
			gamma_data_half = (int)(gamma_data[i*2] << 8 | gamma_data[i*2 + 1])
								+ gamma_offset_peak4000[i];
			DISP_DEBUG("i=%d,gamma_data_half=%d,gamma_offset=%d\n",
				i, gamma_data_half,gamma_offset_peak4000[i]);
		}
		else if (use_offset == 1) {
			gamma_data_half = (int)(gamma_data[i*2] << 8 | gamma_data[i*2 + 1])
								+ gamma_offset_peak3600[i];
			DISP_DEBUG("i=%d,gamma_data_half=%d,gamma_offset=%d\n",
				i, gamma_data_half,gamma_offset_peak3600[i]);
		} else {
			DISP_INFO("peak_hdr use none offset!\n");
			gamma_cfg->peak_hdr_gamma = false;
			rc = -3;
			goto exit;
		}

		if(i < 9){
			gamma_cfg->peak_reg.reg_120_B0[i * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_120_B0[i * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 9 && i< 18){
			gamma_cfg->peak_reg.reg_120_B1[(i - 9) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_120_B1[(i - 9) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 18 && i < 25){
			gamma_cfg->peak_reg.reg_120_B2[(i -18 ) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_120_B2[(i - 18) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 25 && i < 34){
			gamma_cfg->peak_reg.reg_120_B3[(i - 25) *2 ] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_120_B3[(i - 25) *2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 34 && i< 43){
			gamma_cfg->peak_reg.reg_120_B4[(i - 34) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_120_B4[(i - 34) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 43 && i < 50){
			gamma_cfg->peak_reg.reg_120_B5[(i - 43) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_120_B5[(i - 43) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 50 && i < 59){
			gamma_cfg->peak_reg.reg_120_B6[(i - 50) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_120_B6[(i - 50) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 59 && i < 68){
			gamma_cfg->peak_reg.reg_120_B7[(i - 59) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_120_B7[(i - 59) * 2+ 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 68 && i < 75){
			gamma_cfg->peak_reg.reg_120_B8[(i - 68) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_120_B8[(i - 68) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 75 && i < 84){
			gamma_cfg->peak_reg.reg_60_B0[(i - 75) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_60_B0[(i - 75) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 84 && i < 93){
			gamma_cfg->peak_reg.reg_60_B1[(i - 84) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_60_B1[(i - 84) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 93 && i < 100){
			gamma_cfg->peak_reg.reg_60_B2[(i - 93) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_60_B2[(i - 93) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 100 && i < 109){
			gamma_cfg->peak_reg.reg_60_B3[(i - 100) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_60_B3[(i - 100) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 109 && i < 118){
			gamma_cfg->peak_reg.reg_60_B4[(i - 109) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_60_B4[(i - 109) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 118 && i < 125){
			gamma_cfg->peak_reg.reg_60_B5[(i - 118) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_60_B5[(i - 118) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 125 && i < 134){
			gamma_cfg->peak_reg.reg_60_B6[(i - 125) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_60_B6[(i - 125) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 134 && i < 143){
			gamma_cfg->peak_reg.reg_60_B7[(i - 134) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_60_B7[(i - 134) * 2 + 1] = gamma_data_half & 0xFF;
		}
		else if(i >= 143 && i < 150){
			gamma_cfg->peak_reg.reg_60_B8[(i - 143) * 2] = (gamma_data_half>> 8) & 0xFF;
			gamma_cfg->peak_reg.reg_60_B8[(i - 143) * 2 + 1] = gamma_data_half & 0xFF;
		}
	}
exit:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int mi_dsi_panel_update_peak_hdr_gamma(struct dsi_panel *panel)
{
	int rc = 0;
	int i = 0;
	struct gamma_cfg *gamma_cfg;
	struct dsi_display_mode_priv_info *priv_info = NULL;
	struct dsi_cmd_update_info *info = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;

	DISP_INFO("%s begin +\n", __func__);
	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params or not initialized\n");
		return -EINVAL;
	}

	gamma_cfg = &panel->mi_cfg.gamma_cfg;
	priv_info = panel->cur_mode->priv_info;

	cmd_update_index = DSI_CMD_SET_MI_PEAK_GAMMA_COMMAND_UPDATE;
	cmd_update_count = priv_info->cmd_update_count[cmd_update_index];
	info = priv_info->cmd_update[cmd_update_index];
	for(i = 0; i < cmd_update_count; i++){
		if ((info->index >= 4 && info->index <= 12) ||(info->index>=24 && info->index <= 32)) {
			if (info && info->mipi_address == 0xB0)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,	DSI_CMD_SET_MI_PEAK_GAMMA,
				info,gamma_cfg->peak_reg.reg_120_B0,sizeof(gamma_cfg->peak_reg.reg_120_B0));
			else if (info && info->mipi_address == 0xB1)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,	DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_120_B1, sizeof(gamma_cfg->peak_reg.reg_120_B1));
			else if (info && info->mipi_address == 0xB2)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_120_B2, sizeof(gamma_cfg->peak_reg.reg_120_B2));
			else if (info && info->mipi_address == 0xB3)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,	DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_120_B3, sizeof(gamma_cfg->peak_reg.reg_120_B3));
			else if (info && info->mipi_address == 0xB4)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_120_B4, sizeof(gamma_cfg->peak_reg.reg_120_B4));
			else if (info && info->mipi_address == 0xB5)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_120_B5, sizeof(gamma_cfg->peak_reg.reg_120_B5));
			else if (info && info->mipi_address == 0xB6)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,	DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_120_B6, sizeof(gamma_cfg->peak_reg.reg_120_B6));
			else if (info && info->mipi_address == 0xB7)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_120_B7, sizeof(gamma_cfg->peak_reg.reg_120_B7));
			else if (info && info->mipi_address == 0xB8)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_120_B8, sizeof(gamma_cfg->peak_reg.reg_120_B8));
		}
		else if ((info->index >= 14 && info->index <= 22) ||(info->index>=34 && info->index <= 42)) {
			if (info && info->mipi_address == 0xB0)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_60_B0, sizeof(gamma_cfg->peak_reg.reg_60_B0));
			else if (info && info->mipi_address == 0xB1)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_60_B1, sizeof(gamma_cfg->peak_reg.reg_60_B1));
			else if (info && info->mipi_address == 0xB2)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_60_B2, sizeof(gamma_cfg->peak_reg.reg_60_B2));
			else if (info && info->mipi_address == 0xB3)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_60_B3, sizeof(gamma_cfg->peak_reg.reg_60_B3));
			else if (info && info->mipi_address == 0xB4)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_60_B4, sizeof(gamma_cfg->peak_reg.reg_60_B4));
			else if (info && info->mipi_address == 0xB5)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_60_B5, sizeof(gamma_cfg->peak_reg.reg_60_B5));
			else if (info && info->mipi_address == 0xB6)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_60_B6, sizeof(gamma_cfg->peak_reg.reg_60_B6));
			else if (info && info->mipi_address == 0xB7)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_60_B7, sizeof(gamma_cfg->peak_reg.reg_60_B7));
			else if (info && info->mipi_address == 0xB8)
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode, DSI_CMD_SET_MI_PEAK_GAMMA,
				 info,gamma_cfg->peak_reg.reg_60_B8, sizeof(gamma_cfg->peak_reg.reg_60_B8));
		}
		info++;
	}

	gamma_cfg->update_done = true;
	DISP_INFO("%s end -\n", __func__);
	return rc;
}

int mi_dsi_panel_set_hdr_peak_mode(struct dsi_panel *panel, u32 bl_lvl)
{
	int rc = 0;
	if (bl_lvl >= PEAK_HDR_BL_LEVEL_NT37706 && bl_lvl < MAX_BL_LEVEL
		&& !panel->mi_cfg.is_peak_hdr
		&& panel->cur_mode->timing.refresh_rate == 60) {
		if (!panel->mi_cfg.flat_sync_te) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_OFF);
		}
		panel->mi_cfg.is_peak_hdr = true;
		panel->mi_cfg.feature_val[DISP_FEATURE_FLAT_MODE] = FEATURE_OFF;

		DSI_INFO("bl_lvl is %d, enter peakhdr mode on, refresh_rate = %d.\n",
				bl_lvl, panel->cur_mode->timing.refresh_rate);
	} else if (bl_lvl != 0 && bl_lvl < PEAK_HDR_BL_LEVEL_NT37706
		&& panel->mi_cfg.is_peak_hdr) {
		if (!panel->mi_cfg.flat_sync_te) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);
		}
		panel->mi_cfg.is_peak_hdr = false;
		panel->mi_cfg.feature_val[DISP_FEATURE_FLAT_MODE] = FEATURE_ON;
		DSI_INFO("bl_lvl is %d, exit peakhdr mode on, refresh_rate = %d.\n",
				bl_lvl, panel->cur_mode->timing.refresh_rate);
	}

	if (rc)
		DSI_ERR("[%s] failed to send peakhdr mode cmd, rc=%d\n",
				panel->name, rc);

	return 0;
}

int mi_dsi_panel_set_hdr_peak_n16t(struct dsi_panel *panel, u32 bl_lvl)
{
	int rc = 0;
	if (bl_lvl >= PEAK_HDR_BL_LEVEL_NT37706 && bl_lvl < MAX_BL_LEVEL
		&& !panel->mi_cfg.is_peak_hdr
		&& panel->cur_mode->timing.refresh_rate == 60) {
		if (!panel->mi_cfg.flat_sync_te) {
			if (mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PA) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_OFF);
			} else if (mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PB
				|| mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PC)
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_APB_MODE_ON);
		}
		panel->mi_cfg.is_peak_hdr = true;
		panel->mi_cfg.feature_val[DISP_FEATURE_FLAT_MODE] = FEATURE_OFF;

		DSI_INFO("bl_lvl is %d, enter peakhdr mode on, refresh_rate = %d.\n",
				bl_lvl, panel->cur_mode->timing.refresh_rate);
	} else if (bl_lvl != 0 && bl_lvl < PEAK_HDR_BL_LEVEL_NT37706
		&& panel->mi_cfg.is_peak_hdr) {
		if (!panel->mi_cfg.flat_sync_te) {
			if (mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PA) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);
			} else if (mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PB
				|| mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PC)
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_APB_MODE_OFF);
		}
		panel->mi_cfg.is_peak_hdr = false;
		panel->mi_cfg.feature_val[DISP_FEATURE_FLAT_MODE] = FEATURE_ON;
		DSI_INFO("bl_lvl is %d, exit peakhdr mode on, refresh_rate = %d.\n",
				bl_lvl, panel->cur_mode->timing.refresh_rate);
	}

	if (rc)
		DSI_ERR("[%s] failed to send peakhdr mode cmd, rc=%d\n",
				panel->name, rc);

	return 0;
}

int mi_dsi_update_aod_cmd_n16t_PB(struct dsi_panel *panel,
			enum dsi_cmd_set_type type, int bl_lvl)
{
	struct dsi_display_mode_priv_info *priv_info = NULL;
	struct dsi_cmd_update_info *info = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	u8 bl_buf[6] = {0};
	u8 reg_buf_60[3] = {0};
	int j = 0;
	int size = 2;
	u8 reg_buf_48_120[1] = {0x03};
	u8 reg_buf_48_90[1] = {0x13};
	u8 reg_buf_48_60[1] = {0x23};
	reg_buf_60[0] = 0x00;
	reg_buf_60[1] = 0x10;
	reg_buf_60[2] = 0x20;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	bl_buf[0] = (bl_lvl >> 8) & 0xff;
	bl_buf[1] = bl_lvl & 0xff;
	size = 2;

	switch (type) {
	case DSI_CMD_SET_MI_AOD_ENTER:
		cmd_update_index = DSI_CMD_SET_MI_AOD_ENTER_UPDATE;
		break;
	case DSI_CMD_SET_MI_AOD_ENTER_60HZ:
		cmd_update_index = DSI_CMD_SET_MI_AOD_ENTER_60HZ_UPDATE;
		break;
	case DSI_CMD_SET_MI_AOD_EXIT:
		cmd_update_index = DSI_CMD_SET_MI_AOD_EXIT_UPDATE;
		break;
	case DSI_CMD_SET_ON:
		cmd_update_index = DSI_CMD_SET_ON_UPDATE;
		break;
	default:
		DISP_ERROR("[%s] unsupport cmd %s\n",
				panel->type, cmd_set_prop_map[type]);
		return -EINVAL;
	}

	priv_info = panel->cur_mode->priv_info;
	info = priv_info->cmd_update[cmd_update_index];
	cmd_update_count = priv_info->cmd_update_count[cmd_update_index];
	for (j = 0; j < cmd_update_count; j++) {
		if (info) {
			if(info->mipi_address == 0x48) {
				if (panel->cur_mode->timing.refresh_rate == 120){
					mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, reg_buf_48_120, sizeof(reg_buf_48_120));
					DISP_INFO("[%s] update [%s] mipi_address(0x%02X) = 0x%02X. index(%d) lenght(%d)\n",
						panel->type, cmd_set_prop_map[info->type],
						info->mipi_address, reg_buf_48_120[0], info->index, info->length);
				} else if (panel->cur_mode->timing.refresh_rate == 90) {
					mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, reg_buf_48_90, sizeof(reg_buf_48_90));
					DISP_INFO("[%s] update [%s] mipi_address(0x%02X) = 0x%02X. index(%d) lenght(%d)\n",
						panel->type, cmd_set_prop_map[info->type],
						info->mipi_address, reg_buf_48_90[0], info->index, info->length);
				} else if (panel->cur_mode->timing.refresh_rate == 60){
					mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, reg_buf_48_60, sizeof(reg_buf_48_60));
					DISP_INFO("[%s] update [%s] mipi_address(0x%02X) = 0x%02X. index(%d) lenght(%d)\n",
						panel->type, cmd_set_prop_map[info->type],
						info->mipi_address, reg_buf_48_60[0], info->index, info->length);
				}
			} else if (info->mipi_address == 0x60) {
				if (panel->cur_mode->timing.refresh_rate == 120){
					mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, &reg_buf_60[0], sizeof(reg_buf_60[0]));
					DISP_INFO("[%s] update [%s] mipi_address(0x%02X) = 0x%02X. index(%d) lenght(%d)\n",
						panel->type, cmd_set_prop_map[info->type],
						info->mipi_address, reg_buf_60[0], info->index, info->length);
				} else if (panel->cur_mode->timing.refresh_rate == 90) {
					mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, &reg_buf_60[1], sizeof(reg_buf_60[1]));
					DISP_INFO("[%s] update [%s] mipi_address(0x%02X) = 0x%02X. index(%d) lenght(%d)\n",
						panel->type, cmd_set_prop_map[info->type],
						info->mipi_address, reg_buf_60[1], info->index, info->length);
				} else if (panel->cur_mode->timing.refresh_rate == 60){
					mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, &reg_buf_60[2], sizeof(reg_buf_60[2]));
					DISP_INFO("[%s] update [%s] mipi_address(0x%02X) = 0x%02X. index(%d) lenght(%d)\n",
						panel->type, cmd_set_prop_map[info->type],
						info->mipi_address, reg_buf_60[2], info->index, info->length);
				}
			} else if (info->mipi_address == 0x51) {
				mi_dsi_panel_update_cmd_set(panel, panel->cur_mode,
						type, info, bl_buf, size);
				DISP_INFO("[%s] update [%s] mipi_address(0x%02X) = 0x%02X. index(%d) lenght(%d)\n",
					panel->type, cmd_set_prop_map[info->type],
					info->mipi_address, bl_lvl, info->index, info->length);
			}
			info++;
		}
	}

	return 0;
}

int mi_dsi_panel_gamma_switch_n16t_PB(struct dsi_panel *panel)
{
	int rc = 0;
	u32 doze_brightness = panel->mi_cfg.doze_brightness;
	int update_bl = 0;

	if (doze_brightness == DOZE_TO_NORMAL)
		doze_brightness = panel->mi_cfg.last_aod_state;

	// Dynamically update the brightness when AOD state changed
	switch (doze_brightness) {
		case DOZE_BRIGHTNESS_HBM:
			if (panel->cur_mode->timing.refresh_rate == 30) {
				if (mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PB) {
					update_bl = 1023;  // 03 FF
				} else {
					update_bl = 4095;  // 0F FF
				}
			} else if (panel->cur_mode->timing.refresh_rate != 30
					&& panel->mi_cfg.last_refresh_rate == 30) {
				update_bl = panel->mi_cfg.doze_hbm_dbv_level;
			}
			break;
		case DOZE_BRIGHTNESS_LBM:
			if (panel->cur_mode->timing.refresh_rate == 30) {
				if (mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PB) {
					update_bl = 511;  // 01 FF
				} else {
					update_bl = 341;  // 01 55
				}
			} else if (panel->cur_mode->timing.refresh_rate != 30
					&& panel->mi_cfg.last_refresh_rate == 30) {
				update_bl = panel->mi_cfg.doze_lbm_dbv_level;
			}
			break;
		default:
			break;
	}

	if (panel->cur_mode->timing.refresh_rate == 30
		&& (panel->power_mode == SDE_MODE_DPMS_LP1 ||
			panel->power_mode == SDE_MODE_DPMS_LP2)	) {
		if(mi_get_panel_id_by_dsi_panel(panel) == N16T_PANEL_PC &&
			panel->mi_cfg.last_fps == 60) {
			mi_dsi_update_aod_cmd_n16t_PB(panel, DSI_CMD_SET_MI_AOD_ENTER_60HZ, update_bl);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_AOD_ENTER_60HZ);
		} else {
			mi_dsi_update_aod_cmd_n16t_PB(panel, DSI_CMD_SET_MI_AOD_ENTER, update_bl);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_AOD_ENTER);
		}

		DISP_TIME_INFO("%s panel-enter aod: ready change fps (%d->%d)"
			"power_mode =%d(%s), panel_state=%d, doze_brightness = (%d->%d).\n",
			panel->type, panel->mi_cfg.last_refresh_rate,
			panel->cur_mode->timing.refresh_rate, panel->power_mode,
			get_display_power_mode_name(panel->power_mode),
			panel->mi_cfg.panel_state, panel->mi_cfg.last_doze_brightness,
			panel->mi_cfg.doze_brightness);

	} else if(panel->cur_mode->timing.refresh_rate != 30
			&& panel->mi_cfg.last_refresh_rate == 30) {
		mi_dsi_update_aod_cmd_n16t_PB(panel, DSI_CMD_SET_MI_AOD_EXIT, update_bl);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_AOD_EXIT);

		DISP_TIME_INFO("%s panel-exit aod: ready change fps (%d->%d)"
			"power_mode =%d(%s), panel_state=%d, doze_brightness = (%d->%d).\n",
			panel->type, panel->mi_cfg.last_refresh_rate,
			panel->cur_mode->timing.refresh_rate, panel->power_mode,
			get_display_power_mode_name(panel->power_mode),
			panel->mi_cfg.panel_state, panel->mi_cfg.last_doze_brightness,
			panel->mi_cfg.doze_brightness);

	} else if (panel->cur_mode->timing.refresh_rate == 120){
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FPS_120_GAMMA);
	} else if (panel->cur_mode->timing.refresh_rate == 90) {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FPS_90_GAMMA);
	} else if (panel->cur_mode->timing.refresh_rate == 60){
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FPS_60_GAMMA);
	}

	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_SET_MI_AOD cmds, rc=%d\n",
				panel->name, rc);

	return rc;
}

ssize_t mi_dsi_panel_lockdown_info_read( unsigned char *plockdowninfo)
{

	int rc = 0;
	int i = 0;

	if (!g_panel->panel_initialized) {
		DISP_ERROR("[%s] Panel not initialized\n", g_panel->type);
		rc = -EINVAL;
	}

	if (!g_panel || !plockdowninfo) {
		pr_err("invalid params\n");
		return -EINVAL;
	}

	for(i = 0; i < 8; i++) {
		plockdowninfo[i] = g_panel->mi_cfg.lockdown_cfg.lockdown_param[i];
	}

	rc = plockdowninfo[0];

	return rc;
}
EXPORT_SYMBOL(mi_dsi_panel_lockdown_info_read);

void dsi_panel_doubleclick_enable(bool on)
{
	g_panel->mi_cfg.tddi_doubleclick_flag = on;
}
EXPORT_SYMBOL(dsi_panel_doubleclick_enable);
