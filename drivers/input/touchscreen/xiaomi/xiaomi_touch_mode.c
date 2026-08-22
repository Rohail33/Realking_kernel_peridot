#include <linux/of.h>
#include <linux/device.h>
#include <linux/delay.h>
#include "xiaomi_touch.h"
#include "xiaomi_touch_type_common.h"

#define GRIP_RECT_NUM 12
#define GRIP_PARAMETER_NUM 8
#define MI_GRIP_PARAMETERS_SIZE 32
#define MI_TOUCH_MODE_PARAMETERS_SIZE 5
#define MI_CORNERFILTER_AREA_STEP_SIZE 4
#define VALUE_TYPE_SIZE 6

typedef struct {
	unsigned int game_mode[MI_TOUCH_MODE_PARAMETERS_SIZE];
	unsigned int active_mode[MI_TOUCH_MODE_PARAMETERS_SIZE];
	unsigned int up_threshold[MI_TOUCH_MODE_PARAMETERS_SIZE];
	unsigned int tolerance[MI_TOUCH_MODE_PARAMETERS_SIZE];
	unsigned int aim_sensitivity[MI_TOUCH_MODE_PARAMETERS_SIZE];
	unsigned int tap_stability[MI_TOUCH_MODE_PARAMETERS_SIZE];
	unsigned int edge_filter[MI_TOUCH_MODE_PARAMETERS_SIZE];
	unsigned int panel_orien[MI_TOUCH_MODE_PARAMETERS_SIZE];
	unsigned int report_rate[MI_TOUCH_MODE_PARAMETERS_SIZE];
	unsigned int expert_mode[MI_TOUCH_MODE_PARAMETERS_SIZE];
	unsigned int cornerfilter_area_step[MI_CORNERFILTER_AREA_STEP_SIZE];
	unsigned int cornerzone_filter_hor1[MI_GRIP_PARAMETERS_SIZE];
	unsigned int cornerzone_filter_hor2[MI_GRIP_PARAMETERS_SIZE];
	unsigned int cornerzone_filter_ver[MI_GRIP_PARAMETERS_SIZE];
	unsigned int deadzone_filter_hor[MI_GRIP_PARAMETERS_SIZE];
	unsigned int deadzone_filter_ver[MI_GRIP_PARAMETERS_SIZE];
	unsigned int edgezone_filter_hor[MI_GRIP_PARAMETERS_SIZE];
	unsigned int edgezone_filter_ver[MI_GRIP_PARAMETERS_SIZE];
} xiaomi_game_mode_board_data_t;

enum xiaomi_touch_mode_dts_index {
	MI_DTS_GET_MAX_INDEX = 0,
	MI_DTS_GET_MIN_INDEX,
	MI_DTS_GET_DEF_INDEX,
	MI_DTS_SET_CUR_INDEX,
	MI_DTS_GET_CUR_INDEX,
};

static int touch_mode[MAX_TOUCH_PANEL_COUNT][DATA_MODE_33][VALUE_TYPE_SIZE];
static xiaomi_game_mode_board_data_t game_mode_bdata[MAX_TOUCH_PANEL_COUNT];
static struct work_struct switch_mode_work[MAX_TOUCH_PANEL_COUNT];
static struct workqueue_struct *cmd_update_wq = NULL;
static struct work_struct cmd_update_work[MAX_TOUCH_PANEL_COUNT];
static struct work_struct grid_update_work[MAX_TOUCH_PANEL_COUNT];
static s32 grid_updata_value[CMD_DATA_BUF_SIZE];
static int grid_update_length = 0;
#ifdef TOUCH_SENSORHUB_SUPPORT
static s32 fod_value[FOD_VALUE_LEN];
static bool sensorhub_nonui_enable = true;
#define GESTURE_TOUCH_AND_HOLD_UP_EVENT 0x81
#endif

void driver_update_touch_mode(s8 touch_id, int _touch_mode[DATA_MODE_33], long update_mode_mask)
{
	int i = 0;
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);

	if (!xiaomi_touch_data) {
		LOG_ERROR("xiaomi_touch_data is NULL");
		return;
	}

	for (i = 0; i < DATA_MODE_33; i++) {
		if (update_mode_mask & (1 << i)) {
			touch_mode[touch_id][i][SET_CUR_VALUE] = _touch_mode[i];
			if (i > DATA_MODE_8) {
				touch_mode[touch_id][i][GET_CUR_VALUE] = _touch_mode[i];
			}
		}
	}
	if (update_mode_mask & 0x7E) {
		if (cmd_update_wq) {
			queue_work(cmd_update_wq, &cmd_update_work[touch_id]);
		}
		LOG_INFO("touch id %d cmd data update", touch_id);
	}
	if (update_mode_mask & ~(0x7E)) {
		queue_work(xiaomi_touch_data->event_wq, &switch_mode_work[touch_id]);
		LOG_INFO("touch id %d ic switch mode", touch_id);
	}
}
EXPORT_SYMBOL_GPL(driver_update_touch_mode);

int driver_get_touch_mode(s8 touch_id, int mode)
{
	if (touch_id < 0 || touch_id >= MAX_TOUCH_PANEL_COUNT || mode < 0 || mode >= DATA_MODE_33)
		return -1;
	return touch_mode[touch_id][mode][GET_CUR_VALUE];
}
EXPORT_SYMBOL_GPL(driver_get_touch_mode);

u8 xiaomi_get_gesture_type(s8 touch_id)
{
	u8 tmp_value = 0;
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);

	if (xiaomi_touch_data == NULL)
		return -EIO;
	if (touch_mode[touch_id][DATA_MODE_17][GET_CUR_VALUE] == 2)
		return tmp_value;

	if (touch_mode[touch_id][DATA_MODE_11][GET_CUR_VALUE] &&
			!touch_mode[touch_id][DATA_MODE_17][GET_CUR_VALUE])
		tmp_value |= GESTURE_SINGLETAP_EVENT;

	if (touch_mode[touch_id][DATA_MODE_14][GET_CUR_VALUE] &&
			!touch_mode[touch_id][DATA_MODE_17][GET_CUR_VALUE])
		tmp_value |= GESTURE_DOUBLETAP_EVENT;

	if (touch_mode[touch_id][DATA_MODE_10][GET_CUR_VALUE]) {
		tmp_value |= GESTURE_LONGPRESS_EVENT;
		if (touch_mode[touch_id][DATA_MODE_16][GET_CUR_VALUE] &&
				!touch_mode[touch_id][DATA_MODE_17][GET_CUR_VALUE])
			tmp_value |= GESTURE_SINGLETAP_EVENT;
	}
	LOG_INFO("gesture type:%2x\n", tmp_value);
	return tmp_value;
}
EXPORT_SYMBOL_GPL(xiaomi_get_gesture_type);

#ifdef TOUCH_SENSORHUB_SUPPORT
void xiaomi_set_sensorhub_nonui_enable(bool status)
{
	LOG_INFO("status:%d", status);
	sensorhub_nonui_enable = status;
}
EXPORT_SYMBOL_GPL(xiaomi_set_sensorhub_nonui_enable);

bool xiaomi_get_sensorhub_nonui_enable(void)
{
	return sensorhub_nonui_enable;
}
EXPORT_SYMBOL_GPL(xiaomi_get_sensorhub_nonui_enable);

static DEFINE_MUTEX(notify_ssh_mutex);
void xiaomi_notify_sensorhub_enable(s8 touch_id, bool status)
{
	int ssh_value = XIAOMI_TOUCH_SENSORHUB_DISABLE;
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);

	if (!xiaomi_touch_driver_param || !xiaomi_touch_data)
		return;
#ifdef CONFIG_TOUCH_FACTORY_BUILD
	LOG_INFO("don't enable sensorhub in factory build");
	return;
#endif
	if (xiaomi_get_sensorhub_nonui_enable() == 0) {
		LOG_INFO("don't support sensorhub nonui");
		return;
	}
	mutex_lock(&notify_ssh_mutex);
	if (status == xiaomi_touch_data->ssh_status) {
		LOG_INFO("ssh aleady in %d state", status);
		mutex_unlock(&notify_ssh_mutex);
		return;
	}
	LOG_INFO("sensorhub set state:%d ,get gesture type:%d", status, xiaomi_get_gesture_type(touch_id));
	if (status) {
		ssh_value = XIAOMI_TOUCH_SENSORHUB_ENABLE;
		if ((xiaomi_get_gesture_type(touch_id) & GESTURE_LONGPRESS_EVENT) != 0) {
			if (xiaomi_touch_driver_param->hardware_operation.ic_enable_irq) {
				xiaomi_touch_driver_param->hardware_operation.ic_enable_irq(false);
			}
			msleep(10);
			add_common_data_to_buf(0, SET_CUR_VALUE, DATA_MODE_27, 1, &ssh_value);
			xiaomi_touch_data->ssh_status = status;
			LOG_INFO("notify sensorhub enable %d", status);
		}
	} else {
		ssh_value = XIAOMI_TOUCH_SENSORHUB_DISABLE;
		add_common_data_to_buf(0, SET_CUR_VALUE, DATA_MODE_27, 1, &ssh_value);
		msleep(10);
		if (xiaomi_touch_driver_param->hardware_operation.ic_enable_irq) {
			xiaomi_touch_driver_param->hardware_operation.ic_enable_irq(true);
		}
		xiaomi_touch_data->ssh_status = status;
		LOG_INFO("notify sensorhub enable %d", status);
	}
	mutex_unlock(&notify_ssh_mutex);
}
EXPORT_SYMBOL_GPL(xiaomi_notify_sensorhub_enable);

bool xiaomi_get_sensorhub_status(s8 touch_id)
{
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);

	if (!xiaomi_touch_data)
		return 0;
	if (xiaomi_get_sensorhub_nonui_enable() == 0) {
		return 0;
	} else {
		return xiaomi_touch_data->ssh_status;
	}
}
EXPORT_SYMBOL_GPL(xiaomi_get_sensorhub_status);

#endif

static s8 get_work_touch_id(struct work_struct *work_array, struct work_struct *work)
{
	int i = 0;
	for (i = 0; i < MAX_TOUCH_PANEL_COUNT; i++) {
		if (&work_array[i] == work)
			return i;
	}
	return -1;
}

static void xiaomi_touch_grid_update_work(struct work_struct *work)
{
	s8 touch_id = get_work_touch_id(grid_update_work, work);
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
	int i = 0;
	int corner_filter_value = 0;

	LOG_INFO("touch id %d enter", touch_id);
	if (!xiaomi_touch_driver_param || !xiaomi_touch_data ||
			!xiaomi_touch_driver_param->hardware_operation.set_mode_long_value)
		return;

	if (xiaomi_touch_data->is_suspend) {
		LOG_ERROR("touch id %d is in suspend, don't update grip", touch_id);
		return;
	}

	if (grid_update_length != GRIP_RECT_NUM * GRIP_PARAMETER_NUM ||
			GRIP_RECT_NUM * GRIP_PARAMETER_NUM != 3 * MI_GRIP_PARAMETERS_SIZE) {
		LOG_ERROR("grid value length %d is invalid", grid_update_length);
		return;
	}

	if (touch_mode[touch_id][DATA_MODE_0][GET_CUR_VALUE]) {
		LOG_INFO("touch id %d is in game mode, set data from dts", touch_id);
		corner_filter_value = game_mode_bdata[touch_id].
				cornerfilter_area_step[touch_mode[touch_id][DATA_MODE_7][GET_CUR_VALUE]];
		for (i = 0; i < MI_GRIP_PARAMETERS_SIZE; i++) {
			if (touch_mode[touch_id][DATA_MODE_8][GET_CUR_VALUE]) {
				grid_updata_value[i] = game_mode_bdata[touch_id].deadzone_filter_hor[i];
				grid_updata_value[MI_GRIP_PARAMETERS_SIZE + i] = game_mode_bdata[touch_id].edgezone_filter_hor[i];
				if (touch_mode[touch_id][DATA_MODE_8][GET_CUR_VALUE] == 1) {
					grid_updata_value[MI_GRIP_PARAMETERS_SIZE * 2 + i] =
						(i == 4 || i == 5 || i == GRIP_PARAMETER_NUM * 2 + 4) ? corner_filter_value :
						(i == GRIP_PARAMETER_NUM * 2 + 3) ? xiaomi_touch_driver_param->hardware_param.y_resolution - corner_filter_value - 1 :
						game_mode_bdata[touch_id].cornerzone_filter_hor1[i];
				} else if (touch_mode[touch_id][DATA_MODE_8][GET_CUR_VALUE] == 3) {
					grid_updata_value[MI_GRIP_PARAMETERS_SIZE * 2 + i] =
						(i == GRIP_PARAMETER_NUM + 2 || i == GRIP_PARAMETER_NUM * 3 + 2) ? xiaomi_touch_driver_param->hardware_param.x_resolution -corner_filter_value - 1 :
						(i == GRIP_PARAMETER_NUM + 5) ? corner_filter_value :
						(i == GRIP_PARAMETER_NUM * 3 + 3) ? xiaomi_touch_driver_param->hardware_param.y_resolution - corner_filter_value - 1 :
						game_mode_bdata[touch_id].cornerzone_filter_hor2[i];
				}
			} else {
				grid_updata_value[i] = game_mode_bdata[touch_id].deadzone_filter_ver[i];
				grid_updata_value[MI_GRIP_PARAMETERS_SIZE + i] = game_mode_bdata[touch_id].edgezone_filter_ver[i];
				grid_updata_value[MI_GRIP_PARAMETERS_SIZE * 2 + i] = game_mode_bdata[touch_id].cornerzone_filter_ver[i];
			}
		}
	}
	xiaomi_touch_driver_param->hardware_operation.set_mode_long_value(grid_updata_value, grid_update_length);
	LOG_INFO("touch id %d set grid complete", touch_id);
}

#ifdef TOUCH_SENSORHUB_SUPPORT
static void xiaomi_set_fod_value(s8 touch_id)
{
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);

	LOG_INFO("touch id %d enter", touch_id);
	if (!xiaomi_touch_driver_param || !xiaomi_touch_data ||
			!xiaomi_touch_driver_param->hardware_operation.ic_set_fod_value)
		return;
	xiaomi_touch_driver_param->hardware_operation.ic_set_fod_value(fod_value, FOD_VALUE_LEN);
}
#endif

static void xiaomi_touch_cmd_update_work(struct work_struct *work)
{
	int i = 0;
	s8 touch_id = get_work_touch_id(cmd_update_work, work);
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
	long mode_update_flag = 0;
	int mode_value[DATA_MODE_33];
	LOG_INFO("touch id %d enter", touch_id);
	if (!xiaomi_touch_driver_param)
		return;

	for (i = 0; i < DATA_MODE_33; i++) {
		if (i <= DATA_MODE_8) {
			if (touch_mode[touch_id][i][SET_CUR_VALUE] > touch_mode[touch_id][i][GET_MAX_VALUE])
				touch_mode[touch_id][i][SET_CUR_VALUE] = touch_mode[touch_id][i][GET_MAX_VALUE];
			else if (touch_mode[touch_id][i][SET_CUR_VALUE] < touch_mode[touch_id][i][GET_MIN_VALUE])
				touch_mode[touch_id][i][SET_CUR_VALUE] = touch_mode[touch_id][i][GET_MIN_VALUE];
		}

		if (touch_mode[touch_id][i][GET_CUR_VALUE] != touch_mode[touch_id][i][SET_CUR_VALUE]) {
			touch_mode[touch_id][i][GET_CUR_VALUE] = touch_mode[touch_id][i][SET_CUR_VALUE];
			mode_update_flag |= 1 << i;
			LOG_INFO("mode: %d, set value: %d", i, touch_mode[touch_id][i][SET_CUR_VALUE]);
		}
		mode_value[i] = touch_mode[touch_id][i][GET_CUR_VALUE];
	}

	if (mode_update_flag && xiaomi_touch_driver_param->hardware_operation.cmd_update_func) {
		xiaomi_touch_driver_param->hardware_operation.cmd_update_func(mode_update_flag, mode_value);
	}

	if (mode_update_flag & ((1 << DATA_MODE_0) | (1 << DATA_MODE_8) | (1 << DATA_MODE_7))) {
		LOG_INFO("mode_update_flag: 0x%02lX", mode_update_flag);
		schedule_work(&grid_update_work[touch_id]);
	}

}

static void xiaomi_touch_switch_mode_work(struct work_struct *work)
{
	s8 touch_id = get_work_touch_id(switch_mode_work, work);
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
#ifdef	TOUCH_SENSORHUB_SUPPORT
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
#endif

	LOG_INFO("touch id %d enter", touch_id);

	if (!xiaomi_touch_driver_param)
		return;
#ifdef TOUCH_SENSORHUB_SUPPORT
	if (xiaomi_touch_data->is_suspend && xiaomi_touch_data->ssh_status) {
		LOG_INFO("touch id %d is in suspend and ssh enabled, need close sensorhub to set mode", touch_id);
		xiaomi_notify_sensorhub_enable(touch_id, false);
	}
#endif
	if (xiaomi_touch_driver_param->hardware_operation.ic_switch_mode) {
		xiaomi_touch_driver_param->hardware_operation.ic_switch_mode(xiaomi_get_gesture_type(touch_id));
	}
#ifdef TOUCH_SENSORHUB_SUPPORT
	if (xiaomi_touch_data->is_suspend && !xiaomi_touch_data->ssh_status) {
		LOG_INFO("touch id %d is in suspend and ssh enabled, need reopen sensorhub after set mode", touch_id);
		xiaomi_notify_sensorhub_enable(touch_id, true);
	}
#endif
}

static int parse_u32_array_param_dt(struct device_node *np, char *name, unsigned int *value, unsigned int size)
{
	u32 temp_val[MI_GRIP_PARAMETERS_SIZE];
	struct property *prop;
	int retval = 0;
	int i = 0;
	char log_buf[1024];
	int n = 0;

	if (!np || !name || !value || !size)
		return -1;

	prop = of_find_property(np, name, NULL);
	if (!prop || !prop->length) {
		LOG_ERROR("Can't find %s property", name);
		return -1;
	}

	retval = of_property_read_u32_array(np, name ,temp_val, size);
	if (retval < 0) {
		LOG_ERROR("Failed to read value, %s property", name);
		return -1;
	}

	memset(log_buf, 0, 1024);
	n += snprintf(log_buf + n, 1023 - n, "%s: ", name);
	for (i = 0; i < size; i++) {
		value[i] = temp_val[i];
		n += snprintf(log_buf + n, 1023 - n, "%2d, ", value[i]);
	}
	LOG_INFO("%s", log_buf);

	return 0;
}


int xiaomi_touch_init_touch_mode(s8 touch_id, struct device *dev)
{
	struct device_node *np = dev->of_node;

	LOG_INFO("touch id = %d enter", touch_id);
	if (np == NULL || touch_id < 0 || touch_id >= MAX_TOUCH_PANEL_COUNT) {
		LOG_ERROR("error param: np = %p, touch id = %d", np, touch_id);
		return -1;
	}
	parse_u32_array_param_dt(np, "mi_tp,game-mode-array",
		game_mode_bdata[touch_id].game_mode, MI_TOUCH_MODE_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,active-mode-array",
		game_mode_bdata[touch_id].active_mode, MI_TOUCH_MODE_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,up-threshold-array",
		game_mode_bdata[touch_id].up_threshold, MI_TOUCH_MODE_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,tolerance-array",
		game_mode_bdata[touch_id].tolerance, MI_TOUCH_MODE_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,aim-sensitivity-array",
		game_mode_bdata[touch_id].aim_sensitivity, MI_TOUCH_MODE_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,tap-stability-array",
		game_mode_bdata[touch_id].tap_stability, MI_TOUCH_MODE_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,edge-filter-array",
		game_mode_bdata[touch_id].edge_filter, MI_TOUCH_MODE_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,panel-orien-array",
		game_mode_bdata[touch_id].panel_orien, MI_TOUCH_MODE_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,report-rate-array",
		game_mode_bdata[touch_id].report_rate, MI_TOUCH_MODE_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,expert-mode-array",
		game_mode_bdata[touch_id].expert_mode, MI_TOUCH_MODE_PARAMETERS_SIZE);

	parse_u32_array_param_dt(np, "mi_tp,cornerzone-filter-hor1-array",
		game_mode_bdata[touch_id].cornerzone_filter_hor1, MI_GRIP_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,cornerzone-filter-hor2-array",
		game_mode_bdata[touch_id].cornerzone_filter_hor2, MI_GRIP_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,cornerzone-filter-ver-array",
		game_mode_bdata[touch_id].cornerzone_filter_ver, MI_GRIP_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,deadzone-filter-hor-array",
		game_mode_bdata[touch_id].deadzone_filter_hor, MI_GRIP_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,deadzone-filter-ver-array",
		game_mode_bdata[touch_id].deadzone_filter_ver, MI_GRIP_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,edgezone-filter-hor-array",
		game_mode_bdata[touch_id].edgezone_filter_hor, MI_GRIP_PARAMETERS_SIZE);
	parse_u32_array_param_dt(np, "mi_tp,edgezone-filter-ver-array",
		game_mode_bdata[touch_id].edgezone_filter_ver, MI_GRIP_PARAMETERS_SIZE);

	parse_u32_array_param_dt(np, "mi_tp,cornerfilter-area-step-array",
		game_mode_bdata[touch_id].cornerfilter_area_step, MI_CORNERFILTER_AREA_STEP_SIZE);

	/* Touch Game Mode Switch */
	touch_mode[touch_id][DATA_MODE_0][GET_MAX_VALUE] = game_mode_bdata[touch_id].game_mode[MI_DTS_GET_MAX_INDEX];
	touch_mode[touch_id][DATA_MODE_0][GET_MIN_VALUE] = game_mode_bdata[touch_id].game_mode[MI_DTS_GET_MIN_INDEX];
	touch_mode[touch_id][DATA_MODE_0][GET_DEF_VALUE] = game_mode_bdata[touch_id].game_mode[MI_DTS_GET_DEF_INDEX];
	touch_mode[touch_id][DATA_MODE_0][SET_CUR_VALUE] = game_mode_bdata[touch_id].game_mode[MI_DTS_SET_CUR_INDEX];
	touch_mode[touch_id][DATA_MODE_0][GET_CUR_VALUE] = game_mode_bdata[touch_id].game_mode[MI_DTS_GET_CUR_INDEX];

	/* Active Mode */
	touch_mode[touch_id][DATA_MODE_1][GET_MAX_VALUE] = game_mode_bdata[touch_id].active_mode[MI_DTS_GET_MAX_INDEX];
	touch_mode[touch_id][DATA_MODE_1][GET_MIN_VALUE] = game_mode_bdata[touch_id].active_mode[MI_DTS_GET_MIN_INDEX];
	touch_mode[touch_id][DATA_MODE_1][GET_DEF_VALUE] = game_mode_bdata[touch_id].active_mode[MI_DTS_GET_DEF_INDEX];
	touch_mode[touch_id][DATA_MODE_1][SET_CUR_VALUE] = game_mode_bdata[touch_id].active_mode[MI_DTS_SET_CUR_INDEX];
	touch_mode[touch_id][DATA_MODE_1][GET_CUR_VALUE] = game_mode_bdata[touch_id].active_mode[MI_DTS_GET_CUR_INDEX];

	/* Finger Hysteresis */
	touch_mode[touch_id][DATA_MODE_2][GET_MAX_VALUE] = game_mode_bdata[touch_id].up_threshold[MI_DTS_GET_MAX_INDEX];
	touch_mode[touch_id][DATA_MODE_2][GET_MIN_VALUE] = game_mode_bdata[touch_id].up_threshold[MI_DTS_GET_MIN_INDEX];
	touch_mode[touch_id][DATA_MODE_2][GET_DEF_VALUE] = game_mode_bdata[touch_id].up_threshold[MI_DTS_GET_DEF_INDEX];
	touch_mode[touch_id][DATA_MODE_2][SET_CUR_VALUE] = game_mode_bdata[touch_id].up_threshold[MI_DTS_SET_CUR_INDEX];
	touch_mode[touch_id][DATA_MODE_2][GET_CUR_VALUE] = game_mode_bdata[touch_id].up_threshold[MI_DTS_GET_CUR_INDEX];

	/* Tolerance */
	touch_mode[touch_id][DATA_MODE_3][GET_MAX_VALUE] = game_mode_bdata[touch_id].tolerance[MI_DTS_GET_MAX_INDEX];
	touch_mode[touch_id][DATA_MODE_3][GET_MIN_VALUE] = game_mode_bdata[touch_id].tolerance[MI_DTS_GET_MIN_INDEX];
	touch_mode[touch_id][DATA_MODE_3][GET_DEF_VALUE] = game_mode_bdata[touch_id].tolerance[MI_DTS_GET_DEF_INDEX];
	touch_mode[touch_id][DATA_MODE_3][SET_CUR_VALUE] = game_mode_bdata[touch_id].tolerance[MI_DTS_SET_CUR_INDEX];
	touch_mode[touch_id][DATA_MODE_3][GET_CUR_VALUE] = game_mode_bdata[touch_id].tolerance[MI_DTS_GET_CUR_INDEX];

	/*aim sensitivity*/
	touch_mode[touch_id][DATA_MODE_4][GET_MAX_VALUE] = game_mode_bdata[touch_id].aim_sensitivity[MI_DTS_GET_MAX_INDEX];
	touch_mode[touch_id][DATA_MODE_4][GET_MIN_VALUE] = game_mode_bdata[touch_id].aim_sensitivity[MI_DTS_GET_MIN_INDEX];
	touch_mode[touch_id][DATA_MODE_4][GET_DEF_VALUE] = game_mode_bdata[touch_id].aim_sensitivity[MI_DTS_GET_DEF_INDEX];
	touch_mode[touch_id][DATA_MODE_4][SET_CUR_VALUE] = game_mode_bdata[touch_id].aim_sensitivity[MI_DTS_SET_CUR_INDEX];
	touch_mode[touch_id][DATA_MODE_4][GET_CUR_VALUE] = game_mode_bdata[touch_id].aim_sensitivity[MI_DTS_GET_CUR_INDEX];

	/*tap stability*/
	touch_mode[touch_id][DATA_MODE_5][GET_MAX_VALUE] = game_mode_bdata[touch_id].tap_stability[MI_DTS_GET_MAX_INDEX];
	touch_mode[touch_id][DATA_MODE_5][GET_MIN_VALUE] = game_mode_bdata[touch_id].tap_stability[MI_DTS_GET_MIN_INDEX];
	touch_mode[touch_id][DATA_MODE_5][GET_DEF_VALUE] = game_mode_bdata[touch_id].tap_stability[MI_DTS_GET_DEF_INDEX];
	touch_mode[touch_id][DATA_MODE_5][SET_CUR_VALUE] = game_mode_bdata[touch_id].tap_stability[MI_DTS_SET_CUR_INDEX];
	touch_mode[touch_id][DATA_MODE_5][GET_CUR_VALUE] = game_mode_bdata[touch_id].tap_stability[MI_DTS_GET_CUR_INDEX];

	/* Edge Filter */
	touch_mode[touch_id][DATA_MODE_7][GET_MAX_VALUE] = game_mode_bdata[touch_id].edge_filter[MI_DTS_GET_MAX_INDEX];
	touch_mode[touch_id][DATA_MODE_7][GET_MIN_VALUE] = game_mode_bdata[touch_id].edge_filter[MI_DTS_GET_MIN_INDEX];
	touch_mode[touch_id][DATA_MODE_7][GET_DEF_VALUE] = game_mode_bdata[touch_id].edge_filter[MI_DTS_GET_DEF_INDEX];
	touch_mode[touch_id][DATA_MODE_7][SET_CUR_VALUE] = game_mode_bdata[touch_id].edge_filter[MI_DTS_SET_CUR_INDEX];
	touch_mode[touch_id][DATA_MODE_7][GET_CUR_VALUE] = game_mode_bdata[touch_id].edge_filter[MI_DTS_GET_CUR_INDEX];

	/* Orientation */
	touch_mode[touch_id][DATA_MODE_8][GET_MAX_VALUE] = game_mode_bdata[touch_id].panel_orien[MI_DTS_GET_MAX_INDEX];
	touch_mode[touch_id][DATA_MODE_8][GET_MIN_VALUE] = game_mode_bdata[touch_id].panel_orien[MI_DTS_GET_MIN_INDEX];
	touch_mode[touch_id][DATA_MODE_8][GET_DEF_VALUE] = game_mode_bdata[touch_id].panel_orien[MI_DTS_GET_DEF_INDEX];
	touch_mode[touch_id][DATA_MODE_8][SET_CUR_VALUE] = game_mode_bdata[touch_id].panel_orien[MI_DTS_SET_CUR_INDEX];
	touch_mode[touch_id][DATA_MODE_8][GET_CUR_VALUE] = game_mode_bdata[touch_id].panel_orien[MI_DTS_GET_CUR_INDEX];

	/* Report Rate */
	touch_mode[touch_id][DATA_MODE_9][GET_MAX_VALUE] = game_mode_bdata[touch_id].report_rate[MI_DTS_GET_MAX_INDEX];
	touch_mode[touch_id][DATA_MODE_9][GET_MIN_VALUE] = game_mode_bdata[touch_id].report_rate[MI_DTS_GET_MIN_INDEX];
	touch_mode[touch_id][DATA_MODE_9][GET_DEF_VALUE] = game_mode_bdata[touch_id].report_rate[MI_DTS_GET_DEF_INDEX];
	touch_mode[touch_id][DATA_MODE_9][SET_CUR_VALUE] = game_mode_bdata[touch_id].report_rate[MI_DTS_SET_CUR_INDEX];
	touch_mode[touch_id][DATA_MODE_9][GET_CUR_VALUE] = game_mode_bdata[touch_id].report_rate[MI_DTS_GET_CUR_INDEX];

	INIT_WORK(&switch_mode_work[touch_id], xiaomi_touch_switch_mode_work);
	INIT_WORK(&cmd_update_work[touch_id], xiaomi_touch_cmd_update_work);
	INIT_WORK(&grid_update_work[touch_id], xiaomi_touch_grid_update_work);

	if (!cmd_update_wq) {
		cmd_update_wq = alloc_workqueue("xiaomi-touch-cmd-update-queue", WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
		if (!cmd_update_wq) {
			LOG_ERROR("ERROR: Cannot create work thread for cmd_update_wq");
			return -1;
		}
	}

	return 0;
}

static void xiaomi_touch_set_mode_long_value(common_data_t *common_data)
{
	int mode = common_data->mode;
	int len = common_data->data_len;

	if (len <= 0) {
		LOG_ERROR("error param: length %d, mode %d", len, mode);
		return;
	}
#ifdef TOUCH_SENSORHUB_SUPPORT
	if (mode == DATA_MODE_32 && len == FOD_VALUE_LEN) {
		memcpy(fod_value, common_data->data_buf, FOD_VALUE_LEN * sizeof(s32));
		if (fod_value[0] == GESTURE_TOUCH_AND_HOLD_UP_EVENT) {
			xiaomi_set_fod_value(common_data->touch_id);
		} else if (xiaomi_get_gesture_type(common_data->touch_id) & GESTURE_LONGPRESS_EVENT) {
			xiaomi_set_fod_value(common_data->touch_id);
		}
	}
#endif
	if (mode == DATA_MODE_15) {
		if (touch_mode[common_data->touch_id][DATA_MODE_0][GET_CUR_VALUE]) {
			LOG_ERROR("in gamemode,will set by cmd game update update");
			return;
		}

		LOG_INFO("mode: %d, len: %d", mode, len);
		memcpy(grid_updata_value, common_data->data_buf, CMD_DATA_BUF_SIZE * sizeof(s32));
		grid_update_length = len;
		schedule_work(&grid_update_work[common_data->touch_id]);
	}
}

static void xiaomi_touch_get_mode_value(common_data_t *common_data)
{
	int mode = common_data->mode;
	int *value = (int *)common_data->data_buf;
	int val_type = common_data->cmd;

	if ((mode < DATA_MODE_33) && (mode >= 0)) {
		value[0] = touch_mode[common_data->touch_id][mode][val_type];
	}
	else
		LOG_ERROR("error mode[%d]", mode);
}

static void xiaomi_touch_get_mode_all(common_data_t *common_data)
{
	int mode = common_data->mode;
	int *val = (int *)common_data->data_buf;

	if ((mode < DATA_MODE_33) && (mode >= 0)) {
		val[0] = touch_mode[common_data->touch_id][mode][GET_CUR_VALUE];
		val[1] = touch_mode[common_data->touch_id][mode][GET_DEF_VALUE];
		val[2] = touch_mode[common_data->touch_id][mode][GET_MIN_VALUE];
		val[3] = touch_mode[common_data->touch_id][mode][GET_MAX_VALUE];
	} else {
		LOG_ERROR("error mode[%d]", mode);
	}
}

static void xiaomi_touch_reset_mode(common_data_t *common_data)
{
	int i = 0;
	int mode = common_data->mode;

	if (mode < 0) {
		LOG_ERROR("do not support this mode %d", mode);
		return;
	}

	LOG_INFO("touch id:%d, mode:%d", common_data->touch_id, mode);
	if (mode < DATA_MODE_33 && mode > 0) {
		touch_mode[common_data->touch_id][mode][SET_CUR_VALUE] =
			touch_mode[common_data->touch_id][mode][GET_DEF_VALUE];

	} else if (mode == 0) {
		for (i = 0; i < DATA_MODE_9; i++) {
			if (i == DATA_MODE_8) {
				touch_mode[common_data->touch_id][i][SET_CUR_VALUE] =
					touch_mode[common_data->touch_id][i][GET_CUR_VALUE];
			} else {
				touch_mode[common_data->touch_id][i][SET_CUR_VALUE] =
					touch_mode[common_data->touch_id][i][GET_DEF_VALUE];
			}
		}
	}

	if (cmd_update_wq)
		queue_work(cmd_update_wq, &cmd_update_work[common_data->touch_id]);
}

static void xiaomi_touch_set_mode_value(common_data_t *common_data)
{
	int mode = common_data->mode;
	int val = ((int *)common_data->data_buf)[0];
	int value = 0;
	s8 touch_id = common_data->touch_id;
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);

	if (mode < 0 || val < 0 || !xiaomi_touch_driver_param || !xiaomi_touch_data) {
		LOG_ERROR("error param. mode %d, value %d, xiaomi_touch_driver_param %p, xiaomi_touch_data %p",
			mode, val, xiaomi_touch_driver_param, xiaomi_touch_data);
		return;
	}
	if (mode != DATA_MODE_134 && mode != DATA_MODE_150)
		LOG_INFO("touch_id:%d, mode:%d,val:%d", touch_id, mode, val);

	if (mode >= DATA_MODE_33) {
		if (mode == DATA_MODE_42) {
			if (!touch_mode[touch_id][DATA_MODE_0][GET_CUR_VALUE])
				val = 1;
			else
				val = ((common_data->data_buf)[0] == 1 ? 16 : 8);
			if (xiaomi_touch_driver_param->hardware_operation.set_mode_value)
				xiaomi_touch_driver_param->hardware_operation.set_mode_value(mode, &val);
			return;
		}
#ifdef TOUCH_SENSORHUB_SUPPORT
		if (mode == DATA_MODE_48) {
			value = (int)xiaomi_get_gesture_type(touch_id);
			add_common_data_to_buf(0, SET_CUR_VALUE, DATA_MODE_150, 1, &value);
		}
#endif
		if (xiaomi_touch_driver_param->hardware_operation.set_mode_value)
			xiaomi_touch_driver_param->hardware_operation.set_mode_value(mode, common_data->data_buf);
		return;
	}

	touch_mode[touch_id][mode][SET_CUR_VALUE] = val;

	switch (mode) {
	case DATA_MODE_19:
		touch_mode[touch_id][mode][GET_CUR_VALUE] = val;
		if (val) {
			schedule_resume_suspend_work(touch_id, true);
		} else {
			schedule_resume_suspend_work(touch_id, false);
		}
		break;
	case DATA_MODE_14:
		touch_mode[touch_id][mode][GET_CUR_VALUE] = val;
		LOG_INFO("DATA_MODE_14 value [%d]", val);
		queue_work(xiaomi_touch_data->event_wq, &switch_mode_work[touch_id]);
#ifdef TOUCH_SENSORHUB_SUPPORT
		value = (int)xiaomi_get_gesture_type(touch_id);
		add_common_data_to_buf(0, SET_CUR_VALUE, DATA_MODE_150, 1, &value);
#endif
		break;
	case DATA_MODE_11:
		touch_mode[touch_id][mode][GET_CUR_VALUE] = val;
		LOG_INFO("DATA_MODE_11 value [%d]", val);
		queue_work(xiaomi_touch_data->event_wq, &switch_mode_work[touch_id]);
		break;
	case DATA_MODE_10:
		touch_mode[touch_id][mode][GET_CUR_VALUE] = val;
		LOG_INFO("DATA_MODE_10 value [%d]", val);
		queue_work(xiaomi_touch_data->event_wq, &switch_mode_work[touch_id]);
		break;
	case DATA_MODE_16:
		touch_mode[touch_id][mode][GET_CUR_VALUE] = val;
		LOG_INFO("DATA_MODE_16 value [%d]", val);
		queue_work(xiaomi_touch_data->event_wq, &switch_mode_work[touch_id]);
		break;
	case DATA_MODE_17:
		touch_mode[touch_id][mode][GET_CUR_VALUE] = val;
		LOG_INFO("DATA_MODE_17 value [%d]", val);
#ifdef TOUCH_SENSORHUB_SUPPORT
	if (sensorhub_nonui_enable) {
		LOG_INFO("ssh status:%d", xiaomi_touch_data->ssh_status);
		if (val != 2 && !xiaomi_touch_data->ssh_status && (xiaomi_get_gesture_type(touch_id) & GESTURE_LONGPRESS_EVENT) != 0) {
			LOG_INFO("enable sensor again\n");
			queue_work(xiaomi_touch_data->event_wq, &switch_mode_work[touch_id]);
		}
		LOG_INFO("support sensorhub,skip switch ic state in ap\n");
		return;
	}
#endif
		queue_work(xiaomi_touch_data->event_wq, &switch_mode_work[touch_id]);
		break;
	case DATA_MODE_18:
		LOG_INFO("Touch debug log level [%d]", val);
		touch_mode[touch_id][mode][GET_CUR_VALUE] = val;
		current_log_level = val;
		if (xiaomi_touch_driver_param->hardware_operation.touch_log_level_control_v2)
			xiaomi_touch_driver_param->hardware_operation.touch_log_level_control_v2(val);
		break;
	case DATA_MODE_0:
	case DATA_MODE_2:
	case DATA_MODE_3:
	case DATA_MODE_4:
	case DATA_MODE_5:
	case DATA_MODE_7:
	case DATA_MODE_8:
		if (cmd_update_wq) {
			queue_work(cmd_update_wq, &cmd_update_work[touch_id]);
		}
		break;
#ifdef TOUCH_SENSORHUB_SUPPORT
	case DATA_MODE_32:
		if (val == GESTURE_ID_SENSORHUB_ERR) {
			LOG_INFO("sensorhub err, disable sensorhub thp");
			xiaomi_notify_sensorhub_enable(touch_id, false);
		} else if ((xiaomi_get_gesture_type(common_data->touch_id) & GESTURE_DOUBLETAP_EVENT) || (xiaomi_get_gesture_type(common_data->touch_id) & GESTURE_SINGLETAP_EVENT)) {
			if (xiaomi_touch_driver_param->hardware_operation.set_mode_value)
				xiaomi_touch_driver_param->hardware_operation.set_mode_value(mode, common_data->data_buf);
		}
		break;
#endif
	case DATA_MODE_31:
		LOG_INFO("Touch sensor enable: %d", val);
		value = val > 0 ? XIAOMI_TOUCH_ENABLE_SENSOR : XIAOMI_TOUCH_DISABLE_SENSOR;
		add_common_data_to_buf(touch_id, SET_CUR_VALUE, DATA_MODE_27, 1, &value);
		break;
	default:
		LOG_ERROR("touch id %d don't not support mode %d", touch_id, mode);
		break;
	}
}

static DEFINE_MUTEX(ioctl_operation_mutex);
int xiaomi_touch_mode(private_data_t *client_private_data, u32 user_size, unsigned long arg)
{
	mutex_lock(&ioctl_operation_mutex);
	if (user_size != sizeof(common_data_t)) {
		LOG_ERROR("error common data size %d %lu, return!", user_size, sizeof(common_data_t));
		mutex_unlock(&ioctl_operation_mutex);
		return -EINVAL;
	} else {
		common_data_t common_data;
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = NULL;
		int copy_size = copy_from_user(&common_data, (void __user *)arg, user_size);
		if (copy_size) {
			LOG_ERROR("copy cmd from user failed, return! value %d", copy_size);
			mutex_unlock(&ioctl_operation_mutex);
			return -1;
		}

		if (client_private_data->touch_id != common_data.touch_id) {
			LOG_ERROR("client id is not equal common data id, please check it!");
			mutex_unlock(&ioctl_operation_mutex);
			return -1;
		}

		xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(common_data.touch_id);
		if (!xiaomi_touch_driver_param) {
			LOG_ERROR("please select touch id first, then send common data!");
			mutex_unlock(&ioctl_operation_mutex);
			return -1;
		}

			/* filter the log print, only print log when HAVE FINGER/NO FINGER status change*/
		if (common_data.cmd == SET_CUR_VALUE && common_data.mode != DATA_MODE_134) {
			/* print the other log*/
			LOG_INFO("cmd: %d, mode: %d, data_buf[0]: %d", common_data.cmd, common_data.mode, common_data.data_buf[0]);
		}

		switch (common_data.cmd) {
		case SET_THP_IC_CUR_VALUE:
			if (xiaomi_touch_driver_param->hardware_operation.htc_ic_setModeValue) {
				xiaomi_touch_driver_param->hardware_operation.htc_ic_setModeValue(&common_data);
			}
			break;
		case GET_THP_IC_CUR_VALUE:
			if (xiaomi_touch_driver_param->hardware_operation.htc_ic_getModeValue &&
				!xiaomi_touch_driver_param->hardware_operation.htc_ic_getModeValue(&common_data)) {
					update_get_ic_current_value(&common_data);
					copy_size = copy_to_user((void __user *)arg, &common_data, user_size);
					if (copy_size) {
						LOG_ERROR("%d copy to user failed! value %d", common_data.cmd, copy_size);
						mutex_unlock(&ioctl_operation_mutex);
						return -1;
					}
			} else {
				LOG_ERROR("get thp ic cur value has error");
				mutex_unlock(&ioctl_operation_mutex);
				return -1;
			}
			break;
		case SET_CMD_FOR_DRIVER:
			if (common_data.mode == DATA_MODE_26) {
				update_palm_sensor_value(common_data.data_buf[0]);
			}
			mutex_unlock(&ioctl_operation_mutex);
			return 0;
		case SET_CMD_FOR_THP:
			add_common_data_to_buf(common_data.touch_id, SET_CUR_VALUE, common_data.mode, common_data.data_len, common_data.data_buf);
			mutex_unlock(&ioctl_operation_mutex);
			return 0;
		case SET_CUR_VALUE:
			if(common_data.mode == DATA_MODE_151 && common_data.data_buf[0] > 100 && common_data.data_buf[0] < 127) {
				sendnlmsg(common_data.data_buf[0]);
				break;
			}
			xiaomi_touch_set_mode_value(&common_data);
			break;
		case GET_CUR_VALUE:
		case GET_DEF_VALUE:
		case GET_MIN_VALUE:
		case GET_MAX_VALUE:
			xiaomi_touch_get_mode_value(&common_data);
			copy_size = copy_to_user((void __user *)arg, &common_data, user_size);
			if (copy_size)
				LOG_ERROR("%d copy to user failed! value %d", common_data.cmd, copy_size);
			break;
		case RESET_MODE:
			xiaomi_touch_reset_mode(&common_data);
			break;
		case GET_MODE_VALUE:
			xiaomi_touch_get_mode_all(&common_data);
			copy_size = copy_to_user((void __user *)arg, &common_data, user_size);
			if (copy_size)
				LOG_ERROR("%d copy to user failed! value %d", common_data.cmd, copy_size);
			break;
		case SET_LONG_VALUE:
			xiaomi_touch_set_mode_long_value(&common_data);
			break;
		default:
			LOG_ERROR("don't support common cmd");
			mutex_unlock(&ioctl_operation_mutex);
			return -EINVAL;
		}

		if (common_data.mode < THP_CMD_BASE || common_data.mode == DATA_MODE_48) {
			add_common_data_to_buf(common_data.touch_id, common_data.cmd, common_data.mode, common_data.data_len, common_data.data_buf);
		}
	}
	mutex_unlock(&ioctl_operation_mutex);
	return 0;
}
