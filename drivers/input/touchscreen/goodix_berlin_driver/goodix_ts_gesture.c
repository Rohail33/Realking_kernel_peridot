/*
 * Goodix Gesture Module
 *
 * Copyright (C) 2019 - 2020 Goodix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/atomic.h>
#include <linux/input/mt.h>
#include "goodix_ts_core.h"



#define TYPE_B_PROTOCOL

#define GOODIX_GESTURE_TYPE_LEN     32
#define GOODIX_GESTURE_DOUBLE_TAP   0xCC
#define GOODIX_GESTURE_SINGLE_TAP   0x4C
#define GOODIX_GESTURE_FOD_DOWN     0x46
#define GOODIX_GESTURE_FOD_UP       0x55

#define GOODIX_GESTURE_FOD_OFFSET   17


static bool module_initialized;

#define IRQ_EVENT_HEAD_LEN          8
#define FOD_EVENT_LEN               10
#define GOODIX_GESTURE_EVENT        0x20
#define IRQ_EVENT_HEAD_LEN_LARGER   16

static int gesture_event_handler(struct goodix_ts_core *cd,
			struct goodix_ts_event *ts_event, u8 *pre_buf)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	int ges_read_len;
	u8 event_status;
	int ret;
	bool sync_late = false;

	ges_read_len = IRQ_EVENT_HEAD_LEN + FOD_EVENT_LEN;
	ret = hw_ops->read(cd, misc->touch_data_addr,
			pre_buf, ges_read_len);
	if (ret) {
		ts_err("failed get gesture event head data");
		return ret;
	}

	if (checksum_cmp(pre_buf, IRQ_EVENT_HEAD_LEN, CHECKSUM_MODE_U8_LE)) {
		ts_err("touch head checksum err");
		ts_err("touch_head %*ph", IRQ_EVENT_HEAD_LEN, pre_buf);
		ts_event->retry = 1;
		if (pre_buf[4] == GOODIX_GESTURE_FOD_UP && pre_buf[0] == 0) {
			ts_info("warning: fod up checksum err");
			sync_late = true;
		}
		else
			return -EINVAL;
	}

	event_status = pre_buf[0];
	ts_debug("touch_head %*ph", IRQ_EVENT_HEAD_LEN_LARGER, pre_buf);
	if (event_status & GOODIX_GESTURE_EVENT || sync_late) {
		ts_event->event_type = EVENT_GESTURE;
		ts_event->gesture_type = pre_buf[4];
	}

	if (cd->sync_mode == SYNC)
		hw_ops->after_event_handler(cd);
	return 0;
}

/**
 * goodix_gesture_ist - Gesture Irq handle
 * This functions is excuted when interrupt happended and
 * ic in doze mode.
 *
 * @cd: pointer to touch core data
 * @module: pointer to goodix_ext_module struct
 * return: 0 good execute, EVT_CANCEL_IRQEVT  stop execute
 */
int goodix_gesture_ist(struct goodix_ts_core *cd)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ts_event gs_event = {0};
	int ret;
	int key_value;
#ifdef TOUCH_FOD_SUPPORT
	unsigned int fodx, fody, fod_id;
	unsigned int overlay_area;
#endif
	u8 gesture_data[32];

	if (atomic_read(&cd->suspended) == 0)
		return EVT_CONTINUE;

	mutex_lock(&cd->report_mutex);

	ret = gesture_event_handler(cd, &gs_event, gesture_data);
	if (ret) {
		ts_err("failed get gesture data");
		goto re_send_ges_cmd;
	}

	if (!(gs_event.event_type & EVENT_GESTURE)) {
		ts_err("invalid event type: 0x%x",
			cd->ts_event.event_type);
		goto re_send_ges_cmd;
	}
#ifdef TOUCH_FOD_SUPPORT
	fod_id = gesture_data[GOODIX_GESTURE_FOD_OFFSET];
	overlay_area = gesture_data[12];
#endif
	switch (gs_event.gesture_type) {

#ifdef TOUCH_FOD_SUPPORT
	case GOODIX_GESTURE_FOD_DOWN:
		if (!(cd->gesture_enabled & FOD_EN)) {
			ts_info("not enable FOD Down");
			if (cd->fod_finger)
				goto gesture_ist_exit;
			else
				break;
		}

		if (cd->fod_down_before_suspend) {
			ts_debug("fod down before suspend, no need report");
			goto gesture_ist_exit;
		}

		fodx = gesture_data[8] | (gesture_data[9] << 8);
		fody = gesture_data[10] | (gesture_data[11] << 8);
		fodx = fodx / 16 * cd->board_data.super_resolution_factor;
		fody = fody / 16 * cd->board_data.super_resolution_factor;
		ts_info("gesture coordinate fodx: %d, fody: %d, fod_id: %d, overlay_area: %d",
					fodx, fody, fod_id, overlay_area);
#ifdef TYPE_B_PROTOCOL
		input_mt_slot(cd->input_dev, fod_id);
		input_mt_report_slot_state(cd->input_dev, MT_TOOL_FINGER, 1);
#endif
		input_report_key(cd->input_dev, BTN_TOUCH, 1);
		input_report_key(cd->input_dev, BTN_TOOL_FINGER, 1);
		input_report_abs(cd->input_dev, ABS_MT_POSITION_X, fodx);
		input_report_abs(cd->input_dev, ABS_MT_POSITION_Y, fody);
		input_report_abs(cd->input_dev, ABS_MT_WIDTH_MAJOR, overlay_area);
		input_report_abs(cd->input_dev, ABS_MT_WIDTH_MINOR, overlay_area);
		input_sync(cd->input_dev);
		if (!cd->fod_finger)
			ts_info("gesture fod down, id %d, overlay_area: %d", fod_id, overlay_area);
		cd->fod_finger = true;
		update_fod_press_status(1);
		goto gesture_ist_exit;
		break;

	case GOODIX_GESTURE_FOD_UP:
		cd->fod_down_before_suspend = false;
		if (!(cd->gesture_enabled & FOD_EN) && (driver_get_touch_mode(TOUCH_ID, DATA_MODE_17) != 2)
#ifndef TOUCH_THP_SUPPORT
			&& (!cd->fod_finger)
#endif
			) {
			ts_info("not enable FOD Up");
			if (cd->fod_finger)
				goto gesture_ist_exit;
			else
				break;
		}

		if (cd->fod_finger) {
			ts_info("gesture fod up, overlay_area: %d", overlay_area);
			cd->fod_finger = false;
#ifdef TYPE_B_PROTOCOL
			input_mt_slot(cd->input_dev, fod_id);
			input_mt_report_slot_state(cd->input_dev, MT_TOOL_FINGER, 0);
#endif
			input_report_key(cd->input_dev, BTN_TOUCH, 0);
			input_report_key(cd->input_dev, BTN_TOOL_FINGER, 0);
			input_report_abs(cd->input_dev, ABS_MT_WIDTH_MAJOR, 0);
			input_report_abs(cd->input_dev, ABS_MT_WIDTH_MINOR, 0);
			input_sync(cd->input_dev);
			update_fod_press_status(0);
		}
		goto gesture_ist_exit;
		break;
#endif

	case GOODIX_GESTURE_SINGLE_TAP:
		if (!(cd->gesture_enabled & SINGLE_TAP_EN)) {
			ts_debug("not enable SINGLE-TAP");
			break;
		}
		ts_info("GTP gesture report single tap");
		key_value = KEY_GOTO;
		input_report_key(cd->input_dev, key_value, 1);
		input_sync(cd->input_dev);
		input_report_key(cd->input_dev, key_value, 0);
		input_sync(cd->input_dev);
		goto gesture_ist_exit;
		break;

	case GOODIX_GESTURE_DOUBLE_TAP:
		if (!(cd->gesture_enabled & DOUBLE_TAP_EN)) {
			ts_debug("not enable DOUBLE-TAP");
			break;
		}

		ts_info("GTP gesture report double tap");
		key_value = KEY_WAKEUP;
		input_report_key(cd->input_dev, key_value, 1);
		input_sync(cd->input_dev);
		input_report_key(cd->input_dev, key_value, 0);
		input_sync(cd->input_dev);
		goto gesture_ist_exit;
		break;
	default:
		ts_info("unsupported gesture: %x", gs_event.gesture_type);
		break;
	}

re_send_ges_cmd:
	if (hw_ops->gesture(cd, cd->gesture_enabled))
		ts_info("warning: failed re_send gesture cmd");
gesture_ist_exit:
	mutex_unlock(&cd->report_mutex);

	return EVT_CANCEL_IRQEVT;
}

/**
 * gsx_gesture_before_suspend - execute gesture suspend routine
 * This functions is excuted to set ic into doze mode
 *
 * @cd: pointer to touch core data
 * @module: pointer to goodix_ext_module struct
 * return: 0 good execute, EVT_IRQCANCLED  stop execute
 */
int gsx_gesture_before_suspend(struct goodix_ts_core *cd)
{
	int ret;
	const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	ret = hw_ops->gesture(cd, cd->gesture_enabled);
	if (ret)
		ts_err("failed enter gesture mode");
	else
		ts_info("enter gesture mode");

	cd->work_status = TP_GESTURE;
	hw_ops->irq_enable(cd, true);
	enable_irq_wake(cd->irq);

	return EVT_CANCEL_SUSPEND;
}

int gsx_gesture_before_resume(struct goodix_ts_core *cd)
{
	const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	hw_ops->irq_enable(cd, false);
	disable_irq_wake(cd->irq);
	hw_ops->reset(cd, GOODIX_NORMAL_RESET_DELAY_MS);

	return EVT_CANCEL_RESUME;
}

int gesture_module_init(void)
{
	module_initialized = true;
	ts_info("gesture module init success");

	return 0;
}

void gesture_module_exit(void)
{
	ts_info("gesture module exit");
	if (!module_initialized)
		return;

	module_initialized = false;
}
