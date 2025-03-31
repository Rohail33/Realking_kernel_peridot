/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 */

#ifndef __UAPI_CAM_ISPV4_H__
#define __UAPI_CAM_ISPV4_H__

#include <linux/types.h>
#include <media/cam_defs.h>

#define ISP_OPCODE_PWR_ON                       (CAM_EXT_OPCODE_BASE + 0x5)
#define ISP_OPCODE_PWR_OFF                      (CAM_EXT_OPCODE_BASE + 0x6)
#define ISP_OPCODE_HDMA_TRANS                   (CAM_EXT_OPCODE_BASE + 0x7)
#define ISP_OPCODE_IONMAP                       (CAM_EXT_OPCODE_BASE + 0x10)
#define ISP_OPCODE_IONUNMAP                     (CAM_EXT_OPCODE_BASE + 0x11)
#define ISPV4_OPCODE_RPMSG_SEND_ISP             (CAM_EXT_OPCODE_BASE + 0x20)
#define ISPV4_OPCODE_RPMSG_RECV_ISP             (CAM_EXT_OPCODE_BASE + 0x21)
#define ISPV4_OPCODE_RPMSG_GETERR_ISP           (CAM_EXT_OPCODE_BASE + 0x22)
#define ISPV4_OPCODE_RPROC_BOOT                 (CAM_EXT_OPCODE_BASE + 0x30)
#define ISPV4_OPCODE_RPROC_SHUTDOWN             (CAM_EXT_OPCODE_BASE + 0x31)
#define ISPV4_RPROC_DDR_PARAM_LOAD              (CAM_EXT_OPCODE_BASE + 0x32)
#define ISPV4_RPROC_DDR_PARAM_STORE             (CAM_EXT_OPCODE_BASE + 0x33)
#define ISP_OPCODE_SUSPEND                      (CAM_EXT_OPCODE_BASE + 0x41)
#define ISP_OPCODE_RESUME                       (CAM_EXT_OPCODE_BASE + 0x42)
#define ISP_OPCODE_CHANGE_SPI_SPEED             (CAM_EXT_OPCODE_BASE + 0x43)
//#define ISP_OPCODE_READ                         (CAM_EXT_OPCODE_BASE + 0x7)
//#define ISP_OPCODE_WRITE                        (CAM_EXT_OPCODE_BASE + 0x8)
//#define ISP_OPCODE_CHANGE_SPI_SPEED             (CAM_EXT_OPCODE_BASE + 0x9)
//#define ISP_OPCODE_POLL_EXIT                    (CAM_EXT_OPCODE_BASE + 0xb)

//Perframe data
typedef struct {
    int32_t m_nCctV400;
    uint32_t m_pWBGainApply[4];
} algo_awb_info_t;

typedef struct {
    int32_t m_nLongTGain;
    int32_t m_nShortTGain;
    uint16_t m_nLongAnalogGain;
    uint16_t m_nShortAnalogGain;
    int32_t m_nLongSnsDigitalGain;
    int32_t m_nShortSnsDigitalGain;
    uint16_t m_nLongDigitalGain;
    uint16_t m_nShortDigitalGain;

    uint32_t m_nDrcGainV400;
    uint32_t m_nDrcGainDarkV400;

    uint16_t m_nExpRatio;

    uint32_t m_nExpIdxV400;
    uint32_t m_nPrevExpIdxV400;
    int32_t m_nLuxIdxV400;

    int32_t m_nRgb4gainLongDgain;
    int32_t m_nRgb4gainShortDgain;
    int32_t m_nBlcLongDgain;
    int32_t m_nBlcShortDgain;
} algo_ae_info_t;

typedef struct {
    uint16_t v_add;
    uint16_t h_add;
    uint16_t v_size;
    uint16_t h_size;
    uint16_t roi_width_e;
    uint16_t roi_width_s;
    uint16_t roi_height_e;
    uint16_t roi_height_s;
    uint16_t his_roi_width_e;
    uint16_t his_roi_width_s;
    uint16_t his_roi_height_e;
    uint16_t his_roi_height_s;
} algo_ae_roiInfo_t;

typedef struct {
    uint8_t fix_roi_grid_v;
    uint8_t fix_roi_grid_h;
    uint8_t g1_square_en;
    uint8_t g0_square_en;
    uint8_t d1_v_ratio;
    uint8_t d1_h_ratio;
    uint8_t d0_v_ratio;
    uint8_t d0_h_ratio;
    uint8_t g1_filter_set;
    uint8_t g0_filter_set;
    uint16_t fix_roi_width;
    uint16_t fix_roi_start_x;
    uint16_t fix_roi_height;
    uint16_t fix_roi_start_y;
    uint16_t free_roi_width;
    uint16_t free_roi_start_x;
    uint16_t free_roi_height;
    uint16_t free_roi_start_y;
    uint16_t g0_coring_h_bias;
    uint16_t g0_coring_h_th;
    uint16_t g0_coring_v_bias;
    uint16_t g0_coring_v_th;
    uint16_t g1_coring_h_bias;
    uint16_t g1_coring_h_th;
    uint16_t g1_coring_v_bias;
    uint16_t g1_coring_v_th;
    uint16_t g0_count_th_v;
    uint16_t g0_count_th_h;
    uint16_t g1_count_th_v;
    uint16_t g1_count_th_h;
    int16_t g0_fir_h_coef[13];
    int16_t g0_fir_v_coef[8];
    int16_t g1_fir_h_coef[13];
    int16_t g1_fir_v_coef[8];
    int16_t g0_iir_h_coef[10];
    int16_t g0_iir_v_coef[10];
    int16_t g1_iir_h_coef[10];
    int16_t g1_iir_v_coef[10];
    int16_t raw2y_coef[4];
    uint16_t gamma[32];
} algo_af_info_t;

typedef struct {
    uint8_t m_bEnableFilter;
    uint8_t m_bOverlap;
    uint8_t m_bUseWindowFunc;
    uint8_t m_nLRVbinningNum;
    uint8_t m_nLRHbinningNum;
    int16_t m_nHFilterSumCorr;
    uint8_t m_nHFilterShift;
    uint8_t m_nHSearchWidth;
    uint16_t m_nHOutWinSY;
    uint16_t m_nHOutWinSX;
    uint16_t m_nHOutWinEY;
    uint16_t m_nHOutWinEX;
    uint16_t m_nHInWinSY;
    uint16_t m_nHInWinSX;
    uint16_t m_nHInWinEY;
    uint16_t m_nHInWinEX;
    uint16_t m_nHInWinH;
    uint16_t m_nHInWinW;
    uint16_t m_nHInGridNumY;
    uint16_t m_nHInGridNumX;
    uint16_t m_nHFwinPtY;
    uint16_t m_nHFwinPtX;
    uint16_t m_nHFwinPtH;
    uint16_t m_nHFwinPtW;
    int8_t m_pHFilterTable[21];
} algo_pp2pd_info_t;

typedef enum
{
    NPU_AIFD_ROT_0 = 0,
    NPU_AIFD_ROT_90,
    NPU_AIFD_ROT_180,
    NPU_AIFD_ROT_270
} npu_rot_param_e;

#pragma pack(4)
struct ispv4_cmd_set {
    struct xm_ispv4_rpmsg_pkg pkg;
    algo_af_info_t algo_af_info;
    algo_pp2pd_info_t algo_pp2pd_info;
    int32_t m_nCurrentTemperature;
    npu_rot_param_e npu_rot_param;
    uint8_t op_mode;
    uint32_t m_nZoomRatio;
    uint32_t qc_request_id;
    uint32_t mifw_request_id;
};

struct ispv4_sensor_meta {
    struct xm_ispv4_rpmsg_pkg pkg;
    algo_ae_info_t algo_ae_info;
    algo_awb_info_t algo_awb_info;
    algo_ae_roiInfo_t algo_ae_roiInfo;
    uint32_t qc_request_id;
    uint32_t mifw_request_id;
};

#pragma pack()
struct ispv4_dropframe_param{
    struct xm_ispv4_rpmsg_pkg pkg;
    uint32_t enable;
    uint32_t freq;
    uint32_t count;
};

struct ispv4_dump_enable_msg {
    struct xm_ispv4_rpmsg_pkg pkg;
    uint32_t port_id;
    uint32_t frame_start;
    uint32_t frame_num;
    uint32_t meta_data;
    uint32_t reserved;         // Custom data, such as start frame_id
};

struct ispv4_pipe_debug_param {
    struct xm_ispv4_rpmsg_pkg pkg;
    uint32_t hdma_param_mask;
    uint32_t ipc_param_mask;
    uint32_t dynamic_param_mask;
    uint32_t algo_param_mask;
};

struct ispv4_bypass_moudle_msg {
    struct xm_ispv4_rpmsg_pkg pkg;
    uint64_t fe_moudle_mask;
    uint64_t be_moudle_mask;
    uint32_t reserved;         // Custom data
};

//IONMAP
struct ispv4_ionmap_para {
	uint32_t fd;
	enum ispv4_ionmap_region region;
	uint32_t iova;
};

// hdma rpmsg

typedef enum {
    ISP_INITIAL_DONE = 1,
    ISP_NPU_ERROR,
    ISP_TUNING_ERROR,
    ISP_USECASE_ERROR,
    ISP_NOTIFY_TYPE_BOTTOM,
} isp_notify_type_e;

#pragma pack(4)
typedef struct buf_blk {     //8 bytes
	uint32_t offset;         //offset of a buffer block
	uint32_t size;           //size of a buffer block
} buf_blk_t;

typedef struct meta_buf {    //20 bytes
	uint32_t hdma_addr;      // AP to FW usually
	uint32_t total_size;     // total buffer size
    union {
        uint32_t buf_id;   // buffer id: 3A/Depth/FDFA/Dump/LSC
        uint32_t reserved;    // reserved, ex for lsc ap->fw dst_hdmaaddr
    } u;
	uint32_t buf_fd;         // buffer fd
	uint32_t total_blk_num;  // number of buffer block
	buf_blk_t blk[0];        // pointer of buffer block
} meta_buf_t;

typedef struct ispv4_ipc_hdma_msg {  //notify: 16+16+20+8=60 bytes
	struct xm_ispv4_rpmsg_pkg pkg;
	uint32_t frame_id;       // FW to AP usually
	uint64_t reserved;       // Custom data, such as usecase, time stamp, status
	uint32_t meta_buf_num;   // 3A Depth FDFA Dump Buffer Number
	meta_buf_t buf[0];       // pointer of meta buffer
} ispv4_ipc_hdma_msg_t;

typedef struct ispv4_ipc_hdma_msg_recv {  //notify: 8+16+20+8=52 bytes
	uint32_t func;           // when csl send event will remove the pkg header
	uint32_t frame_id;       // FW to AP usually
	uint64_t reserved;       // Custom data, such as usecase, time stamp, status
	uint32_t meta_buf_num;   // 3A Depth FDFA Dump Buffer Number
	meta_buf_t buf[0];       // pointer of meta buffer
} ispv4_ipc_hdma_msg_recv_t;
#pragma pack()

#pragma pack(4)
typedef struct npu_algo_meta {
    float rgb4gain_dgain_fix;
    float noise_tuning_dark;
    float fusion_tuning_dark;
    float noise_tuning_normal;
    float fusion_tuning_normal;
    float noise_tuning_hdr;
    float fusion_tuning_hdr;
    uint32_t luxindex;
    uint32_t model_again[4];
    uint32_t model_expo_ratio[4];
} npu_algo_meta_t;

struct ispv4_ipc_npu_algo_msg {
    struct xm_ispv4_rpmsg_pkg pkg;
    npu_algo_meta_t npu_meta;
};
#pragma pack()

//RPMSG
struct ispv4_rpmsg_send_para {
	uint32_t cmd;
	uint32_t len; /*len of specific rpmsg struct you define*/
	uint32_t msgid;
	uint64_t data_handle; /*no use for hal*/
	uint32_t data[0]; /*specific rpmsg struct you define*/
};

struct ispv4_rpmsg_recv_para {
	uint32_t cap;
	union {
	uint64_t data_handle;
	uint8_t data[0];
	};
};

//HDMA:
struct ispv4_hdma_para {
	enum pcie_hdma_dir hdma_dir;
	uint32_t ep_addr;
	uint32_t len;
	uint64_t data_handle;

};

enum cam_ispv4_packet_opcodes {
	CAMERA_ISPV4_CMD_OPCODE_UPDATE,
	CAMERA_ISPV4_CMD_OPCODE_RPMSG_SEND,
	CAMERA_ISPV4_CMD_OPCODE_RPMSG_RECV,
	CAMERA_ISPV4_CMD_OPCODE_RPMSG_GETERR,
	CAMERA_ISPV4_CMD_OPCODE_PWR_ON,
	CAMERA_ISPV4_CMD_OPCODE_PWR_OFF,
	CAMERA_ISPV4_CMD_OPCODE_IONMAP_WITH_NOTIFY,
	CAMERA_ISPV4_CMD_OPCODE_IONUNMAP,
	CAMERA_ISPV4_CMD_OPCODE_ANALOG_BYPASS,
	CAMERA_ISPV4_CMD_OPCODE_DIGITAL_BYPASS,
	CAMERA_ISPV4_CMD_OPCODE_RAWLOG_DUMP,
	CAMERA_ISPV4_CMD_OPCODE_DEBUGINFO_DUMP,
	CAMERA_ISPV4_CMD_OPCODE_BOOTINFO_DUMP,
	CAMERA_ISPV4_CMD_OPCODE_IONMAP_REGION,
	CAMERA_ISPV4_CMD_OPCODE_IONUNMAP_REGION,
	CAMERA_ISPV4_CMD_OPCODE_MAX,
};
#endif
