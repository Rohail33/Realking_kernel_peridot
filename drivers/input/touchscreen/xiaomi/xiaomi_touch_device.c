#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/sched.h>

#include "xiaomi_touch.h"

#define DEVICE_NAME_LEN_MAX  64
#define XIAOMI_TOUCH_VENDOR_ID 0x06C2

typedef enum {
	ACTION_DOWN = 0,
	ACTION_MOVE = 1,
	ACTION_UP = 2,
	ACTION_HOVER = 3,
} point_action_t;

typedef enum {
	INPUT_STYLE_FINGER = 0,
	INPUT_STYLE_STYLUS = 1,
	INPUT_STYLE_INVALID,
} input_style_t;

typedef enum {
	FINGER_SLOT = 0,
	FINGER_MAJOR = 1,
	FINGER_MINOR = 2,
	MAX_FINGER_POINT_PROP = 10
} finger_point_prop_t;

typedef enum {
	STYLUS_SLOT = 0,
	STYLUS_TILT_X = 1,
	STYLUS_TILT_Y = 2,
	STYLUS_DISTANCE = 3,
	STYLUS_PRESSURE = 4,
	MAX_STYLUS_POINT_PROP = 10
} stylus_point_prop_t;

typedef struct hal_report_piont {
	input_style_t input_style;
	union {
		struct {
			s32 x;
			s32 y;
			s32 prop[MAX_FINGER_POINT_PROP];
			point_action_t point_action;
		} finger;

		struct {
			s32 x;
			s32 y;
			s32 prop[MAX_STYLUS_POINT_PROP];
			point_action_t point_action;
		} stylus;
	};
} hal_report_piont_t;

static struct input_dev *input_devs[MAX_TOUCH_PANEL_COUNT];
static struct input_dev *stylus_input_devs[MAX_TOUCH_PANEL_COUNT];
static struct mutex ts_report_mutex[MAX_TOUCH_PANEL_COUNT];

static void *report_point_mmap_base = NULL;
static dma_addr_t report_point_phy_base = 0;

static void report_point_mmap_addr_init(void)
{
	if (report_point_mmap_base != NULL) {
		return;
	}
	report_point_mmap_base = kzalloc_retry(PAGE_SIZE, 3);
	if (!report_point_mmap_base) {
		LOG_ERROR("alloc report point mmap addr failed!");
		return;
	}
	report_point_phy_base = virt_to_phys(report_point_mmap_base);
	LOG_INFO("init report point mmap base %p", report_point_mmap_base);
}

static void report_point_mmap_addr_release(void)
{
	int i = 0;
	for (i = 0; i < MAX_TOUCH_PANEL_COUNT; i++) {
		if (input_devs[i] != NULL) {
			return;
		}
		if (stylus_input_devs[i] != NULL) {
			return;
		}
	}
	kzalloc_free(report_point_mmap_base);
	report_point_phy_base = 0;
	report_point_mmap_base = NULL;
	LOG_INFO("release report point mmap");
}

dma_addr_t get_report_point_info_phy_addr(void)
{
	return report_point_phy_base;
}

struct input_dev *register_xiaomi_input_dev(s8 touch_id, int max_x, int max_y, ic_product_code code)
{
	int ret;
	struct input_dev *input_dev = NULL;
	static char name[MAX_TOUCH_PANEL_COUNT][DEVICE_NAME_LEN_MAX];
	static char phys[MAX_TOUCH_PANEL_COUNT][DEVICE_NAME_LEN_MAX];

	if (IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_ERROR("touch id %d is invalid", touch_id);
		return NULL;
	}

	if (input_devs[touch_id] != NULL) {
		LOG_ERROR("touch id %d has been registered", touch_id);
		return input_devs[touch_id];
	}

	input_dev = input_allocate_device();

	if (!input_dev) {
		LOG_ERROR("Failed to allocate input device %d!", touch_id);
		return NULL;
	}

	snprintf(name[touch_id], DEVICE_NAME_LEN_MAX, "Xiaomi_Touch_Input_%d", touch_id);
	snprintf(phys[touch_id], DEVICE_NAME_LEN_MAX, "Xiaomi_Touch_Device_%d", touch_id);
	LOG_INFO("xiaomi allocat !!!,the device is %s, %s, %d, %d",name[touch_id], phys[touch_id], XIAOMI_TOUCH_VENDOR_ID, code);
	input_dev->name = name[touch_id];
	input_dev->phys = phys[touch_id];
	input_dev->id.vendor = XIAOMI_TOUCH_VENDOR_ID;
	input_dev->id.product = code;

	__set_bit(EV_SYN, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_ABS, input_dev->evbit);

	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(BTN_TOOL_FINGER, input_dev->keybit);
	__set_bit(KEY_WAKEUP, input_dev->keybit);
	__set_bit(KEY_GOTO, input_dev->keybit);

	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	input_mt_init_slots(input_dev, MAX_TOUCH_ID, INPUT_MT_DIRECT);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);
	ret = input_register_device(input_dev);
	   if (ret) {
		LOG_INFO("Failed to register input device");
		input_free_device(input_dev);
		return NULL;
	}
	mutex_init(&ts_report_mutex[touch_id]);
	input_devs[touch_id] = input_dev;
	report_point_mmap_addr_init();
	LOG_INFO("touch id %d success register input dev", touch_id);

	return input_devs[touch_id];
}
EXPORT_SYMBOL_GPL(register_xiaomi_input_dev);

void unregister_xiaomi_input_dev(s8 touch_id)
{
	struct input_dev *input_dev = input_devs[touch_id];

	if (!input_dev) {
		LOG_ERROR("input device is NULL, return");
		return;
	}

	input_unregister_device(input_dev);

	input_devs[touch_id] = NULL;

	report_point_mmap_addr_release();
	LOG_INFO("touch id %d success unregister input dev", touch_id);

	return;
}
EXPORT_SYMBOL_GPL(unregister_xiaomi_input_dev);

struct input_dev *register_xiaomi_stylus_input_dev(s8 touch_id, int max_x, int max_y, ic_product_code code)
{
	int ret;
	struct input_dev *stylus_input_dev = NULL;
	static char name[MAX_TOUCH_PANEL_COUNT][DEVICE_NAME_LEN_MAX];
	static char phys[MAX_TOUCH_PANEL_COUNT][DEVICE_NAME_LEN_MAX];

	if (IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_ERROR("touch id %d is invalid", touch_id);
		return NULL;
	}

	if (stylus_input_devs[touch_id] != NULL) {
		return stylus_input_devs[touch_id];
	}

	stylus_input_dev = input_allocate_device();

	if (!stylus_input_dev) {
		LOG_INFO("Failed to allocate input device %d!", touch_id);
		return NULL;
	}

	snprintf(name[touch_id], DEVICE_NAME_LEN_MAX, "Xiaomi_Touch_Stylus_Input_%d", touch_id);
	snprintf(phys[touch_id], DEVICE_NAME_LEN_MAX, "Xiaomi_Touch_Stylus_Device_%d", touch_id);
	LOG_INFO("xiaomi allocat !!! the device is %s, %s",name[touch_id], phys[touch_id]);
	stylus_input_dev->name = name[touch_id];
	stylus_input_dev->phys = phys[touch_id];

	__set_bit(EV_SYN, stylus_input_dev->evbit);
	__set_bit(EV_KEY, stylus_input_dev->evbit);
	__set_bit(EV_ABS, stylus_input_dev->evbit);

	__set_bit(BTN_TOUCH, stylus_input_dev->keybit);
	__set_bit(BTN_TOOL_PEN, stylus_input_dev->keybit);
	__set_bit(KEY_WAKEUP, stylus_input_dev->keybit);

	__set_bit(INPUT_PROP_DIRECT, stylus_input_dev->propbit);
	input_set_abs_params(stylus_input_dev, ABS_X, 0, max_x, 0, 0);
	input_set_abs_params(stylus_input_dev, ABS_Y, 0, max_y, 0, 0);
	input_set_abs_params(stylus_input_dev, ABS_PRESSURE, 0, 8191, 0, 0);
	input_set_abs_params(stylus_input_dev, ABS_DISTANCE, 0, 1, 0, 0);
	input_set_abs_params(stylus_input_dev, ABS_TILT_X, -60, 60, 0, 0);
	input_set_abs_params(stylus_input_dev, ABS_TILT_Y, -60, 60, 0, 0);
	ret = input_register_device(stylus_input_dev);
	   if (ret) {
		LOG_INFO("Failed to register input device");
		input_free_device(stylus_input_dev);
		return NULL;
	}

	stylus_input_devs[touch_id] = stylus_input_dev;
	report_point_mmap_addr_init();
	LOG_INFO("touch id %d success register input dev", touch_id);

	return stylus_input_devs[touch_id];
}
EXPORT_SYMBOL_GPL(register_xiaomi_stylus_input_dev);

void unregister_xiaomi_stylus_input_dev(s8 touch_id)
{
	struct input_dev *input_dev = stylus_input_devs[touch_id];

	if (!input_dev) {
		LOG_ERROR("input device is NULL, return");
		return;
	}

	input_unregister_device(input_dev);

	stylus_input_devs[touch_id] = NULL;

	report_point_mmap_addr_release();
	LOG_INFO("touch id %d success unregister stylus input dev", touch_id);

	return;
}
EXPORT_SYMBOL_GPL(unregister_xiaomi_stylus_input_dev);


static struct input_dev *report_finger_event(s8 touch_id, hal_report_piont_t *report_point)
{
	static u16 finger_report_point_bitmap = 0;
	struct input_dev *input_dev = input_devs[touch_id];
	if (input_dev == NULL) {
		LOG_INFO("touch id %d input device is empty ,fail to report!", touch_id);
		return NULL;
	}
	if (report_point->finger.point_action != ACTION_UP) {
		input_report_key(input_dev, BTN_TOUCH, 1);
		input_report_key(input_dev, BTN_TOOL_FINGER, 1);
		input_mt_slot(input_dev, report_point->finger.prop[FINGER_SLOT]);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 1);
		input_report_abs(input_dev, ABS_MT_POSITION_X, report_point->finger.x);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, report_point->finger.y);
		input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, report_point->finger.prop[FINGER_MAJOR]);
		input_report_abs(input_dev, ABS_MT_TOUCH_MINOR, report_point->finger.prop[FINGER_MINOR]);
		__set_bit(report_point->finger.prop[FINGER_SLOT], (unsigned long *)&finger_report_point_bitmap);
	} else {
		input_mt_slot(input_dev, report_point->finger.prop[FINGER_SLOT]);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
		__clear_bit(report_point->finger.prop[FINGER_SLOT], (unsigned long *)&finger_report_point_bitmap);
	}
	if (finger_report_point_bitmap == 0) {
		input_report_key(input_dev, BTN_TOUCH, 0);
		input_report_key(input_dev, BTN_TOOL_FINGER, 0);
	}
	return input_dev;
}

static struct input_dev *report_stylus_event(s8 touch_id, hal_report_piont_t *report_point)
{
	struct input_dev *stylus_input_dev = stylus_input_devs[touch_id];
	if (stylus_input_dev == NULL) {
		LOG_INFO("touch id %d input device is empty ,fail to report!", touch_id);
		return NULL;
	}
	if (report_point->stylus.point_action != ACTION_UP) {
		input_report_key(stylus_input_dev, BTN_TOUCH, 1);
		input_report_key(stylus_input_dev, BTN_TOOL_PEN, 1);
		input_report_abs(stylus_input_dev, ABS_X, report_point->stylus.x);
		input_report_abs(stylus_input_dev, ABS_Y, report_point->stylus.y);
		input_report_abs(stylus_input_dev, ABS_PRESSURE, report_point->stylus.prop[STYLUS_PRESSURE]);
		input_report_abs(stylus_input_dev, ABS_DISTANCE, report_point->stylus.prop[STYLUS_DISTANCE]);
		input_report_abs(stylus_input_dev, ABS_TILT_X, report_point->stylus.prop[STYLUS_TILT_X]);
		input_report_abs(stylus_input_dev, ABS_TILT_Y, report_point->stylus.prop[STYLUS_TILT_Y]);
	} else {
		input_report_abs(stylus_input_dev, ABS_X, 0);
		input_report_abs(stylus_input_dev, ABS_Y, 0);
		input_report_abs(stylus_input_dev, ABS_PRESSURE, 0);
		input_report_abs(stylus_input_dev, ABS_DISTANCE, 0);
		input_report_abs(stylus_input_dev, ABS_TILT_X, 0);
		input_report_abs(stylus_input_dev, ABS_TILT_Y, 0);
		input_report_key(stylus_input_dev, BTN_TOUCH, 0);
		input_report_key(stylus_input_dev, BTN_TOOL_PEN, 0);
	}
	return stylus_input_dev;
}

int report_touch_event(s8 touch_id, u8 event_count)
{
	struct input_dev *input_dev = NULL;
	struct input_dev *stylus_input_dev = NULL;
	hal_report_piont_t *event = (hal_report_piont_t *)report_point_mmap_base;
	int index = 0;

	if (IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_ERROR("touch id %d is invalid", touch_id);
		return -1;
	}
	if (event == NULL) {
		LOG_ERROR("event memory is NULL");
		return -1;
	}
	mutex_lock(&ts_report_mutex[touch_id]);

	for (index = 0; index < event_count; index++) {
		if (event[index].input_style == INPUT_STYLE_FINGER) {
			input_dev = report_finger_event(touch_id, &event[index]);
		} else if (event[index].input_style == INPUT_STYLE_STYLUS) {
			stylus_input_dev = report_stylus_event(touch_id, &event[index]);
		} else {
			LOG_ERROR("input style %d is invalid", event[index].input_style);
		}
		cond_resched();
	}
	if (input_dev != NULL) {
		input_sync(input_dev);
	}
	if (stylus_input_dev != NULL) {
		input_sync(stylus_input_dev);
	}
	mutex_unlock(&ts_report_mutex[touch_id]);
	return 0;
}
