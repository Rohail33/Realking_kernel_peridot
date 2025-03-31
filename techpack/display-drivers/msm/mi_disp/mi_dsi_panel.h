/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#ifndef _MI_DSI_PANEL_H_
#define _MI_DSI_PANEL_H_

#include <linux/types.h>
#include "dsi_defs.h"
#include "mi_disp_feature.h"
#include <linux/pm_wakeup.h>
#include <drm/mi_disp.h>

/* ---------------------------- */
/* | 15 - 14 | 13 - 7 | 6 - 0 | */
/* ---------------------------- */
/* |   mode  | sf fps | min fps| */
/* ---------------------------- -*/
/* mode: 1 idle, 2 auto, 3 qsync */
/*   1 << 14 | 120 << 7 | 24     */
/* ---------------------------- */
#define FPS_NORMAL           0
#define FPS_VALUE_MASK       0x7F
#define FPS_SF_FPS_OFFSET    7
#define FPS_MODE_OFFSET      14
#define FPS_MODE_VALUE_MASK  0x3
#define FPS_MODE_IDLE        1
#define FPS_MODE_AUTO        2
#define FPS_MODE_QSYNC       3
#define FPS_COUNT(DDIC_MODE,DDIC_FPS,DDIC_MIN_FPS) ((DDIC_MODE * 100000) + (DDIC_FPS * 100) + DDIC_MIN_FPS)

#define PMIC_PWRKEY_BARK_TRIGGER 1
#define PMIC_PWRKEY_TRIGGER 2
#define DISPLAY_DELAY_SHUTDOWN_TIME_MS 1800

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

enum dbv_Interval {
	DBV_IN19_27 = 0x0,
	DBV_IN28_4B,
	DBV_IN4C_83,
	DBV_MAX
};

struct dsi_panel;

enum backlight_dimming_state {
	STATE_NONE,
	STATE_DIM_BLOCK,
	STATE_DIM_RESTORE,
	STATE_ALL
};

enum panel_state {
	PANEL_STATE_OFF = 0,
	PANEL_STATE_ON,
	PANEL_STATE_DOZE_HIGH,
	PANEL_STATE_DOZE_LOW,
	PANEL_STATE_MAX,
};

enum dc_lut_state {
	DC_LUT_60HZ,
	DC_LUT_120HZ,
	DC_LUT_MAX
};

enum dc_feature_type {
	TYPE_NONE = 0,
	TYPE_CRC_SKIP_BL
};

/* Enter/Exit DC_LUT info */
struct dc_lut_cfg {
	bool update_done;
	u8 enter_dc_lut[DC_LUT_MAX][75];
	u8 exit_dc_lut[DC_LUT_MAX][75];
};

struct flat_mode_cfg {
	bool update_done;
	int cur_flat_state;  /*only use when flat cmd need sync with te*/
	u8 flat_on_data[4];
	u8 flat_off_data[4];
};

struct lockdown_cfg {
	u8 lockdown_param[8];
};

struct gamma_reg {
	u8 reg_120_B0[18];
	u8 reg_120_B1[18];
	u8 reg_120_B2[14];
	u8 reg_120_B3[18];
	u8 reg_120_B4[18];
	u8 reg_120_B5[14];
	u8 reg_120_B6[18];
	u8 reg_120_B7[18];
	u8 reg_120_B8[14];
	u8 reg_60_B0[18];
	u8 reg_60_B1[18];
	u8 reg_60_B2[14];
	u8 reg_60_B3[18];
	u8 reg_60_B4[18];
	u8 reg_60_B5[14];
	u8 reg_60_B6[18];
	u8 reg_60_B7[18];
	u8 reg_60_B8[14];
};

struct gamma_cfg {
	struct gamma_reg peak_reg;
	bool read_done;
	bool update_done;
	bool peak_hdr_gamma;
};

struct mi_dsi_panel_cfg {
	struct dsi_panel *dsi_panel;

	/* xiaomi panel id */
	u64 mi_panel_id;

	bool is_tddi_flag;
	bool tddi_doubleclick_flag;
	bool panel_dead_flag;

	/* xiaomi feature values */
	int feature_val[DISP_FEATURE_MAX];

	/* indicate esd check gpio and config irq */
	int esd_err_irq_gpio;
	int esd_err_irq;
	int esd_err_irq_flags;
	bool esd_err_enabled;
	bool panel_build_id_read_needed;
	bool panel_mura_info_read_needed;

	/* indicate second esd check gpio and config irq */
	int esd_err_irq_gpio_second;
	int esd_err_irq_second;
	int esd_err_irq_flags_second;
	bool esd_err_enabled_second;

	/* brightness control */
	u32 last_bl_level;
	u32 last_no_zero_bl_level;
	atomic_t brightness_clone;
	u32 max_brightness_clone;

	/* AOD control */
	u32 doze_brightness;
	u32 last_doze_brightness;
	u32 last_aod_state;
	struct mutex doze_lock;
	struct wakeup_source *disp_wakelock;
	int doze_hbm_dbv_level;
	int doze_lbm_dbv_level;
	bool aod_status;

	/* Local HBM */
	bool local_hbm_enabled;
	u32 lhbm_ui_ready_delay_frame;
	u32 lhbm_lbl_mode_threshold;
	u32 lhbm_hbm_mode_threshold;
	u32 ltmp_cmpst_on_threshold;
	u32 real_ltmp_cmpst_state;
	bool need_fod_animal_in_normal;
	bool aod_to_normal_statue;
	bool in_fod_calibration;
	bool uefi_read_gray_scale_success;
	bool lhbm_gxzw;
	bool lhbm_0size_on;
	bool aod_to_normal_pending;
	/*3*2*3*/
	u8 lhbm_rgb_param[18];
	u8 lhbm_param[52];
	u8 lhbm_gamma_param[28];
	bool read_lhbm_gamma_success;

	/* DDIC round corner */
	bool ddic_round_corner_enabled;

	/* lockdown read */
	struct lockdown_cfg lockdown_cfg;

	/* DC */
	bool dc_feature_enable;
	bool dc_update_flag;
	enum dc_feature_type dc_type;
	u32 dc_threshold;
	struct dc_lut_cfg dc_cfg;
	u32 real_dc_state;
	/*dbi*/
	int real_dbi_state;

	/* flat mode */
	bool flatmode_default_on_enabled;
	bool flat_sync_te;
	bool flat_update_flag;
	struct flat_mode_cfg flat_cfg;

	/* peak hdr */
	bool is_peak_hdr;
	bool update_gamma_befor_init;
	u8 gamma_rgb_param[162];
	bool read_gamma_success;

    /* record the last refresh_rate */
	u32 last_refresh_rate;
	u32 last_fps_mode;
	u32 last_fps;

	/* Dimming */
	u32 panel_on_dimming_delay;
	u32 dimming_state;
	bool disable_ic_dimming;

	/* Panel status */
	int panel_state;
	/* ip */
	bool ip_state;

	u8 panel_batch_number;
	bool panel_batch_number_read_done;

	u32 hbm_backlight_threshold;
	bool count_hbm_by_backlight;
	int pmic_pwrkey_status;

	/*DDIC ADD CMD Type*/
	enum dsi_cmd_set_type skip_source_type;
	enum dsi_cmd_set_type dbi_bwg_type;

	/* DDIC auto update gamma */
	bool nedd_auto_update_gamma;
	bool first_timing_switch;
	ktime_t last_mode_switch_time;

	/* em cycle 32 pulse for 3840 PWM */
	bool is_em_cycle_32_pulse;
	u32 panel_3840pwm_dbv_threshold;

	/*hbm gamma read*/
	struct gamma_cfg gamma_cfg;
	bool aod_enter_flags;
	bool aod_exit_flags;

	bool multi_timing_enable;
};

struct panel_batch_info
{
	u8 batch_number;       /* Panel batch number */
	char *batch_name;      /* Panel batch name */
};

struct drm_panel_build_id_config {
	struct dsi_panel_cmd_set id_cmd;
	struct dsi_panel_cmd_set sub_id_cmd;
	u32 id_cmds_rlen;
	u8 build_id;
};

struct drm_panel_wp_config {
	struct dsi_panel_cmd_set pre_tx_cmd;
	struct dsi_panel_cmd_set wp_cmd;
	u32 wp_read_info_index;
	u32 wp_cmds_rlen;
	u8 *return_buf;
};

struct drm_panel_cell_id_config {
	struct dsi_panel_cmd_set pre_tx_cmd;
	struct dsi_panel_cmd_set cell_id_cmd;
	struct dsi_panel_cmd_set after_tx_cmd;
	u32 cell_id_read_info_index;
	u32 cell_id_cmds_rlen;
	u8 *return_buf;
};

struct drm_panel_mura_info_config {
	struct dsi_panel_cmd_set batch_cmd;
	struct dsi_panel_cmd_set offset_cmd;
	struct dsi_panel_cmd_set checksum_cmd;
	struct dsi_panel_cmd_set batch_read_cmd;
	struct dsi_panel_cmd_set offset_read_cmd;
	struct dsi_panel_cmd_set checksum_read_cmd;
	bool ischecked;
	char batch[3];
	char checksum[5];
	char offset[7];
};

extern const char *cmd_set_prop_map[DSI_CMD_SET_MAX];
extern const char *cmd_set_update_map[DSI_CMD_UPDATE_MAX];

int mi_dsi_panel_pre_init(struct dsi_panel *panel);
int mi_dsi_panel_init(struct dsi_panel *panel);
int mi_dsi_panel_deinit(struct dsi_panel *panel);
int mi_dsi_acquire_wakelock(struct dsi_panel *panel);
int mi_dsi_release_wakelock(struct dsi_panel *panel);

bool is_aod_and_panel_initialized(struct dsi_panel *panel);

bool is_backlight_set_skip(struct dsi_panel *panel, u32 bl_lvl);

void mi_dsi_panel_update_last_bl_level(struct dsi_panel *panel,
			int brightness);

bool is_hbm_fod_on(struct dsi_panel *panel);

int mi_dsi_panel_esd_irq_ctrl(struct dsi_panel *panel,
			bool enable);

int mi_dsi_panel_esd_irq_ctrl_locked(struct dsi_panel *panel,
			bool enable);

int mi_dsi_print_51_backlight_log(struct dsi_panel *panel,
			struct dsi_cmd_desc *cmd);

int mi_dsi_panel_parse_sub_timing(struct mi_mode_info *mode,
			struct dsi_parser_utils *utils);

int mi_dsi_panel_parse_cmd_sets_update(struct dsi_panel *panel,
			struct dsi_display_mode *mode);

int mi_dsi_panel_parse_gamma_config(struct dsi_panel *panel,
			struct dsi_display_mode *mode);

int mi_dsi_panel_parse_dc_fps_config(struct dsi_panel *panel,
			struct dsi_display_mode *mode);

int mi_dsi_panel_set_dbi_by_temp(struct dsi_panel *panel,
			int temp_val);

int mi_dsi_panel_set_dbi_by_temp_locked(struct dsi_panel *panel,
			int temp_val);

int mi_dsi_panel_set_csc_by_temp_locked(struct dsi_panel *panel,
			int temp_val);

int mi_dsi_panel_set_dbi_3dlut_by_temp(struct dsi_panel *panel,
			int temp_val);

int mi_dsi_panel_set_dbi_3dlut_by_temp_locked(struct dsi_panel *panel,
			int temp_val);

int mi_dsi_update_switch_cmd_N3(struct dsi_panel *panel,
		u32 cmd_update_index, u32 index);

int mi_dsi_panel_set_lhbm_fod_locked(struct dsi_panel *panel,
			struct disp_feature_ctl *ctl);

int mi_dsi_panel_parse_2F26reg_gamma_config(struct dsi_panel *panel,
			struct dsi_display_mode *mode);

int mi_dsi_panel_update_cmd_set(struct dsi_panel *panel,
			struct dsi_display_mode *cur_mode, enum dsi_cmd_set_type type,
			struct dsi_cmd_update_info *info, u8 *payload, u32 size);

int mi_dsi_panel_write_cmd_set(struct dsi_panel *panel,
			struct dsi_panel_cmd_set *cmd_sets);

int mi_dsi_panel_read_batch_number(struct dsi_panel *panel);

bool mi_dsi_panel_is_need_tx_cmd(u32 feature_id);

int mi_dsi_panel_set_disp_param(struct dsi_panel *panel,
			struct disp_feature_ctl *ctl);

int mi_dsi_panel_get_disp_param(struct dsi_panel *panel,
			struct disp_feature_ctl *ctl);

ssize_t mi_dsi_panel_show_disp_param(struct dsi_panel *panel,
			char *buf, size_t size);

int mi_dsi_panel_set_doze_brightness(struct dsi_panel *panel,
			u32 doze_brightness);

int mi_dsi_panel_get_doze_brightness(struct dsi_panel *panel,
			u32 *doze_brightness);

int mi_dsi_panel_get_brightness(struct dsi_panel *panel,
			u32 *brightness);

int mi_dsi_panel_write_dsi_cmd(struct dsi_panel *panel,
			struct dsi_cmd_rw_ctl *ctl);

int mi_dsi_panel_write_dsi_cmd_set(struct dsi_panel *panel, int type);

ssize_t mi_dsi_panel_show_dsi_cmd_set_type(struct dsi_panel *panel,
			char *buf, size_t size);

int mi_dsi_panel_set_brightness_clone(struct dsi_panel *panel,
			u32 brightness_clone);

int mi_dsi_panel_get_brightness_clone(struct dsi_panel *panel,
			u32 *brightness_clone);

int mi_dsi_panel_get_max_brightness_clone(struct dsi_panel *panel,
			u32 *max_brightness_clone);

int mi_dsi_panel_set_dc_mode(struct dsi_panel *panel, bool enable);

int mi_dsi_panel_set_dc_mode_locked(struct dsi_panel *panel, bool enable);

int mi_dsi_panel_set_ltmp_cmpst_locked(struct dsi_panel *panel, bool enable);

int mi_dsi_panel_set_round_corner_locked(struct dsi_panel *panel,
			bool enable);

int mi_dsi_panel_set_round_corner(struct dsi_panel *panel,
			bool enable);

int mi_dsi_update_switch_cmd(struct dsi_panel *panel);

int mi_dsi_panel_set_gamma_update_reg(struct dsi_panel *panel, u32 bl_lvl);

int mi_dsi_panel_set_gamma_update_state(struct dsi_panel *panel);

int mi_dsi_first_timing_switch(struct dsi_panel *panel);

int mi_dsi_update_flat_mode_on_cmd(struct dsi_panel *panel, enum dsi_cmd_set_type type);

int mi_dsi_update_timing_switch_and_flat_mode_cmd(struct dsi_panel *panel, enum dsi_cmd_set_type type);

int mi_dsi_panel_tigger_timming_switch_delayed_work(struct dsi_panel *panel);

int dsi_panel_parse_build_id_read_config(struct dsi_panel *panel);

int dsi_panel_send_em_cycle_setting(struct dsi_panel *panel, u32 bl_lvl, bool no_backlight);

int mi_dsi_update_51_mipi_cmd(struct dsi_panel *panel, enum dsi_cmd_set_type type, int bl_lvl);

int dsi_panel_parse_mura_reg_read_config(struct dsi_panel *panel);

int dsi_panel_parse_wp_reg_read_config(struct dsi_panel *panel);

int dsi_panel_parse_cell_id_read_config(struct dsi_panel *panel);

int mi_dsi_panel_set_lhbm_0size_locked(struct dsi_panel *panel);

int mi_dsi_set_switch_cmd_before(struct dsi_panel *panel, int fps_mode);
int mi_dsi_panel_set_nolp_locked(struct dsi_panel *panel);
int mi_dsi_panel_update_gamma_param(struct dsi_panel * panel, u32 cmd_update_index,
               enum dsi_cmd_set_type type);

int mi_dsi_panel_read_peak_hdr_gamma(struct dsi_panel *panel);

int mi_dsi_panel_update_peak_hdr_gamma(struct dsi_panel *panel);

int mi_dsi_panel_aod_to_normal_optimize_locked(struct dsi_panel *panel, bool enable);

int mi_dsi_panel_set_hdr_peak_mode(struct dsi_panel *panel, u32 bl_lvl);

int mi_dsi_panel_set_hdr_peak_n16t(struct dsi_panel *panel, u32 bl_lvl);

int mi_dsi_update_aod_cmd_n16t_PB(struct dsi_panel *panel,
		enum dsi_cmd_set_type type, int bl_lvl);

int mi_dsi_panel_gamma_switch_n16t_PB(struct dsi_panel *panel);

int mi_dsi_panel_parse_timing_fps_params(struct dsi_panel *panel, struct dsi_display_mode *mode, struct dsi_parser_utils *utils);

#endif /* _MI_DSI_PANEL_H_ */
