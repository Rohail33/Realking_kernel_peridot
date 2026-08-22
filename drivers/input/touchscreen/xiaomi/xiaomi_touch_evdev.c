#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/time.h>
#include <linux/sysfs.h>
#include <linux/rtc.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include "xiaomi_touch.h"

#define LAST_TOUCH_EVENTS_MAX	(512)

#define INPUT_EVENT_TIME_LINE_BUF_SIZE  (120)
#define NO_EVENT_TIME_BUF_SIZE  (20)
#define CHANNEL_COUNT    10
#define APP_NAME_SIZE    64

#define MAX_NAME_LENGTH (32)
#define DEFAULT_INPUT_DEVICE_NAME "unknown input device"

#pragma pack(1)
typedef struct input_event_time_line {
	int8_t noEventTimeCount;
	int64_t startInterruptTime[NO_EVENT_TIME_BUF_SIZE];
	int64_t endInterruptTime[NO_EVENT_TIME_BUF_SIZE];
	int64_t startThpTime[NO_EVENT_TIME_BUF_SIZE];
	int64_t endThpTime[NO_EVENT_TIME_BUF_SIZE];
	int64_t eventTime;
	int32_t eventId;
	int64_t readTime;
	int64_t enterIqTime;
	int64_t deliveryTime;
	int8_t appCount;
	int8_t appNeedCount;
	char appPackageName[CHANNEL_COUNT][APP_NAME_SIZE];
	int64_t startAppTime[CHANNEL_COUNT];
	int64_t endAppTime[CHANNEL_COUNT];
	int64_t gpuCompletedTime;
	int64_t presentTime;
} input_event_time_line_t;
#pragma pack()

enum touch_state {
	EVENT_INIT,
	EVENT_DOWN,
	EVENT_UP,
};

struct touch_event {
	u32 slot;
	char name[MAX_NAME_LENGTH];
	enum touch_state state;
	struct timespec64 touch_time;
};

struct last_touch_event {
	int head;
	struct touch_event touch_event_buf[LAST_TOUCH_EVENTS_MAX];
};

static int input_event_time_line_index = 0;
static input_event_time_line_t *input_event_time_line_base = NULL;
static xiaomi_touch_t *xiaomi_touch;
static struct proc_dir_entry *last_touch_events_pde = NULL;
static struct last_touch_event last_touch_events;
static int slot;
static int event_state[MAX_TOUCH_ID] = {0};
static void last_touch_events_collect(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	int state;

	if (code == ABS_MT_SLOT)
		slot = value;
	if (code == ABS_MT_TRACKING_ID) {
		state = (value == -1 ? 0 : 1);
		if (slot >= MAX_TOUCH_ID || event_state[slot] == state)
			return;
		event_state[slot] = state;
		if (handle->dev->name != NULL) {
			strncpy(last_touch_events.touch_event_buf[last_touch_events.head].name, handle->dev->name, MAX_NAME_LENGTH);
		} else {
			strncpy(last_touch_events.touch_event_buf[last_touch_events.head].name, DEFAULT_INPUT_DEVICE_NAME, MAX_NAME_LENGTH);
		}
		last_touch_events.touch_event_buf[last_touch_events.head].name[MAX_NAME_LENGTH - 1] = '\0';
		last_touch_events.touch_event_buf[last_touch_events.head].state = !!state ? EVENT_DOWN : EVENT_UP;
		last_touch_events.touch_event_buf[last_touch_events.head].slot = slot;
		ktime_get_real_ts64(&last_touch_events.touch_event_buf[last_touch_events.head].touch_time);
		last_touch_events.head++;
		last_touch_events.head &= LAST_TOUCH_EVENTS_MAX - 1;
	}
}

static void print_version_info_in_last_touch_events(struct seq_file *m) {
	s8 touch_id = 0;
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = NULL;

	for (touch_id = 0; touch_id < MAX_TOUCH_PANEL_COUNT; touch_id++) {
		xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
		if (xiaomi_touch_driver_param == NULL)
			continue;
		seq_printf(m, "touch id %d:\n", touch_id);
		if (strlen(xiaomi_touch_driver_param->hardware_param.fw_version) == 0 && xiaomi_touch_driver_param->hardware_operation.ic_get_fw_version)
			xiaomi_touch_driver_param->hardware_operation.ic_get_fw_version(xiaomi_touch_driver_param->hardware_param.fw_version);
		seq_printf(m, "    fw version: %s\n", xiaomi_touch_driver_param->hardware_param.fw_version);
		seq_printf(m, "    driver version: %s\n", xiaomi_touch_driver_param->hardware_param.driver_version);
		seq_printf(m, "    hal version: %s\n", xiaomi_touch_driver_param->hal_version);
		seq_printf(m, "    xiaomi-touch version: %s\n", XIAOMI_TOUCH_VERSION);
		seq_printf(m, "    lockdown info: 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X\n\n",
				xiaomi_touch_driver_param->hardware_param.lockdown_info[0], xiaomi_touch_driver_param->hardware_param.lockdown_info[1],
				xiaomi_touch_driver_param->hardware_param.lockdown_info[2], xiaomi_touch_driver_param->hardware_param.lockdown_info[3],
				xiaomi_touch_driver_param->hardware_param.lockdown_info[4], xiaomi_touch_driver_param->hardware_param.lockdown_info[5],
				xiaomi_touch_driver_param->hardware_param.lockdown_info[6], xiaomi_touch_driver_param->hardware_param.lockdown_info[7]);
	}
}

static void *event_start(struct seq_file *m, loff_t *p)
{
	int pos = 0;

	if (*p >= LAST_TOUCH_EVENTS_MAX)
		return NULL;

	if (*p == 0) {
		print_version_info_in_last_touch_events(m);
	}

	pos = (last_touch_events.head + *p) & (LAST_TOUCH_EVENTS_MAX - 1);
	return &last_touch_events.touch_event_buf[pos];
}

static void *event_next(struct seq_file *m, void *v, loff_t *p)
{
	int pos = 0;
	if (++*p >= LAST_TOUCH_EVENTS_MAX)
		return NULL;
	pos = (last_touch_events.head + *p) & (LAST_TOUCH_EVENTS_MAX - 1);
	return &last_touch_events.touch_event_buf[pos];
}

static int32_t event_show(struct seq_file *m, void *v)
{
	struct touch_event *event_info = (struct touch_event *)v;
	struct rtc_time tm;

	if (event_info->state == EVENT_INIT)
		return 0;
	rtc_time64_to_tm(event_info->touch_time.tv_sec, &tm);
	seq_printf(m, "%d-%02d-%02d %02d:%02d:%02d.%09lu UTC Finger %s (%2d) %s\n",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, event_info->touch_time.tv_nsec,
		event_info->name, event_info->slot,
		event_info->state == EVENT_DOWN ? "P" : "R");
	return 0;
}

void event_stop(struct seq_file *m, void *v) {}

const struct seq_operations last_touch_events_seq_ops = {
	.start = event_start,
	.stop = event_stop,
	.next = event_next,
	.show = event_show,
};

void add_input_event_timeline_before_event_time(int type, u64 frame_count, s64 start_time, s64 end_time)
{
	static u64 old_frame_count = -1;
	input_event_time_line_t *input_event_time_line = NULL;
	int index = 0;
	int i = 0;

	if (!input_event_time_line_base)
		return;
	input_event_time_line = &input_event_time_line_base[input_event_time_line_index];
	if (type == 0) { /* irq start and end time */
		if (input_event_time_line->noEventTimeCount >= NO_EVENT_TIME_BUF_SIZE) {
			for (i = 1; i < NO_EVENT_TIME_BUF_SIZE; i++) {
				input_event_time_line->startInterruptTime[i - 1] = input_event_time_line->startInterruptTime[i];
				input_event_time_line->endInterruptTime[i - 1] = input_event_time_line->endInterruptTime[i];
				input_event_time_line->startThpTime[i - 1] = input_event_time_line->startThpTime[i];
				input_event_time_line->endThpTime[i - 1] = input_event_time_line->endThpTime[i];
			}
			index = NO_EVENT_TIME_BUF_SIZE - 1;
		} else {
			index = input_event_time_line->noEventTimeCount;
		}
		old_frame_count = frame_count;
		input_event_time_line->startInterruptTime[index] = start_time;
		input_event_time_line->endInterruptTime[index] = end_time;
		input_event_time_line->startThpTime[index] = 0;
		input_event_time_line->endThpTime[index] = 0;
		input_event_time_line->noEventTimeCount++;
		if (input_event_time_line->noEventTimeCount >= NO_EVENT_TIME_BUF_SIZE) {
			input_event_time_line->noEventTimeCount = NO_EVENT_TIME_BUF_SIZE;
		}
	} else { /* thp start and end time */
		index = input_event_time_line->noEventTimeCount;
		if (frame_count > old_frame_count) {
			return;
		}
		index--;
		index -= (old_frame_count - frame_count);
		if (index >= NO_EVENT_TIME_BUF_SIZE || index < 0) {
			LOG_ERROR("type %d has error index %d", type, index);
			return;
		}
		input_event_time_line->startThpTime[index] = start_time;
		input_event_time_line->endThpTime[index] = end_time;
	}

}
EXPORT_SYMBOL_GPL(add_input_event_timeline_before_event_time);

static void add_input_event_event_time(struct input_handle *handle)
{
	static s64 old_event_time = 0;
	s64 event_time = handle->dev->timestamp[INPUT_CLK_MONO];
	input_event_time_line_t *input_event_time_line = NULL;

	if (!input_event_time_line_base)
		return;

	if (old_event_time == event_time)
		return;

	if (unlikely(event_time == 0)) {
		LOG_ERROR("xiaomi handler get time is 0, please reboot and retry!");
		return;
	}

	old_event_time = event_time;
	event_time = event_time  / 1000 * 1000;
	input_event_time_line = &input_event_time_line_base[input_event_time_line_index];

	if (input_event_time_line->noEventTimeCount >= NO_EVENT_TIME_BUF_SIZE) {
		LOG_ERROR("no point irq count is more than buf, maybe lose data!");
	}
	input_event_time_line->eventTime  = event_time;
	input_event_time_line->eventId = -1;
	input_event_time_line->appCount = 0;
	input_event_time_line->appNeedCount = 1;
	input_event_time_line->gpuCompletedTime = 0;
	input_event_time_line->presentTime = 0;
	LOG_INFO("add event time %lld in index %d", event_time, input_event_time_line_index);

	input_event_time_line_index++;
	if (input_event_time_line_index >= INPUT_EVENT_TIME_LINE_BUF_SIZE) {
		input_event_time_line_index = 0;
	}
	input_event_time_line_base[input_event_time_line_index].noEventTimeCount = 0;
}

static void xiaomitouch_input_event(struct input_handle *handle,
		unsigned int type, unsigned int code, int value)
{
	if (type == EV_KEY && (code == KEY_POWER || code == KEY_VOLUMEDOWN || code ==  KEY_VOLUMEUP))
		LOG_INFO("keycode:%d,value:%d", code, value);
	if (type == EV_ABS) {
		last_touch_events_collect(handle, type, code, value);
		add_input_event_event_time(handle);
	}
}

static int xiaomitouch_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "xiaomitouch";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	handle = NULL;
	return error;
}

static void xiaomitouch_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
	handle = NULL;
}

static const struct input_device_id xiaomitouch_ids[] = {
	/* multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y)
		},
	},
	/* touchpad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y)
		},
	},
	/* Keypad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ },
};

static struct input_handler xiaomitouch_input_handler = {
	.event		= xiaomitouch_input_event,
	.connect	= xiaomitouch_input_connect,
	.disconnect	= xiaomitouch_input_disconnect,
	.name		= "xiaomi-touch",
	.id_table	= xiaomitouch_ids,
};

bool init_input_event_timeline(void)
{
	/* alloc input event time line memory */
	if (input_event_time_line_base)
		return true;

	xiaomi_touch->input_event_time_line_phy_base = 0;
	xiaomi_touch->input_event_time_line_mmap_base = kzalloc_retry(PAGE_SIZE * 45, 3);
	if (!xiaomi_touch->input_event_time_line_mmap_base) {
		LOG_ERROR("alloc input event time line memory failed!");
		return false;
	} else {
		LOG_INFO("init input event time line base %p", xiaomi_touch->input_event_time_line_mmap_base);
		input_event_time_line_base = (input_event_time_line_t *)(xiaomi_touch->input_event_time_line_mmap_base);
		xiaomi_touch->input_event_time_line_phy_base = virt_to_phys(xiaomi_touch->input_event_time_line_mmap_base);
	}
	return true;
}

void release_input_event_timeline(void)
{
	if (!input_event_time_line_base)
		return;

	kzalloc_free(xiaomi_touch->input_event_time_line_mmap_base);
	xiaomi_touch->input_event_time_line_phy_base = 0;
	xiaomi_touch->input_event_time_line_mmap_base = NULL;
	input_event_time_line_base = NULL;
	input_event_time_line_index = 0;
	LOG_INFO("input event time line base release");
}

int xiaomi_touch_evdev_init(xiaomi_touch_t *_xiaomi_touch)
{
	int ret;

	LOG_ERROR("enter");
	xiaomi_touch = _xiaomi_touch;
	ret = input_register_handler(&xiaomitouch_input_handler);
	/* create last_touch_events */
	if (!last_touch_events_pde) {
		last_touch_events_pde = proc_create_seq("last_touch_events", 0644, NULL, &last_touch_events_seq_ops);
		if (!last_touch_events_pde) {
			LOG_ERROR("last_touch_events_proc create has error, exit!");
			return -1;
		}
		memset(&last_touch_events, 0, sizeof(struct last_touch_event));
	}
	return ret;
}

void xiaomi_touch_evdev_remove(void)
{
	LOG_ERROR("enter");
	release_input_event_timeline();
	input_unregister_handler(&xiaomitouch_input_handler);
	if (last_touch_events_pde) {
		proc_remove(last_touch_events_pde);
		last_touch_events_pde = NULL;
	}
}
