#ifndef _MI_DISP_FLAT_MODE_H_
#define _MI_DISP_FLAT_MODE_H_

#include "dsi_parser.h"
#include "dsi_panel.h"
#include "dsi_display.h"

struct mi_panel_flatmode_config {
	bool flatmode_check_enabled;
	struct dsi_panel_cmd_set offset_cmd;
	struct dsi_panel_cmd_set status_cmd;
	struct dsi_panel_cmd_set offset_end_cmd;
	u32 *status_cmds_rlen;
	u32 *status_value;
	u8 *return_buf;
	u8 *status_buf;
};

int mi_dsi_panel_flatmode_validate_status(struct dsi_display *display, bool *status);

#endif /* _MI_DISP_FLAT_MODE_H_ */
