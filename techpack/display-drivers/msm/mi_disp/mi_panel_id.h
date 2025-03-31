/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#ifndef _MI_PANEL_ID_H_
#define _MI_PANEL_ID_H_

#include <linux/types.h>
#include "dsi_panel.h"

/*
   Naming Rules,Wiki Dec .
   4Byte : Project ASCII Value .Exemple "L18" ASCII Is 004C3138
   1Byte : Prim Panel Is 'P' ASCII Value , Sec Panel Is 'S' Value
   1Byte : Panel Vendor
   1Byte : DDIC Vendor ,Samsung 0x0C Novatek 0x02
   1Byte : Display screen supplier ID (0x0A/0x0B/0x0C ...)
*/
#define N1_42_02_0A_PANEL_ID   0x00004E310042020A
#define N2_42_02_0A_PANEL_ID   0x00004E320042020A
#define N3_42_0D_0A_PANEL_ID   0x00004E3300420D0A
#define N11U_42_02_0A_PANEL_ID 0x4E3131550042020A
#define N8_42_02_0A_PRIM_PANEL_ID   0x00004E385042020A
#define N8_42_02_0A_SEC_PANEL_ID   0x00004E385342020A
#define N9_42_02_0A_PANEL_ID   0x00004E395042020A
#define N18_38_0C_0A_PRIM_PANEL_ID   0x004E313850380C0A
#define N18_42_02_0A_SEC_PANEL_ID   0x004E31385342020A
#define N16T_42_02_0A_PANEL_ID    0x4E3136540042020A
#define N16T_36_0F_0B_PANEL_ID    0x4E31365400360F0B
#define N16T_42_0A_0C_PANEL_ID    0x4E31365400420A0C
#define O16U_42_02_0A_PANEL_ID    0x4F3136550042020A
#define O16U_36_0D_0B_PANEL_ID    0x4F31365500360D0B
#define O81_36_02_0A_PANEL_ID  0x004F38315036020A
#define O82_36_02_0A_PANEL_ID  0x004F38325036020A
#define O81_42_02_0B_PANEL_ID  0x004F38315042020B
#define O82_42_02_0B_PANEL_ID  0x004F38325042020B
#define O11_42_02_0A_PANEL_ID  0x004F31310042020A

/* PA: Primary display, First selection screen
 * PB: Primary display, Second selection screen
 * SA: Secondary display, First selection screen
 * SB: Secondary display, Second selection screen
 */
enum mi_project_panel_id {
	PANEL_ID_INVALID = 0,
	N1_PANEL_PA,
	N2_PANEL_PA,
	N3_PANEL_PA,
	N11U_PANEL_PA,
	N8_PANEL_PA,
	N8_PANEL_SA,
	N9_PANEL_PA,
	N18_PANEL_PA,
	N18_PANEL_SA,
	N16T_PANEL_PA,
	N16T_PANEL_PB,
	N16T_PANEL_PC,
	O16U_PANEL_PA,
	O16U_PANEL_PB,
	O81_PANEL_PA,
	O82_PANEL_PA,
	O81_PANEL_PB,
	O82_PANEL_PB,
	O11_PANEL_PA,
	PANEL_ID_MAX
};

#define N1_PANEL_PA_P01_01 0x54
#define N1_PANEL_PA_P01_02 0x55
#define N1_PANEL_PA_P1_01 0x56
#define N1_PANEL_PA_P1_02 0x57
#define N1_PANEL_PA_P11_01 0x60
#define N1_PANEL_PA_P11_02 0x62

#define N2_PANEL_PA_P00 0x00
#define N2_PANEL_PA_P01 0x03
#define N2_PANEL_PA_P11_01 0x50
#define N2_PANEL_PA_P11_02 0x52
#define N2_PANEL_PA_P2_01 0x80
#define N2_PANEL_PA_P2_02 0x81

#define N3_PANEL_PA_P00 0x00
#define N3_PANEL_PA_P10 0x20
#define N3_PANEL_PA_P11 0x50
#define N3_PANEL_PA_P11_2_0 0x51
#define N3_PANEL_PA_P11_2_1 0x52
#define N3_PANEL_PA_P11_2_3 0x53
#define N3_PANEL_PA_P20 0x80
#define N3_PANEL_PA_MP 0xC0

#define N18_PANEL_PA_P00 0x00
#define N18_PANEL_PA_P01_01 0x01
#define N18_PANEL_PA_P01_02 0x80
#define N18_PANEL_PA_P10 0x10

#define N18_PANEL_SA_P01 0x10
#define N18_PANEL_SA_P10_01 0x40
#define N18_PANEL_SA_P10_02 0x41
#define N18_PANEL_SA_P11 0x50
#define N18_PANEL_SA_P2 0x80
#define N18_PANEL_SA_P21 0x90

#define N9_PANEL_PA_P11 0x50
#define N9_PANEL_PA_P2 0x80
#define N9_PANEL_PA_P21 0x90

#define N8_PANEL_SA_P11 0x50

static inline enum mi_project_panel_id mi_get_panel_id(u64 mi_panel_id)
{
	switch(mi_panel_id) {
	case N1_42_02_0A_PANEL_ID:
		return N1_PANEL_PA;
	case N2_42_02_0A_PANEL_ID:
		return N2_PANEL_PA;
	case N3_42_0D_0A_PANEL_ID:
		return N3_PANEL_PA;
	case N11U_42_02_0A_PANEL_ID:
		return N11U_PANEL_PA;
	case N8_42_02_0A_PRIM_PANEL_ID:
		return N8_PANEL_PA;
	case N8_42_02_0A_SEC_PANEL_ID:
		return N8_PANEL_SA;
	case N9_42_02_0A_PANEL_ID:
		return N9_PANEL_PA;
	case N18_38_0C_0A_PRIM_PANEL_ID:
		return N18_PANEL_PA;
	case N18_42_02_0A_SEC_PANEL_ID:
		return N18_PANEL_SA;
	case N16T_42_02_0A_PANEL_ID:
		return N16T_PANEL_PA;
	case N16T_36_0F_0B_PANEL_ID:
		return N16T_PANEL_PB;
	case N16T_42_0A_0C_PANEL_ID:
		return N16T_PANEL_PC;
	case O16U_42_02_0A_PANEL_ID:
		return O16U_PANEL_PA;
	case O16U_36_0D_0B_PANEL_ID:
		return O16U_PANEL_PB;
	case O81_36_02_0A_PANEL_ID:
		return O81_PANEL_PA;
	case O82_36_02_0A_PANEL_ID:
		return O82_PANEL_PA;
	case O81_42_02_0B_PANEL_ID:
		return O81_PANEL_PB;
	case O82_42_02_0B_PANEL_ID:
		return O82_PANEL_PB;
	case O11_42_02_0A_PANEL_ID:
		return O11_PANEL_PA;
	default:
		return PANEL_ID_INVALID;
	}
}

static inline const char *mi_get_panel_id_name(u64 mi_panel_id)
{
	switch (mi_get_panel_id(mi_panel_id)) {
	case N1_PANEL_PA:
		return "N1_PANEL_PA";
	case N2_PANEL_PA:
		return "N2_PANEL_PA";
	case N3_PANEL_PA:
		return "N3_PANEL_PA";
	case N11U_PANEL_PA:
		return "N11U_PANEL_PA";
	case N8_PANEL_PA:
		return "N8_PANEL_PA";
	case N8_PANEL_SA:
		return "N8_PANEL_SA";
	case N9_PANEL_PA:
		return "N9_PANEL_PA";
	case N18_PANEL_PA:
		return "N18_PANEL_PA";
	case N18_PANEL_SA:
		return "N18_PANEL_SA";
	case N16T_PANEL_PA:
		return "N16T_PANEL_PA";
	case N16T_PANEL_PB:
		return "N16T_PANEL_PB";
	case N16T_PANEL_PC:
		return "N16T_PANEL_PC";
	case O16U_PANEL_PA:
		return "O16U_PANEL_PA";
	case O16U_PANEL_PB:
		return "O16U_PANEL_PB";
	case O81_PANEL_PA:
		return "O81_PANEL_PA";
	case O82_PANEL_PA:
		return "O82_PANEL_PA";
	case O81_PANEL_PB:
		return "O81_PANEL_PB";
	case O82_PANEL_PB:
		return "O82_PANEL_PB";
	case O11_PANEL_PA:
		return "O11_PANEL_PA";
	default:
		return "unknown";
	}
}

static inline bool is_use_nt37801_dsc_config(u64 mi_panel_id)
{
	switch(mi_panel_id) {
	case N1_42_02_0A_PANEL_ID:
	case N2_42_02_0A_PANEL_ID:
	case N11U_42_02_0A_PANEL_ID:
		return true;
	default:
		return false;
	}
}

static inline bool is_use_nt37706_dsc_config(u64 mi_panel_id)
{
	switch(mi_panel_id) {
	case N16T_42_02_0A_PANEL_ID:
	case N16T_36_0F_0B_PANEL_ID:
		return true;
	default:
		return false;
	}
}

static inline bool is_use_nt36532_dsc_config(u64 mi_panel_id)
{
	switch(mi_panel_id) {
	case O81_36_02_0A_PANEL_ID:
	case O82_36_02_0A_PANEL_ID:
	case O81_42_02_0B_PANEL_ID:
	case O82_42_02_0B_PANEL_ID:
		return true;
	default:
		return false;
	}
}


enum mi_project_panel_id mi_get_panel_id_by_dsi_panel(struct dsi_panel *panel);
enum mi_project_panel_id mi_get_panel_id_by_disp_id(int disp_id);

#endif /* _MI_PANEL_ID_H_ */
