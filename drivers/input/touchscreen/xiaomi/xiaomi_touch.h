#ifndef __XIAOMI_TOUCH_H__
#define __XIAOMI_TOUCH_H__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <asm/atomic.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include "xiaomi_touch_type_common.h"

#define XIAOMI_TOUCH_VERSION    "2024.08.07-01"

#define BTN_INFO 0x152

#define LOG_TAG					"[MI_TP"
#define COMMON_DATA_BUF_SIZE    10
#define MAX_TOUCH_PANEL_COUNT   2
#define MAX_TOUCH_ID 10
#define HAL_VERSION_LENGTH  128
#define TOUCH_THP_SUPPORT 1
#define TOUCH_FOD_SUPPORT 1
#define GESTURE_LONGPRESS_EVENT 0x01
#define GESTURE_SINGLETAP_EVENT 0x02
#define GESTURE_DOUBLETAP_EVENT 0x04

#ifdef TOUCH_SENSORHUB_SUPPORT
#define GESTURE_ID_SENSORHUB_ERR 0x100
#endif

/*
 * If the temperature >= 100 degrees or <= -100 degrees,
 *   it is considered invalid temperature.
 */
#define INVAILD_TEMPERATURE			1000

#define IS_TOUCH_ID_INVALID(touch_id) (touch_id < 0 || touch_id >= MAX_TOUCH_PANEL_COUNT)

enum touch_doze_analysis {
	TOUCH_0 = 0,
	TOUCH_1,
	TOUCH_2,
	TOUCH_3,
	TOUCH_4,
	TOUCH_5,
	TOUCH_6,
	TOUCH_7,
	TOUCH_8,
	TOUCH_9,
	TOUCH_10,
	TOUCH_11,
	TOUCH_12,
	TOUCH_13,
};

enum MI_TP_LOG_LEVEL {
	MI_TP_LOG_ALWAYS = 0,
	MI_TP_LOG_ERROR,
	MI_TP_LOG_WARNING,
	MI_TP_LOG_INFO,
	MI_TP_LOG_DEBUG,
	MI_TP_LOG_VERBOSE,
};


enum charge_status {
	NOT_CHARGING = 0,
	CHARGING = 1,
	WIRED_CHARGING = 2,
	WIRELESS_CHARGING = 4,
};

typedef enum {
	ST_PRI = 0x0010,
	ST_SEC = 0x0011,
	FOCAL_PRI = 0x0020,
	FOCAL_SEC = 0x0021,
	GOODIX_PRI = 0x0030,
	GOODIX_SEC = 0x0031,
	SYNA_PRI = 0x0040,
	SYNA_SEC = 0x0041,
	NVT_PRI = 0x0050,
	HIMAX_PRI = 0x0060,
} ic_product_code;

extern enum MI_TP_LOG_LEVEL current_log_level;

#define LOG_ALWAYS(fmt, args...)\
		do {\
			if (current_log_level >= MI_TP_LOG_ALWAYS) {\
				pr_info(LOG_TAG "_A][%s:%d]: " fmt, __func__, __LINE__, ##args);\
			}\
		} while (0)

#define LOG_ERROR(fmt, args...)\
		do {\
			if (current_log_level >= MI_TP_LOG_ERROR) {\
				pr_info(LOG_TAG "_E][%s:%d]: " fmt, __func__, __LINE__, ##args);\
			}\
		} while (0)

#define LOG_WARNING(fmt, args...)\
		do {\
			if (current_log_level >= MI_TP_LOG_WARNING) {\
				pr_info(LOG_TAG "_W][%s:%d]: " fmt, __func__, __LINE__, ##args);\
			}\
		} while (0)

#define LOG_INFO(fmt, args...)\
		do {\
			if (current_log_level >= MI_TP_LOG_INFO) {\
				pr_info(LOG_TAG "_I][%s:%d]: " fmt, __func__, __LINE__, ##args);\
			}\
		} while (0)

#define LOG_DEBUG(fmt, args...)\
		do {\
			if (current_log_level >= MI_TP_LOG_DEBUG) {\
				pr_info(LOG_TAG "_D][%s:%d]: " fmt, __func__, __LINE__, ##args);\
			}\
		} while (0)

#define LOG_VERBOSE(fmt, args...)\
		do {\
			if (current_log_level >= MI_TP_LOG_VERBOSE) {\
				pr_info(LOG_TAG "_V][%s:%d]: " fmt, __func__, __LINE__, ##args);\
			}\
		} while (0)


#define XIAOMI_TOUCH_UTC_PRINT(tag) \
	do { \
		struct timespec64 ts; \
		struct tm tm; \
		ktime_get_real_ts64(&ts); \
		time64_to_tm(ts.tv_sec, 0, &tm); \
		LOG_INFO("%s [xiaomi_touch_utc] [%ld-%02d-%02d %02d:%02d:%02d.%06lu]\n", \
			tag, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, \
			tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec/1000); \
	} while(0)

enum poll_notify_type {
	COMMON_DATA_NOTIFY = 0,
	FRAME_DATA_NOTIFY,
	RAW_DATA_NOTIFY,
};

typedef struct private_data {
	struct list_head node;
	u8 mmap_area;
	s8 touch_id;
	wait_queue_head_t poll_wait_queue_head;
	wait_queue_head_t poll_wait_queue_head_for_cmd;
	wait_queue_head_t poll_wait_queue_head_for_frame;
	wait_queue_head_t poll_wait_queue_head_for_raw;

	atomic_t common_data_index;
	atomic_t frame_data_index;
	atomic_t raw_data_index;
} private_data_t;

typedef struct xiaomi_touch_data {
	s8 touch_id;

	atomic_t frame_data_buf_index;
	u32 frame_data_size;
	u8 frame_data_buf_size;
	void *frame_data_mmap_base;
	dma_addr_t frame_data_mmap_phy_base;

	atomic_t raw_data_buf_index;
	u32 raw_data_size;
	u8 raw_data_buf_size;
	void *raw_data_mmap_base;
	dma_addr_t raw_data_mmap_phy_base;

	struct list_head private_data_list;
	spinlock_t private_data_lock;

	atomic_t common_data_buf_index;
	common_data_t common_data_buf[COMMON_DATA_BUF_SIZE];
	struct mutex common_data_buf_lock;
	struct htc_ic_polldata* poll_data;

	struct workqueue_struct *event_wq;
#if defined(CONFIG_DRM)
	bool is_suspend;
	struct work_struct suspend_work;
	struct work_struct resume_work;
	struct delayed_work panel_notifier_register_work;
	struct device *dev;
	void *notifier_cookie;
	int panel_event_notifier_tag;
	int panel_event_notifier_client;
#endif
#ifdef TOUCH_SENSORHUB_SUPPORT
	bool ssh_status;
#endif
	wait_queue_head_t temp_detect_wait_queue;
	atomic_t temp_detect_ready;
} xiaomi_touch_data_t;

typedef struct hardware_operation {
	int (*ic_self_test)(char *type, int *result);
	int (*ic_data_collect)(char *buf, int *length);
	int (*ic_get_lockdown_info)(u8 lockdown_info[8]);
	int (*ic_get_fw_version)(char fw_version[64]);

	void (*set_mode_value)(int mode, int *value);
	void (*get_mode_value)(common_data_t *common_data);
	void (*get_mode_all)(common_data_t *common_data);
	void (*reset_mode)(common_data_t *common_data);
	void (*ic_switch_mode)(u8 gesture_type);
	void (*ic_enable_irq)(bool enable);
	void (*cmd_update_func)(long mode_update_flag, int mode_value[DATA_MODE_33]);
	void (*set_mode_long_value)(s32 value[], int length);

	int (*palm_sensor_write)(int on);
	int (*enable_touch_raw)(int en);
	u8 (*panel_vendor_read)(void);
	u8 (*panel_color_read)(void);
	u8 (*panel_display_read)(void);
	char (*touch_vendor_read)(void);
	void (*get_touch_ic_buffer)(u64 address, u8 *buf);
	int (*touch_doze_analysis)(int value);
	int (*touch_log_level_control)(bool value);
	int (*touch_log_level_control_v2)(int value);
	int (*htc_ic_setModeValue)(common_data_t *common_data);
	int (*htc_ic_getModeValue)(common_data_t *common_data);

	int (*ic_resume_suspend)(bool is_resume, u8 gesture_type);
	void (*ic_set_charge_state)(int status);
	void (*ic_set_fod_value)(s32 value[], int length);
	void (*xiaomi_touch_fod_test)(int value);
	int (*set_thermal_temp)(int temp, bool force);
} hardware_operation_t;

typedef struct xiaomi_touch_driver_param {
	s8 touch_id;
	char hal_version[HAL_VERSION_LENGTH];
	hardware_param_t hardware_param;
	hardware_operation_t hardware_operation;
} xiaomi_touch_driver_param_t;

typedef struct xiaomi_touch {
	u16 use_count;
	u8 panel_register_mask;
	xiaomi_touch_data_t xiaomi_touch_data[MAX_TOUCH_PANEL_COUNT];
	xiaomi_touch_driver_param_t xiaomi_touch_driver_param[MAX_TOUCH_PANEL_COUNT];
	int charging_status;
	struct notifier_block power_supply_notifier;
	struct work_struct power_supply_work;
	/* memory for input event time line */
	void *input_event_time_line_mmap_base;
	dma_addr_t input_event_time_line_phy_base;
} xiaomi_touch_t;

#pragma pack(1)
typedef struct htc_ic_polldata {
	uint16_t protocol_version;
	uint16_t frame_len;
	uint16_t ic_fw_v;
	uint16_t ic_name;
	uint16_t ic_project_name;
	uint16_t ic_supplier_name;
	uint8_t pitch_size_y;
	uint8_t pitch_size_x;
	uint8_t numCol;
	uint8_t numRow;
	uint16_t packaging_factory;
	uint16_t wafe_factory;
	uint16_t x_resolution;
	uint16_t y_resolution;
	uint64_t lockdown_info;
	uint8_t frame_data_type;
	uint16_t mutual_len;
	uint16_t slef1_len;
	uint16_t slef2_len;
} htc_ic_polldata_t;
#pragma pack()

/* export for other module and xiaomi_touch module */
int notify_gesture_single_tap(void);
int notify_gesture_double_tap(void);
int update_fod_press_status(int value);
struct class *get_xiaomi_touch_class(void);
int update_palm_sensor_value(int value);
void *get_raw_data_base(s8 touch_id);
void notify_raw_data_update(s8 touch_id);
void add_common_data_to_buf(s8 touch_id, enum common_data_cmd cmd, enum common_data_mode mode, int length, int *data);
int register_touch_panel(struct device *dev, s8 touch_id, hardware_param_t *hardware_param, hardware_operation_t *hardware_operation);
void unregister_touch_panel(s8 touch_id);
void xiaomi_register_panel_notifier(struct device *dev, s8 touch_id,
		int panel_event_notifier_tag, int panel_event_notifier_client);
void xiaomi_unregister_panel_notifier(struct device *dev, s8 touch_id);
void schedule_resume_suspend_work(s8 touch_id, bool resume_work);
void driver_update_touch_mode(s8 touch_id, int touch_mode[DATA_MODE_33], long update_mode_mask);
u8 xiaomi_get_gesture_type(s8 touch_id);
int driver_get_touch_mode(s8 touch_id, int mode);
#ifdef TOUCH_SENSORHUB_SUPPORT
void xiaomi_set_sensorhub_nonui_enable(bool status);
bool xiaomi_get_sensorhub_nonui_enable(void);
void xiaomi_notify_sensorhub_enable(s8 touch_id, bool status);
bool xiaomi_get_sensorhub_status(s8 touch_id);
#endif

/* use in xiaomi_touch module */
#ifdef TOUCH_KNOCK_SUPPORT
int knock_node_init(void);
void knock_node_release(void);
void register_frame_count_change_listener(void *listener);
void update_knock_data(u8 *buf, int size, int frame_id);
void knock_data_notify(void);
#endif
void notify_xiaomi_touch(xiaomi_touch_data_t *xiaomi_touch_data, enum poll_notify_type type);
void update_get_ic_current_value(common_data_t *common_data);
void schedule_resume_suspend_work(s8 touch_id, bool resume_work);

void sendnlmsg(char message);

void *kzalloc_retry(size_t size, int retry);
void kzalloc_free(void *p);
void *kvzalloc_retry(size_t size, int retry);
void kvzalloc_free(void *p);

xiaomi_touch_data_t *get_xiaomi_touch_data(s8 touch_id);
xiaomi_touch_driver_param_t *get_xiaomi_touch_driver_param(s8 touch_id);

int xiaomi_touch_create_proc(xiaomi_touch_driver_param_t *xiaomi_touch_driver_param);
int xiaomi_touch_remove_proc(s8 touch_id);

int xiaomi_touch_operation_init(xiaomi_touch_t *temp_xiaomi_touch);
int xiaomi_touch_operation_remove(void);

int xiaomi_touch_init_touch_mode(s8 touch_id, struct device *dev);
int xiaomi_touch_mode(private_data_t *client_private_data, u32 user_size, unsigned long arg);

int xiaomi_touch_sys_init(void);
int xiaomi_touch_sys_remove(void);
int xiaomi_touch_evdev_init(xiaomi_touch_t *xiaomi_touch);
void add_input_event_timeline_before_event_time(int type, u64 frame_count, s64 start_time, s64 end_time);
bool init_input_event_timeline(void);
void release_input_event_timeline(void);
void xiaomi_touch_evdev_remove(void);
struct input_dev *register_xiaomi_input_dev(s8 touch_id, int max_x, int max_y, ic_product_code code);
void unregister_xiaomi_input_dev(s8 touch_id);
struct input_dev *register_xiaomi_stylus_input_dev(s8 touch_id, int max_x, int max_y, ic_product_code code);
void unregister_xiaomi_stylus_input_dev(s8 touch_id);
int report_touch_event(s8 touch_id, u8 event_count);
dma_addr_t get_report_point_info_phy_addr(void);
void enable_temperature_detection_func(bool is_resume);
int get_bms_temp_common(void);
#endif
