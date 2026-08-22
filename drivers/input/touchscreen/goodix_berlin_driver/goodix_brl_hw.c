/*
 * Goodix Touchscreen Driver
 * Copyright (C) 2020 - 2021 Goodix, Inc.
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
 *
 */
#include "goodix_ts_core.h"
#include <linux/rtc.h>
#include <linux/time.h>
#include <linux/time64.h>
#ifdef GOODIX_DEBUG_SPI
#define CREATE_TRACE_POINTS
#include "touch_trace.h"
#endif

/* berlin_A SPI mode setting */
#define GOODIX_SPI_MODE_REG			0xC900
#define GOODIX_SPI_NORMAL_MODE_0	0x01

/* berlin_A D12 setting */
#define GOODIX_REG_CLK_STA0			0xD807
#define GOODIX_CLK_STA0_ENABLE		0xFF
#define GOODIX_REG_CLK_STA1			0xD806
#define GOODIX_CLK_STA1_ENABLE		0x77
#define GOODIX_REG_TRIM_D12			0xD006
#define GOODIX_TRIM_D12_LEVEL		0x3C
#define GOODIX_REG_RESET			0xD808
#define GOODIX_RESET_EN				0xFA
#define HOLD_CPU_REG_W				0x0002
#define HOLD_CPU_REG_R				0x2000

#define DEV_CONFIRM_VAL				0xAA
#define BOOTOPTION_ADDR				0x10000
#define FW_VERSION_INFO_ADDR_BRA	0x1000C
#define FW_VERSION_INFO_ADDR		0x10014

#define GOODIX_IC_INFO_MAX_LEN		1024
#define GOODIX_IC_INFO_ADDR_BRA		0x10068
#define GOODIX_IC_INFO_ADDR			0x10070


enum brl_request_code {
	BRL_REQUEST_CODE_CONFIG = 0x01,
	BRL_REQUEST_CODE_REF_ERR = 0x02,
	BRL_REQUEST_CODE_RESET = 0x03,
	BRL_REQUEST_CODE_CLOCK = 0x04,
};

static int brl_select_spi_mode(struct goodix_ts_core *cd)
{
	int ret;
	int i;
	u8 w_value = 0x70;
	u8 r_value;

	if (cd->bus->bus_type == GOODIX_BUS_TYPE_I2C ||
			cd->bus->ic_type != IC_TYPE_BERLIN_B)
		return 0;

	for (i = 0; i < GOODIX_RETRY_5; i++) {
		cd->hw_ops->write(cd, 0xC981,
				&w_value, 1);
		ret = cd->hw_ops->read(cd, 0xC981,
				&r_value, 1);
		if (!ret && r_value == w_value)
			return 0;
	}
	ts_err("failed switch SPI mode after reset, ret:%d r_value:%02x", ret, r_value);
	return -EINVAL;
}

static int brl_power_on(struct goodix_ts_core *cd, bool on)
{
	int ret = 0;
	int iovdd_gpio = cd->board_data.iovdd_gpio;
	int avdd_gpio = cd->board_data.avdd_gpio;
	int reset_gpio = cd->board_data.reset_gpio;

	if(on) {
		if (iovdd_gpio > 0)
			gpio_direction_output(iovdd_gpio, 1);
		else if (cd->iovdd) {
			ret = regulator_enable(cd->iovdd);
			if (ret) {
				ts_err("Failed to enable iovdd:%d", ret);
				goto power_off;
			}
			ts_info("regulator enable iovdd success");
		}

		/*usleep_range(3000, 3100);*/
		/* o80没有iovdd 延时也不需要*/
		if (avdd_gpio > 0) {
			ret = gpio_direction_output(avdd_gpio, 1);
			ts_info("gpio[%d] set directionn return:%d", avdd_gpio, ret);
			ts_info("gpio[%d] value is:%d", avdd_gpio, gpio_get_value(avdd_gpio));
		}
		else if (cd->avdd) {
			ret = regulator_enable(cd->avdd);
			if (ret) {
				ts_err("[DIS-TF-TOUCH] Failed to enable avdd:%d", ret);
				goto power_off;
			}
			ts_info("regulator enable avdd success");
		}

		usleep_range(15000, 15100); /* T2 must longer than 10ms*/
		gpio_direction_output(reset_gpio, 1);
		msleep(GOODIX_NORMAL_RESET_DELAY_MS);
		brl_select_spi_mode(cd);
		return 0;
	}

power_off:
	ts_info("tp power off");
	gpio_direction_output(reset_gpio, 0);
	if (iovdd_gpio > 0)
		gpio_direction_output(iovdd_gpio, 0);
	else if (cd->iovdd) {
		ret = regulator_disable(cd->iovdd);
		if (ret < 0)
			ts_err("Failed to disable iovdd:%d", ret);
	}

	if (avdd_gpio > 0)
		gpio_direction_output(avdd_gpio, 0);
	else if (cd->avdd) {
		ret = regulator_disable(cd->avdd);
		if (ret)
			ts_err("Failed to disable iovdd:%d", ret);
	}
	usleep_range(10000, 11000);
	return ret;
}

#define GOODIX_SLEEP_CMD	0x84
int brl_suspend(struct goodix_ts_core *cd)
{
	struct goodix_ts_cmd sleep_cmd;

	sleep_cmd.cmd = GOODIX_SLEEP_CMD;
	sleep_cmd.len = 4;
	if (cd->hw_ops->send_cmd(cd, &sleep_cmd))
		ts_err("failed send sleep cmd");

	return 0;
}

int brl_resume(struct goodix_ts_core *cd)
{
	return cd->hw_ops->reset(cd, GOODIX_NORMAL_RESET_DELAY_MS);
}

#define GOODIX_GESTURE_CMD         0xA6
#define GOODIX_DOUBLE_CLICK_BIT    0x80
#define GOODIX_FINGER_PRINT_BIT    0x20
#define GOODIX_SINGER_CLICK_BIT    0x10
int brl_gesture(struct goodix_ts_core *cd, int gesture_type)
{
	struct goodix_ts_cmd cmd;

	cmd.cmd = GOODIX_GESTURE_CMD;
	cmd.len = 6;
	cmd.data[0] = 0xFF;
	cmd.data[1] = 0xFF;

	if (gesture_type & DOUBLE_TAP_EN)
		cmd.data[0] &= ~GOODIX_DOUBLE_CLICK_BIT;

	if (gesture_type & SINGLE_TAP_EN)
		cmd.data[1] &= ~GOODIX_SINGER_CLICK_BIT;

#ifdef TOUCH_FOD_SUPPORT
	if (gesture_type & FOD_EN)
		cmd.data[1] &= ~GOODIX_FINGER_PRINT_BIT;
#endif

	ts_debug("BRL cmd 0 is 0x%x", cmd.data[0]);
	ts_debug("BRL cmd 1 is 0x%x", cmd.data[1]);
	if (cd->hw_ops->send_cmd(cd, &cmd))
		ts_err("failed send gesture cmd");

	return 0;
}

static int brl_dev_confirm(struct goodix_ts_core *cd)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int ret = 0;
	int retry = GOODIX_RETRY_3;
	u8 tx_buf[8] = {0};
	u8 rx_buf[8] = {0};

	memset(tx_buf, DEV_CONFIRM_VAL, sizeof(tx_buf));
	while (retry--) {
		ret = hw_ops->write(cd, BOOTOPTION_ADDR,
			tx_buf, sizeof(tx_buf));
		if (ret < 0)
			return ret;
		ret = hw_ops->read(cd, BOOTOPTION_ADDR,
			rx_buf, sizeof(rx_buf));
		if (ret < 0)
			return ret;
		if (!memcmp(tx_buf, rx_buf, sizeof(tx_buf)))
			break;
		usleep_range(5000, 5100);
	}

	if (retry < 0) {
		ret = -EINVAL;
		ts_err("device confirm failed, rx_buf:%*ph", 8, rx_buf);
	}

	return ret;
}

static int brl_reset(struct goodix_ts_core *cd, int delay)
{
	ts_info("chip_reset");

	gpio_direction_output(cd->board_data.reset_gpio, 0);
	usleep_range(2000, 2100);
	gpio_direction_output(cd->board_data.reset_gpio, 1);
	if (delay < 20)
		usleep_range(delay * 1000, delay * 1000 + 100);
	else
		msleep(delay);

	return brl_select_spi_mode(cd);
}

static int brl_irq_enbale(struct goodix_ts_core *cd, bool enable)
{
	struct irq_desc *desc;

	desc = irq_to_desc(cd->irq);
	ts_info("irq enable: %d depth: %d", enable, desc->depth);
	if (enable && !atomic_cmpxchg(&cd->irq_enabled, 0, 1)) {
		while(desc->depth > 1) {
			ts_info("irq depth unbalance, enable irq again");
			enable_irq(cd->irq);
		}
		enable_irq(cd->irq);
		ts_info("Irq enabled");
		return 0;
	}

	if (!enable && atomic_cmpxchg(&cd->irq_enabled, 1, 0)) {
		disable_irq_nosync(cd->irq);
		ts_info("Irq disabled");
		return 0;
	}
	ts_info("warnning: irq deepth inbalance!");
	return 0;
}

static int brl_read(struct goodix_ts_core *cd, unsigned int addr,
		unsigned char *data, unsigned int len)
{
	struct goodix_bus_interface *bus = cd->bus;

	return bus->read(bus->dev, addr, data, len);
}

static int brl_write(struct goodix_ts_core *cd, unsigned int addr,
		unsigned char *data, unsigned int len)
{
	struct goodix_bus_interface *bus = cd->bus;

	return bus->write(bus->dev, addr, data, len);
}

/* command ack info */
#define CMD_ACK_IDLE             0x01
#define CMD_ACK_BUSY             0x02
#define CMD_ACK_BUFFER_OVERFLOW  0x03
#define CMD_ACK_CHECKSUM_ERROR   0x04
#define CMD_ACK_OK               0x80

#define GOODIX_CMD_RETRY 6
static int brl_send_cmd(struct goodix_ts_core *cd,
	struct goodix_ts_cmd *cmd)
{
	int ret, retry, i;
	struct goodix_ts_cmd cmd_ack;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	if (misc->cmd_addr == 0x0000) {
		ts_err("invalid cmd addr:0x0000, skip cmd");
		ret = -EINVAL;
	}

	cmd->state = 0;
	cmd->ack = 0;
	goodix_append_checksum(&(cmd->buf[2]), cmd->len - 2,
		CHECKSUM_MODE_U8_LE);
	ts_debug("cmd data %*ph", cmd->len, &(cmd->buf[2]));

	retry = 0;
	while (retry++ < GOODIX_CMD_RETRY) {
		ret = hw_ops->write(cd, misc->cmd_addr,
				cmd->buf, sizeof(*cmd));
		if (ret < 0) {
			ts_err("failed write command");
			return ret;
		}

		for (i = 0; i < GOODIX_CMD_RETRY; i++) {
			/* check command result */
			ret = hw_ops->read(cd, misc->cmd_addr,
				cmd_ack.buf, sizeof(cmd_ack));
			if (ret < 0) {
				ts_err("failed read command ack, %d", ret);
				return ret;
			}
			ts_debug("cmd ack data %*ph",
				(int)sizeof(cmd_ack), cmd_ack.buf);
			if (cmd_ack.ack == CMD_ACK_OK) {
				usleep_range(10000, 11000);
				return 0;
			}
			if (cmd_ack.ack == CMD_ACK_BUSY ||
				cmd_ack.ack == 0x00) {
				usleep_range(1000, 1100);
				continue;
			}
			if (cmd_ack.ack == CMD_ACK_BUFFER_OVERFLOW)
				usleep_range(10000, 11000);
			usleep_range(1000, 1100);
			break;
		}
	}
	ts_err("failed get valid cmd ack");
	return -EINVAL;
}

static u32 checksum16_u32(const u8 *data, int size)
{
	int i;
	u32 checksum = 0;
	for (i = 0; i < size; i += 2)
	checksum += data[i] | (data[i + 1] << 8);
	return checksum;
}
static int package_data(int addr, int data_len, u8 *data_buf, u8 *output_buf)
{
	flash_head_info_t *head_info;
	head_info = (flash_head_info_t *)output_buf;
	head_info->address = cpu_to_le32(addr);
	head_info->length = cpu_to_le32(data_len);
	memcpy(output_buf + sizeof(flash_head_info_t), data_buf, data_len);
	head_info->checksum = cpu_to_le32(checksum16_u32(output_buf + 4, data_len + 8));
	return data_len + sizeof(flash_head_info_t);
}
#define FLASH_CMD_STATE_READY		0x04
#define FLASH_CMD_STATE_CHECKERR	0x05
#define FLASH_CMD_STATE_DENY		0x06
#define FLASH_CMD_STATE_OKAY		0x07
#define FLASH_CMD_R_START			0x09
#define FLASH_CMD_W_START			0x0A
#define FLASH_CMD_RW_FINISH			0x0B
#define FLASH_CMD_FINISH			0x0C
#define FLASH_CMD_STATUS_PASS		0x80
#define FLASH_CMD_RETRY				100
static int goodix_flash_cmd(struct goodix_ts_core *cd,
		uint8_t cmd, uint8_t status,
		int retry_count)
{
	u32 cmd_addr = cd->ic_info.misc.cmd_addr;
	int ret, i;
	u8 cmd_buf[6] = {0};
	u8 tmp_buf[6] = {0};
	u16 checksum = 0;
	int retry = 5;
	cmd_buf[0] = 0; // state
	cmd_buf[1] = 0; // ack
	cmd_buf[2] = 4; // len
	cmd_buf[3] = cmd; // len
	checksum = cmd_buf[2] + cmd_buf[3];
	cmd_buf[4] = checksum & 0xFF; // checksum_L
	cmd_buf[5] = (checksum >> 8) & 0xFF; // checksum_H
resend_cmd:
	ret = brl_write(cd, cmd_addr, cmd_buf, sizeof(cmd_buf));
	if (ret) {
		ts_err("failed send cmd 0x%x", cmd);
		return ret;
	}
	for (i = 0; i < retry_count; i++) {
		ret = brl_read(cd, cmd_addr, tmp_buf, sizeof(tmp_buf));
		if (ret) {
			ts_err("failed read cmd ack info");
			usleep_range(5000, 5100);
			continue;
		}
		if (tmp_buf[3] != cmd_buf[3] && --retry) {
			ts_err("command readback unequal need resend, 0x%x != 0x%x, retry %d",
				tmp_buf[3], cmd_buf[3], retry);
			goto resend_cmd;
		}
		if (tmp_buf[0] == 0) {
			ts_debug("cmd state not ready, retry");
			usleep_range(3000, 3100);
			continue;
		}
		if (tmp_buf[0] == status) {
			ts_info("get target state 0x%x, retry cnt: %d", status, i);
			usleep_range(5000, 5100);
			return 0;
		}
		ts_err("cmd error");
		return -1;
	}
	ts_err("failed get target state 0x%x != 0x%x, retry %d", tmp_buf[0], status, i);
	return -1;
}
static int brl_flash_read(struct goodix_ts_core *cd,
		unsigned int addr, unsigned char *buf,
		unsigned int len)
{
	int ret = -1;
	u8 *tmp_buf;
	u32 buffer_addr = cd->ic_info.misc.fw_buffer_addr;
	u32 checksum = 0;
	int read_len = len;
	flash_head_info_t head_info = {0};

	if ((read_len % 2) != 0)
		read_len++;

	tmp_buf = kzalloc(len + sizeof(flash_head_info_t), GFP_KERNEL);
	if (!tmp_buf)
		return -1;

	head_info.address = cpu_to_le32(addr);
	head_info.length = cpu_to_le32(read_len);
	head_info.checksum = cpu_to_le32(checksum16_u32((u8 *)&head_info.address, 8));
	//write flash_w cmd
	ret = goodix_flash_cmd(cd, FLASH_CMD_R_START, FLASH_CMD_STATE_READY, FLASH_CMD_RETRY);
	if (ret) {
		ts_err("failed enter flash read state");
		goto read_end;
	}

	ret = brl_write(cd, buffer_addr, (u8 *)&head_info, sizeof(head_info));
	if (ret) {
		ts_err("failed write flash head info");
		goto read_end;
	}

	ret = goodix_flash_cmd(cd, FLASH_CMD_RW_FINISH, FLASH_CMD_STATE_OKAY, FLASH_CMD_RETRY);
	if (ret) {
		ts_err("faild read flash ready state");
		goto read_end;
	}

	ret = brl_read(cd, buffer_addr, tmp_buf, read_len + sizeof(flash_head_info_t));
	if (ret) {
		ts_err("failed read data len %lu", read_len + sizeof(flash_head_info_t));
		goto read_end;
	}
	checksum = checksum16_u32(tmp_buf + 4, read_len + sizeof(flash_head_info_t) - 4);
	if (checksum != le32_to_cpup((u32 *)tmp_buf)) {
		ts_err("read back data checksum error 0x%x != 0x%x", checksum, le32_to_cpup((u32 *)tmp_buf));
		ret = -1;
		goto read_end;
	}
	memcpy(buf, tmp_buf + sizeof(flash_head_info_t), read_len);

read_end:
	ret = goodix_flash_cmd(cd, FLASH_CMD_FINISH, FLASH_CMD_STATUS_PASS, FLASH_CMD_RETRY);
	if (ret)
		ts_err("failed wait for flash ready, ret: %d", ret);
	kfree(tmp_buf);
	msleep(20);
	return ret;
}
static int brl_flash_write(struct goodix_ts_core *cd,
		unsigned int addr, unsigned char *buf,
		unsigned int len)
{
	int ret = -1, i;
	u8 *pack_buf = NULL;
	u32 buffer_addr = cd->ic_info.misc.fw_buffer_addr;
	int package_len = 0;

	//write flash_w cmd
	ret = goodix_flash_cmd(cd, FLASH_CMD_W_START, FLASH_CMD_STATE_READY, FLASH_CMD_RETRY);
	if (ret) {
		ts_err("failed enter flash write state");
		return ret;
	}
	pack_buf = kzalloc(sizeof(flash_head_info_t) + FLASH_WRITE_MAX_LEN, GFP_KERNEL);
	if (!pack_buf) {
		ts_err("Failed to alloc memory\n");
		return -1;
	}
	package_len = package_data(addr, len, buf, pack_buf);
	for (i = 0; i < 3; i++) {
		ret = brl_write(cd, buffer_addr, pack_buf, package_len);
		if (!ret)
			break;
	}
	if (ret)
		ts_err("fail to write data to fw buffer!");
	ret = goodix_flash_cmd(cd, FLASH_CMD_RW_FINISH, FLASH_CMD_STATE_OKAY, FLASH_CMD_RETRY);
	if (ret)
		ts_err("failed send flash finish command");
	ret = goodix_flash_cmd(cd, FLASH_CMD_FINISH, FLASH_CMD_STATUS_PASS, FLASH_CMD_RETRY);
	if (ret)
		ts_err("failed wait for flash ready, ret: %d", ret);
	ts_info(">>>>>>send package to flash finish!");
	kfree(pack_buf);
	msleep(20);
	return ret;
}
#pragma  pack(1)
struct goodix_config_head {
	union {
		struct {
			u8 panel_name[8];
			u8 fw_pid[8];
			u8 fw_vid[4];
			u8 project_name[8];
			u8 file_ver[2];
			u32 cfg_id;
			u8 cfg_ver;
			u8 cfg_time[8];
			u8 reserved[15];
			u8 flag;
			u16 cfg_len;
			u8 cfg_num;
			u16 checksum;
		};
		u8 buf[64];
	};
};
#pragma pack()

#define CONFIG_CMD_LEN			4
#define CONFIG_CMD_START		0x04
#define CONFIG_CMD_START_BD		0x0A
#define CONFIG_CMD_WRITE		0x05
#define CONFIG_CMD_WRITE_BD		0x0B
#define CONFIG_CMD_EXIT			0x06
#define CONFIG_CMD_EXIT_BD		0x0C
#define CONFIG_CMD_READ_START	0x07
#define CONFIG_CMD_READ_EXIT	0x08

#define CONFIG_CMD_STATUS_PASS	0x80
#define CONFIG_CMD_START_STATUS 0x04
#define CONFIG_CMD_WRITE_STATUS 0x07
#define CONFIG_CMD_WAIT_RETRY	20

static int wait_cmd_status(struct goodix_ts_core *cd,
	u8 target_status, int retry)
{
	struct goodix_ts_cmd cmd_ack;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int i, ret;

	for (i = 0; i < retry; i++) {
		ret = hw_ops->read(cd, misc->cmd_addr, cmd_ack.buf,
			sizeof(cmd_ack));
		if (!ret && cmd_ack.state == target_status) {
			ts_info("status check pass  addr 0x%x,  cmd buf %*ph", misc->cmd_addr, (int)sizeof(cmd_ack), cmd_ack.buf);
			return 0;
		}
		msleep(20);
	}

	ts_err("cmd status not ready, retry %d, addr 0x%x,  cmd buf %*ph, ack 0x%x, status 0x%x, ret %d",
			i, misc->cmd_addr, (int)sizeof(cmd_ack), cmd_ack.buf, cmd_ack.ack, cmd_ack.state, ret);
	return -EINVAL;
}

static int send_cfg_cmd(struct goodix_ts_core *cd,
	struct goodix_ts_cmd *cfg_cmd, u8 target_status)
{
	int ret;
	ts_debug("enter cmd:0x%*ph", (int)sizeof(cfg_cmd), cfg_cmd);
	ret = cd->hw_ops->send_cmd(cd, cfg_cmd);
	if (ret) {
		ts_err("failed write cfg prepare cmd %d, cmd:0x%*ph", ret,  (int)sizeof(cfg_cmd), cfg_cmd);
		return ret;
	}

	ret = wait_cmd_status(cd, target_status, CONFIG_CMD_WAIT_RETRY);
	if (ret) {
		ts_err("failed wait for fw ready for config, %d", ret);
		return ret;
	}

	return 0;
}

#define CONFIG_DATA_ADDR_BRD 0x3E000
#define CONFIG_DATA_HEAD_BD 12
#if 0
/* for solve tp_selftest fail */
static int brl_package_config(u8 *cfg, int len, u8 *buf)
{
	int i;
	u32 checksum = 0;
	u32 cfg_data_addr = CONFIG_DATA_ADDR_BRD;

	if (!cfg || !buf) {
		ts_err("cfg or buf is NULL");
		return -EINVAL;
	}

	buf[4] = cfg_data_addr & 0xFF;
	buf[5] = (cfg_data_addr >> 8) & 0xFF;
	buf[6] = (cfg_data_addr >> 16) & 0xFF;
	buf[7] = (cfg_data_addr >> 24) & 0xFF;
	buf[8] = len & 0xFF;
	buf[9] = (len >> 8) & 0xFF;
	buf[10] = (len >> 16) & 0xFF;
	buf[11] = (len >> 24) & 0xFF;
	memcpy(&buf[CONFIG_DATA_HEAD_BD], cfg, len);

	for (i = 4; i < len + CONFIG_DATA_HEAD_BD; i += 2)
		checksum += (buf[i] + (buf[1 + i] << 8));

	buf[0] = checksum & 0xFF;
	buf[1] = (checksum >> 8) & 0xFF;
	buf[2] = (checksum >> 16) & 0xFF;
	buf[3] = (checksum >> 24) & 0xFF;

	ts_debug("buf:0x%*ph", CONFIG_DATA_HEAD_BD, buf);

	return 0;
}
#endif 
static int brl_send_config(struct goodix_ts_core *cd, u8 *cfg, int len)
{
	int ret;
	u8 *tmp_buf = NULL;
	u8 *tx_buf = cfg;
	/* u8 buffer[4096]; */
	int tx_len = len;
	struct goodix_ts_cmd cfg_cmd;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	u8 cmd_start = CONFIG_CMD_START;
	u8 cmd_write = CONFIG_CMD_WRITE;
	u8 cmd_exit = CONFIG_CMD_EXIT;
	u8 start_status = CONFIG_CMD_STATUS_PASS;
	u8 write_status = CONFIG_CMD_STATUS_PASS;
	u8 exit_status = CONFIG_CMD_STATUS_PASS;

	if (len > misc->fw_buffer_max_len) {
		ts_err("config len exceed limit %d > %d",
			len, misc->fw_buffer_max_len);
		return -EINVAL;
	}

#if 0
/* remove unused stack */
	if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
		cmd_start = CONFIG_CMD_START_BD;
		cmd_write = CONFIG_CMD_WRITE_BD;
		cmd_exit = CONFIG_CMD_EXIT_BD;
		start_status = CONFIG_CMD_START_STATUS;
		write_status = CONFIG_CMD_WRITE_STATUS;
		tx_buf = buffer;
		tx_len = len + CONFIG_DATA_HEAD_BD;
		ret = brl_package_config(cfg, len, buffer);
		if (ret < 0)
			return ret;
	}
#endif
	tmp_buf = kzalloc(tx_len, GFP_KERNEL);
	if (!tmp_buf) {
		ts_err("try alloc (tx_len=%d) memory err", tx_len);
		return -ENOMEM;
	}

	cfg_cmd.len = CONFIG_CMD_LEN;
	cfg_cmd.cmd = cmd_start;
	ret = send_cfg_cmd(cd, &cfg_cmd, start_status);
	if (ret) {
		ts_err("failed write cfg prepare cmd %d", ret);
		goto exit;
	}

	ts_debug("try send config to 0x%x, len %d", misc->fw_buffer_addr, tx_len);
	ret = hw_ops->write(cd, misc->fw_buffer_addr, tx_buf, tx_len);
	if (ret) {
		ts_err("failed write config data, %d", ret);
		goto exit;
	}
	ret = hw_ops->read(cd, misc->fw_buffer_addr, tmp_buf, tx_len);
	if (ret) {
		ts_err("failed read back config data");
		goto exit;
	}

	if (memcmp(tx_buf, tmp_buf, tx_len)) {
		ts_err("config data read back compare file");
		ret = -EINVAL;
		goto exit;
	}
	/* notify fw for recive config */
	memset(cfg_cmd.buf, 0, sizeof(cfg_cmd));
	cfg_cmd.len = CONFIG_CMD_LEN;
	cfg_cmd.cmd = cmd_write;
	ret = send_cfg_cmd(cd, &cfg_cmd, write_status);
	if (ret)
		ts_err("failed send config data ready cmd %d", ret);

exit:
	memset(cfg_cmd.buf, 0, sizeof(cfg_cmd));
	cfg_cmd.len = CONFIG_CMD_LEN;
	cfg_cmd.cmd = cmd_exit;
	if (send_cfg_cmd(cd, &cfg_cmd, exit_status)) {
		ts_err("failed send config write end command");
		ret = -EINVAL;
	}

	if (!ret) {
		ts_info("success send config");
		/*msleep(100);*/
	}

	kfree(tmp_buf);
	brl_reset(cd, 100);
	return ret;
}

/*
 * return: return config length on succes, other wise return < 0
 **/
static int brl_read_config(struct goodix_ts_core *cd, u8 *cfg, int size)
{
	int ret;
	struct goodix_ts_cmd cfg_cmd;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_config_head cfg_head;

	if (!cfg)
		return -EINVAL;

	cfg_cmd.len = CONFIG_CMD_LEN;
	cfg_cmd.cmd = CONFIG_CMD_READ_START;
	ret = send_cfg_cmd(cd, &cfg_cmd, CONFIG_CMD_STATUS_PASS);
	if (ret) {
		ts_err("failed send config read prepare command");
		return ret;
	}

	ret = hw_ops->read(cd, misc->fw_buffer_addr,
				cfg_head.buf, sizeof(cfg_head));
	if (ret) {
		ts_err("failed read config head %d", ret);
		goto exit;
	}

	if (checksum_cmp(cfg_head.buf, sizeof(cfg_head), CHECKSUM_MODE_U8_LE)) {
		ts_err("config head checksum error");
		ret = -EINVAL;
		goto exit;
	}

	cfg_head.cfg_len = le16_to_cpu(cfg_head.cfg_len);
	if (cfg_head.cfg_len > misc->fw_buffer_max_len ||
	    cfg_head.cfg_len > size) {
		ts_err("cfg len exceed buffer size %d > %d", cfg_head.cfg_len,
				misc->fw_buffer_max_len);
		ret = -EINVAL;
		goto exit;
	}

	memcpy(cfg, cfg_head.buf, sizeof(cfg_head));
	ret = hw_ops->read(cd, misc->fw_buffer_addr + sizeof(cfg_head),
			cfg + sizeof(cfg_head), cfg_head.cfg_len);
	if (ret) {
		ts_err("failed read cfg pack, %d", ret);
		goto exit;
	}

	ts_info("config len %d", cfg_head.cfg_len);
	if (checksum_cmp(cfg + sizeof(cfg_head),
				cfg_head.cfg_len, CHECKSUM_MODE_U16_LE)) {
		ts_err("config body checksum error");
		ret = -EINVAL;
		goto exit;
	}
	ts_info("success read config data: len %zu",
		cfg_head.cfg_len + sizeof(cfg_head));
exit:
	memset(cfg_cmd.buf, 0, sizeof(cfg_cmd));
	cfg_cmd.len = CONFIG_CMD_LEN;
	cfg_cmd.cmd = CONFIG_CMD_READ_EXIT;
	if (send_cfg_cmd(cd, &cfg_cmd, CONFIG_CMD_STATUS_PASS)) {
		ts_err("failed send config read finish command");
		ret = -EINVAL;
	}
	if (ret)
		return -EINVAL;
	return cfg_head.cfg_len + sizeof(cfg_head);
}

/*
 *	return: 0 for no error.
 *	GOODIX_EBUS when encounter a bus error
 *	GOODIX_ECHECKSUM version checksum error
 *	GOODIX_EVERSION  patch ID compare failed,
 *	in this case the sensorID is valid.
 */
static int brl_read_version(struct goodix_ts_core *cd,
			struct goodix_fw_version *version)
{
	int ret, i;
	u32 fw_addr;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	u8 buf[sizeof(struct goodix_fw_version)] = {0};
	u8 temp_pid[8] = {0};

	if (cd->bus->ic_type == IC_TYPE_BERLIN_A)
		fw_addr = FW_VERSION_INFO_ADDR_BRA;
	else
		fw_addr = FW_VERSION_INFO_ADDR;

	for (i = 0; i < 3; i++) {
		ret = hw_ops->read(cd, fw_addr, buf, sizeof(buf));
		if (ret) {
			ts_info("read fw version: %d, retry %d", ret, i);
			ret = -GOODIX_EBUS;
			usleep_range(5000, 5100);
			continue;
		}

		if (!checksum_cmp(buf, sizeof(buf), CHECKSUM_MODE_U8_LE))
			break;

		ts_info("invalid fw version: checksum error!");
		ts_info("fw version:%*ph", (int)sizeof(buf), buf);
		ret = -GOODIX_ECHECKSUM;
		usleep_range(10000, 11000);
	}

	if (ret) {
		ts_err("failed get valied fw version");
		return ret;
	}
	memcpy(version, buf, sizeof(*version));
	memcpy(temp_pid, version->rom_pid, sizeof(version->rom_pid));
	ts_info("rom_pid:%s", temp_pid);
	ts_info("rom_vid:%*ph", (int)sizeof(version->rom_vid),
		version->rom_vid);
	ts_info("pid:%s", version->patch_pid);
	ts_info("vid:%*ph", (int)sizeof(version->patch_vid),
		version->patch_vid);
	ts_info("sensor_id:%d", version->sensor_id);

	return 0;
}

#define LE16_TO_CPU(x)  (x = le16_to_cpu(x))
#define LE32_TO_CPU(x)  (x = le32_to_cpu(x))
static int convert_ic_info(struct goodix_ic_info *info, const u8 *data)
{
	int i;
	struct goodix_ic_info_version *version = &info->version;
	struct goodix_ic_info_feature *feature = &info->feature;
	struct goodix_ic_info_param *parm = &info->parm;
	struct goodix_ic_info_misc *misc = &info->misc;
	struct goodix_ic_info_other *other = &info->other;

	info->length = le16_to_cpup((__le16 *)data);

	data += 2;
	memcpy(version, data, sizeof(*version));
	version->config_id = le32_to_cpu(version->config_id);

	data += sizeof(struct goodix_ic_info_version);
	memcpy(feature, data, sizeof(*feature));
	feature->freqhop_feature =
		le16_to_cpu(feature->freqhop_feature);
	feature->calibration_feature =
		le16_to_cpu(feature->calibration_feature);
	feature->gesture_feature =
		le16_to_cpu(feature->gesture_feature);
	feature->side_touch_feature =
		le16_to_cpu(feature->side_touch_feature);
	feature->stylus_feature =
		le16_to_cpu(feature->stylus_feature);

	data += sizeof(struct goodix_ic_info_feature);
	parm->drv_num = *(data++);
	parm->sen_num = *(data++);
	parm->button_num = *(data++);
	parm->force_num = *(data++);
	parm->active_scan_rate_num = *(data++);
	if (parm->active_scan_rate_num > MAX_SCAN_RATE_NUM) {
		ts_err("invalid scan rate num %d > %d",
			parm->active_scan_rate_num, MAX_SCAN_RATE_NUM);
		return -EINVAL;
	}
	for (i = 0; i < parm->active_scan_rate_num; i++)
		parm->active_scan_rate[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->active_scan_rate_num * 2;
	parm->mutual_freq_num = *(data++);
	if (parm->mutual_freq_num > MAX_SCAN_FREQ_NUM) {
		ts_err("invalid mntual freq num %d > %d",
			parm->mutual_freq_num, MAX_SCAN_FREQ_NUM);
		return -EINVAL;
	}
	for (i = 0; i < parm->mutual_freq_num; i++)
		parm->mutual_freq[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->mutual_freq_num * 2;
	parm->self_tx_freq_num = *(data++);
	if (parm->self_tx_freq_num > MAX_SCAN_FREQ_NUM) {
		ts_err("invalid tx freq num %d > %d",
			parm->self_tx_freq_num, MAX_SCAN_FREQ_NUM);
		return -EINVAL;
	}
	for (i = 0; i < parm->self_tx_freq_num; i++)
		parm->self_tx_freq[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->self_tx_freq_num * 2;
	parm->self_rx_freq_num = *(data++);
	if (parm->self_rx_freq_num > MAX_SCAN_FREQ_NUM) {
		ts_err("invalid rx freq num %d > %d",
			parm->self_rx_freq_num, MAX_SCAN_FREQ_NUM);
		return -EINVAL;
	}
	for (i = 0; i < parm->self_rx_freq_num; i++)
		parm->self_rx_freq[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->self_rx_freq_num * 2;
	parm->stylus_freq_num = *(data++);
	if (parm->stylus_freq_num > MAX_FREQ_NUM_STYLUS) {
		ts_err("invalid stylus freq num %d > %d",
			parm->stylus_freq_num, MAX_FREQ_NUM_STYLUS);
		return -EINVAL;
	}
	for (i = 0; i < parm->stylus_freq_num; i++)
		parm->stylus_freq[i] =
			le16_to_cpup((__le16 *)(data + i * 2));

	data += parm->stylus_freq_num * 2;
	memcpy(misc, data, sizeof(*misc));
	misc->cmd_addr = le32_to_cpu(misc->cmd_addr);
	misc->cmd_max_len = le16_to_cpu(misc->cmd_max_len);
	misc->cmd_reply_addr = le32_to_cpu(misc->cmd_reply_addr);
	misc->cmd_reply_len = le16_to_cpu(misc->cmd_reply_len);
	misc->fw_state_addr = le32_to_cpu(misc->fw_state_addr);
	misc->fw_state_len = le16_to_cpu(misc->fw_state_len);
	misc->fw_buffer_addr = le32_to_cpu(misc->fw_buffer_addr);
	misc->fw_buffer_max_len = le16_to_cpu(misc->fw_buffer_max_len);
	misc->frame_data_addr = le32_to_cpu(misc->frame_data_addr);
	misc->frame_data_head_len = le16_to_cpu(misc->frame_data_head_len);
	misc->fw_attr_len = le16_to_cpu(misc->fw_attr_len);
	misc->fw_log_len = le16_to_cpu(misc->fw_log_len);
	misc->stylus_struct_len = le16_to_cpu(misc->stylus_struct_len);
	misc->mutual_struct_len = le16_to_cpu(misc->mutual_struct_len);
	misc->self_struct_len = le16_to_cpu(misc->self_struct_len);
	misc->noise_struct_len = le16_to_cpu(misc->noise_struct_len);
	misc->touch_data_addr = le32_to_cpu(misc->touch_data_addr);
	misc->touch_data_head_len = le16_to_cpu(misc->touch_data_head_len);
	misc->point_struct_len = le16_to_cpu(misc->point_struct_len);
	LE16_TO_CPU(misc->panel_x);
	LE16_TO_CPU(misc->panel_y);
	LE32_TO_CPU(misc->mutual_rawdata_addr);
	LE32_TO_CPU(misc->mutual_diffdata_addr);
	LE32_TO_CPU(misc->mutual_refdata_addr);
	LE32_TO_CPU(misc->self_rawdata_addr);
	LE32_TO_CPU(misc->self_diffdata_addr);
	LE32_TO_CPU(misc->self_refdata_addr);
	LE32_TO_CPU(misc->iq_rawdata_addr);
	LE32_TO_CPU(misc->iq_refdata_addr);
	LE32_TO_CPU(misc->im_rawdata_addr);
	LE16_TO_CPU(misc->im_readata_len);
	LE32_TO_CPU(misc->noise_rawdata_addr);
	LE16_TO_CPU(misc->noise_rawdata_len);
	LE32_TO_CPU(misc->stylus_rawdata_addr);
	LE16_TO_CPU(misc->stylus_rawdata_len);
	LE32_TO_CPU(misc->noise_data_addr);
	LE32_TO_CPU(misc->esd_addr);
	LE32_TO_CPU(misc->auto_scan_cmd_addr);
	LE32_TO_CPU(misc->auto_scan_info_addr);
	LE16_TO_CPU(misc->normalize_k_version);

	data += sizeof(*misc);
	memcpy((u8 *)other, data, sizeof(*other));
	LE16_TO_CPU(other->screen_max_x);
	LE16_TO_CPU(other->screen_max_y);

	return 0;
}

extern struct goodix_ts_core *goodix_core_data;

static void print_ic_info(struct goodix_ic_info *ic_info)
{
	struct goodix_ic_info_version *version = &ic_info->version;
	struct goodix_ic_info_feature *feature = &ic_info->feature;
	struct goodix_ic_info_param *parm = &ic_info->parm;
	struct goodix_ic_info_misc *misc = &ic_info->misc;
	struct goodix_ic_info_other *other = &ic_info->other;

	ts_debug("ic_info_length:                %d",
		ic_info->length);
	ts_debug("info_customer_id:              0x%01X",
		version->info_customer_id);
	ts_debug("info_version_id:               0x%01X",
		version->info_version_id);
	ts_debug("ic_die_id:                     0x%01X",
		version->ic_die_id);
	ts_debug("ic_version_id:                 0x%01X",
		version->ic_version_id);
	ts_debug("config_id:                     0x%4X",
		version->config_id);
	ts_info("config_version:                0x%01X",
		version->config_version);
	ts_debug("frame_data_customer_id:        0x%01X",
		version->frame_data_customer_id);
	ts_debug("frame_data_version_id:         0x%01X",
		version->frame_data_version_id);
	ts_debug("touch_data_customer_id:        0x%01X",
		version->touch_data_customer_id);
	ts_debug("touch_data_version_id:         0x%01X",
		version->touch_data_version_id);
	ts_debug("freqhop_feature:               0x%04X",
		feature->freqhop_feature);
	ts_debug("calibration_feature:           0x%04X",
		feature->calibration_feature);
	ts_debug("gesture_feature:               0x%04X",
		feature->gesture_feature);
	ts_debug("side_touch_feature:            0x%04X",
		feature->side_touch_feature);
	ts_debug("stylus_feature:                0x%04X",
		feature->stylus_feature);

	ts_debug("Drv*Sen,Button,Force num:      %d x %d, %d, %d",
		parm->drv_num, parm->sen_num,
		parm->button_num, parm->force_num);
	ts_debug("scan rate num:                 %d", parm->active_scan_rate_num);
	ts_debug("mutual freq num:               %d", parm->mutual_freq_num);

	ts_debug("Cmd:                           0x%04X, %d",
		misc->cmd_addr, misc->cmd_max_len);
	ts_debug("Cmd-Reply:                     0x%04X, %d",
		misc->cmd_reply_addr, misc->cmd_reply_len);
	ts_debug("FW-State:                      0x%04X, %d",
		misc->fw_state_addr, misc->fw_state_len);
	ts_debug("FW-Buffer:                     0x%04X, %d",
		misc->fw_buffer_addr, misc->fw_buffer_max_len);
	ts_info("Frame-Data:                    0x%04X, %d",
		misc->frame_data_addr, misc->frame_data_head_len);
	ts_info("Touch-Data:                    0x%04X, %d",
		misc->touch_data_addr, misc->touch_data_head_len);
	ts_debug("point_struct_len:              %d",
		misc->point_struct_len);
	ts_debug("panel_x:                       %d",
		misc->panel_x);
	ts_debug("panel_y:                       %d",
		misc->panel_y);
	ts_info("panel_max_x:                   %d", other->screen_max_x);
	ts_info("panel_max_y:                   %d", other->screen_max_y);
	ts_debug("mutual_rawdata_addr:           0x%04X",
		misc->mutual_rawdata_addr);
	ts_debug("mutual_diffdata_addr:          0x%04X",
		misc->mutual_diffdata_addr);
	ts_debug("mutual_refdata_addr:           0x%04X",
		misc->mutual_refdata_addr);
	ts_debug("self_rawdata_addr:             0x%04X",
		misc->self_rawdata_addr);
	ts_debug("self_diffdata_addr:            0x%04X",
		misc->self_diffdata_addr);
	ts_debug("stylus_rawdata_addr:           0x%04X, %d",
		misc->stylus_rawdata_addr, misc->stylus_rawdata_len);
	ts_debug("esd_addr:                      0x%04X",
		misc->esd_addr);
/*
#ifdef TOUCH_THP_SUPPORT
	if (goodix_core_data->enable_touch_raw)
#endif
		ts_info("normalize K version:            0x%02X",
			misc->normalize_k_version);
*/
    //n11u's drive no longer needs to send K-matrix to fw
}

static int brl_get_ic_info(struct goodix_ts_core *cd,
	struct goodix_ic_info *ic_info)
{
	int ret, i;
	u16 length = 0;
	u32 ic_addr;
	u8 afe_data[GOODIX_IC_INFO_MAX_LEN] = {0};
	struct goodix_ts_hw_ops *hw_ops = NULL;

	if (!cd || !ic_info)
		return -EINVAL;

	hw_ops = cd->hw_ops;

	if (cd->bus->ic_type == IC_TYPE_BERLIN_A)
		ic_addr = GOODIX_IC_INFO_ADDR_BRA;
	else
		ic_addr = GOODIX_IC_INFO_ADDR;

	for (i = 0; i < GOODIX_RETRY_3; i++) {
		ret = hw_ops->read(cd, ic_addr,
				   (u8 *)&length, sizeof(length));
		if (ret) {
			ts_info("failed get ic info length, %d", ret);
			usleep_range(5000, 5100);
			continue;
		}
		length = le16_to_cpu(length);
		if (length >= GOODIX_IC_INFO_MAX_LEN) {
			ts_info("invalid ic info length %d, retry %d",
				length, i);
			continue;
		}

		ret = hw_ops->read(cd, ic_addr, afe_data, length);
		if (ret) {
			ts_info("failed get ic info data, %d", ret);
			usleep_range(5000, 5100);
			continue;
		}
		/* judge whether the data is valid */
		if (is_risk_data((const uint8_t *)afe_data, length)) {
			ts_info("fw info data invalid");
			usleep_range(5000, 5100);
			continue;
		}
		if (checksum_cmp((const uint8_t *)afe_data,
					length, CHECKSUM_MODE_U8_LE)) {
			ts_info("fw info checksum error!");
			usleep_range(5000, 5100);
			continue;
		}
		break;
	}
	if (i == GOODIX_RETRY_3) {
		ts_err("failed get ic info");
		return -EINVAL;
	}

	ret = convert_ic_info(ic_info, afe_data);
	if (ret) {
		ts_err("convert ic info encounter error");
		return ret;
	}

	print_ic_info(ic_info);

	/* check some key info */
	if (!ic_info->misc.cmd_addr || !ic_info->misc.fw_buffer_addr ||
		!ic_info->misc.touch_data_addr) {
		ts_err("cmd_addr fw_buf_addr and touch_data_addr is null");
		return -EINVAL;
	}

	return 0;
}

#define GOODIX_ESD_TICK_WRITE_DATA	0xAA
static int brl_esd_check(struct goodix_ts_core *cd)
{
	int ret;
	u32 esd_addr;
	u8 esd_value;

	if (!cd->ic_info.misc.esd_addr)
		return 0;

	esd_addr = cd->ic_info.misc.esd_addr;
	ret = cd->hw_ops->read(cd, esd_addr, &esd_value, 1);
	if (ret) {
		ts_err("failed get esd value, %d", ret);
		return ret;
	}

	if (esd_value == GOODIX_ESD_TICK_WRITE_DATA) {
		ts_err("esd check failed, 0x%x", esd_value);
		return -EINVAL;
	}
	esd_value = GOODIX_ESD_TICK_WRITE_DATA;
	ret = cd->hw_ops->write(cd, esd_addr, &esd_value, 1);
	if (ret) {
		ts_err("failed refrash esd value");
		return ret;
	}
	return 0;
}

#define IRQ_EVENT_HEAD_LEN			8
#define IQR_FRAME_HEAD_LEN			16
#define BYTES_PER_POINT				8
#define COOR_DATA_CHECKSUM_SIZE		2
#define GOODIX_TOUCH_EVENT			0x80
#define GOODIX_FRAME_EVENT			0x80
#ifdef TOUCH_FOD_SUPPORT
#define GOODIX_POWERON_FOD_EVENT	0x88
#endif
#define GOODIX_REQUEST_EVENT		0x40
#define GOODIX_GESTURE_EVENT		0x20
#define POINT_TYPE_STYLUS_HOVER		0x01
#define POINT_TYPE_STYLUS			0x03
#define GOODIX_LRAGETOUCH_EVENT		0x10

#ifdef TOUCH_FOD_SUPPORT
static u8 eve_type;
#endif

static void goodix_parse_finger(struct goodix_touch_data *touch_data,
				u8 *buf, int touch_num)
{
	unsigned int id = 0, x = 0, y = 0, w = 0;
	u8 *coor_data;
	int i;
	static u32 pre_finger_map;
	u32 cur_finger_map = 0;

	coor_data = &buf[IRQ_EVENT_HEAD_LEN];

#ifdef TOUCH_FOD_SUPPORT
	if (eve_type == 0x88 || (eve_type == 0x80 && goodix_core_data->fod_finger)) {
		touch_data->overlay = coor_data[touch_num * 8 + 2];
		if (coor_data[1] != 0)
			touch_data->fod_id = (coor_data[0] >> 4) & 0x0F;
	}
#endif

	for (i = 0; i < touch_num; i++) {
		id = (coor_data[0] >> 4) & 0x0F;
		touch_data->t_id = id;
		if (id >= GOODIX_MAX_TOUCH) {
			ts_err("invalid finger id =%d", id);
			touch_data->touch_num = 0;
			return;
		}
		x = le16_to_cpup((__le16 *)(coor_data + 2));
		y = le16_to_cpup((__le16 *)(coor_data + 4));
		w = le16_to_cpup((__le16 *)(coor_data + 6));
		touch_data->coords[id].status = TS_TOUCH;
		touch_data->coords[id].x = x;
		touch_data->coords[id].y = y;
		touch_data->coords[id].w = w;
		cur_finger_map |= (1 << id);
		coor_data += BYTES_PER_POINT;
	}

	/* process finger release */
	for (i = 0; i < GOODIX_MAX_TOUCH; i++) {
		if (cur_finger_map & (1 << i))
			continue;
		if (pre_finger_map & (1 << i))
			touch_data->coords[i].status = TS_RELEASE;
	}

	pre_finger_map = cur_finger_map;
	touch_data->touch_num = touch_num;
}





static int goodix_touch_handler(struct goodix_ts_core *cd,
		struct goodix_ts_event *ts_event,
		u8 *pre_buf, u32 pre_buf_len)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_touch_data *touch_data = &ts_event->touch_data;
	static u8 buffer[IRQ_EVENT_HEAD_LEN +
			BYTES_PER_POINT * GOODIX_MAX_TOUCH + 2 + 8];
	u8 touch_num = 0;
	int ret = 0;
	u8 point_type = 0;
	static u8 pre_finger_num;

	/* clean event buffer */
	memset(ts_event, 0, sizeof(*ts_event));
	/* copy pre-data to buffer */
	memcpy(buffer, pre_buf, pre_buf_len);

	touch_num = buffer[2] & 0x0F;

	if (touch_num > GOODIX_MAX_TOUCH) {
		ts_err("invalid touch num %d", touch_num);
		return -EINVAL;
	}
	if (unlikely(touch_num > 2)) {
		ret = hw_ops->read(cd,misc->touch_data_addr +
			pre_buf_len,&buffer[pre_buf_len],
			(touch_num - 2) * BYTES_PER_POINT + 2 + 8);
		if (ret) {
			ts_debug("failed get touch data");
			return ret;
		}
	}

	if (touch_num > 0) {
		point_type = buffer[IRQ_EVENT_HEAD_LEN] & 0x0F;
		if (point_type == POINT_TYPE_STYLUS ||
				point_type == POINT_TYPE_STYLUS_HOVER) {
			ret = checksum_cmp(&buffer[IRQ_EVENT_HEAD_LEN],
					BYTES_PER_POINT * 2 + 2, CHECKSUM_MODE_U8_LE);
			if (ret) {
				ts_debug("touch data checksum error");
				ts_debug("data:%*ph", BYTES_PER_POINT * 2 + 2,
						&buffer[IRQ_EVENT_HEAD_LEN]);
				return -EINVAL;
			}
		} else {
			ret = checksum_cmp(&buffer[IRQ_EVENT_HEAD_LEN],
					touch_num * BYTES_PER_POINT + 2, CHECKSUM_MODE_U8_LE);
			if (ret) {
				ts_debug("touch data checksum error");
				ts_debug("data:%*ph", touch_num * BYTES_PER_POINT + 2,
						&buffer[IRQ_EVENT_HEAD_LEN]);
				return -EINVAL;
			}
		}
	}
	ts_event->event_type = EVENT_TOUCH;
	goodix_parse_finger(touch_data, buffer, touch_num);
	pre_finger_num = touch_num;

	return 0;
}

static int brl_event_handler(struct goodix_ts_core *cd,
			struct goodix_ts_event *ts_event)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	int pre_read_len;
	u8 pre_buf[32];
	u8 frame_head[16];
	u8 event_status;
	u8 large_touch_status;
	int ret;
	int frame_read_len;

#ifdef TOUCH_THP_SUPPORT
	static u64 frame_cnt = 0;
	struct timespec64 ts;
	struct rtc_time tm;
	u8 *frame_ptr;

	if (cd->enable_touch_raw) {
		struct tp_frame *tp_frame = (struct tp_frame *)get_raw_data_base(TOUCH_ID);
		if (tp_frame == NULL)
			return -EINVAL;

#ifdef GOODIX_DEBUG_SPI
		TOUCH_TRACE_FRAME_CNT_BEGIN(frame_cnt, frame_cnt - 1);
#endif
		/*Add read variable length scheme */
		ret = hw_ops->read(cd, misc->frame_data_addr,
				frame_head, GOODIX_THP_FRAME_HEAD_SIZE);
		if (ret) {
			ts_info("failed get frame head data");
			return -EINVAL;
		}
		if (checksum_cmp(frame_head, GOODIX_THP_FRAME_HEAD_SIZE, CHECKSUM_MODE_U8_LE)) {
			ts_info("frame head checksum err");
			ts_info("frame_head %*ph", GOODIX_THP_FRAME_HEAD_SIZE, frame_head);
			return -EINVAL;
		}
		/*ts_info("frame_head %*ph", GOODIX_THP_FRAME_HEAD_SIZE, frame_head);*/
		frame_read_len = (frame_head[4] << 8) | (frame_head[3]);
		if (frame_read_len > GOODIX_THP_FRAME_SIZE) {
			ts_err("frame read len err %d", frame_read_len);
			frame_read_len = GOODIX_THP_FRAME_SIZE;
		}

		ret = hw_ops->read(cd, misc->frame_data_addr,
				tp_frame->thp_frame, frame_read_len);
		if (ret) {
			ts_err("failed get frame data");
#ifdef GOODIX_DEBUG_SPI
			TOUCH_TRACE_FRAME_CNT_END();
#endif
			return -EINVAL;
		}
		frame_ptr = (u8 *)&tp_frame->thp_frame;
		event_status = frame_ptr[0];
		// ts_info("frame_head %*ph", IQR_FRAME_HEAD_LEN, frame_ptr);

		if (event_status & GOODIX_FRAME_EVENT) {
			if (cd->sync_mode == SYNC)
				hw_ops->after_event_handler(cd);
			ktime_get_real_ts64(&ts);
			tp_frame->time_ns = timespec64_to_ns(&ts);
			ts_event->event_type = EVENT_FRAME;
			tp_frame->frame_cnt = frame_cnt;
			tp_frame->fod_pressed = cd->fod_finger;
#ifdef TOUCH_DUMP_TIC_SUPPORT
			goodix_htc_dump_tic(cd, tp_frame);
#endif //TOUCH_DUMP_TIC_SUPPORT
			notify_raw_data_update(TOUCH_ID);
			rtc_time64_to_tm(ts.tv_sec, &tm);
			frame_cnt++;
#ifdef GOODIX_DEBUG_SPI
			TOUCH_TRACE_FRAME_CNT_END();
#endif
			ts_debug("one touch frame data");
			return 0;
		} else {
			ts_err("invalid event type: %d", event_status);
#ifdef GOODIX_DEBUG_SPI
			TOUCH_TRACE_FRAME_CNT_END();
#endif
			return -EINVAL;
		}
	}
#endif

	pre_read_len = IRQ_EVENT_HEAD_LEN +
		BYTES_PER_POINT * 2 + COOR_DATA_CHECKSUM_SIZE;
	ret = hw_ops->read(cd, misc->touch_data_addr,
			pre_buf, pre_read_len);
	if (ret) {
		ts_err("failed get event head data");
		return ret;
	}

	if (checksum_cmp(pre_buf, IRQ_EVENT_HEAD_LEN, CHECKSUM_MODE_U8_LE)) {
		ts_err("touch head checksum err");
		ts_err("touch_head %*ph", IRQ_EVENT_HEAD_LEN, pre_buf);
		ts_event->retry = 1;
		return -EINVAL;
	}

	large_touch_status = pre_buf[2];
	event_status = pre_buf[0];
	ts_debug("touch_head %*ph", IRQ_EVENT_HEAD_LEN, pre_buf);

#ifdef TOUCH_FOD_SUPPORT
	if (event_status & GOODIX_POWERON_FOD_EVENT) {
		cd->eventsdata = event_status;
		eve_type = event_status;
	}
#endif
	if (event_status == 0) {
		ts_event->event_type = EVENT_INVALID;
		ts_debug("Invalid event status");
		return -EINVAL;
	}

	if (event_status & GOODIX_TOUCH_EVENT)
		goodix_touch_handler(cd, ts_event, pre_buf, pre_read_len);

	if (event_status & GOODIX_REQUEST_EVENT) {
		ts_event->event_type = EVENT_REQUEST;
		if (pre_buf[2] == BRL_REQUEST_CODE_CONFIG)
			ts_event->request_code = REQUEST_TYPE_CONFIG;
		else if (pre_buf[2] == BRL_REQUEST_CODE_RESET)
			ts_event->request_code = REQUEST_TYPE_RESET;
		else
			ts_debug("unsupported request code 0x%x", pre_buf[2]);
	}
	if (event_status & GOODIX_GESTURE_EVENT) {
		ts_event->event_type = EVENT_GESTURE;
		ts_event->gesture_type = pre_buf[4];
	}

	hw_ops->after_event_handler(cd);

	return 0;
}

static int brl_after_event_handler(struct goodix_ts_core *cd)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	u8 sync_clean = 0;
#ifdef TOUCH_THP_SUPPORT
	if (cd->enable_touch_raw && atomic_read(&cd->suspended) == 0)
		return hw_ops->write(cd, misc->frame_data_addr, &sync_clean, 1);
	else
#endif
		return hw_ops->write(cd, misc->touch_data_addr, &sync_clean, 1);
}

static int brld_get_framedata(struct goodix_ts_core *cd,
		struct ts_rawdata_info *info)
{
	int ret;
	u8 val;
	int retry = 20;
	struct frame_head *frame_head;
	unsigned char *frame_buf;
	unsigned char *cur_ptr;
	u32 flag_addr = cd->ic_info.misc.frame_data_addr;
	int tx = cd->ic_info.parm.drv_num;
	int rx = cd->ic_info.parm.sen_num;

	frame_buf = kzalloc(FRAME_DATA_MAX_LEN, GFP_KERNEL);
	if (!frame_buf) {
		ts_err("malloc frame_buf failed");
		return -ENOMEM;
	}

	/* clean touch event flag */
	val = 0;
	ret = brl_write(cd, flag_addr, &val, 1);
	if (ret < 0) {
		ts_err("clean touch event failed, exit!");
		goto exit;
	}

	while (retry--) {
		usleep_range(2000, 2100);
		ret = brl_read(cd, flag_addr, &val, 1);
		if (!ret && (val & GOODIX_TOUCH_EVENT))
			break;
	}
	if (retry < 0) {
		ts_err("framedata is not ready val:0x%02x, exit!", val);
		ret = -EINVAL;
		goto exit;
	}

	ret = brl_read(cd, flag_addr, frame_buf, FRAME_DATA_MAX_LEN);
	if (ret < 0) {
		ts_err("read frame data failed");
		goto exit;
	}

	if (checksum_cmp(frame_buf, cd->ic_info.misc.frame_data_head_len, CHECKSUM_MODE_U8_LE)) {
		ts_err("frame head checksum error");
		ret = -EINVAL;
		goto exit;
	}

	frame_head = (struct frame_head *)frame_buf;
	if (checksum_cmp(frame_buf, frame_head->cur_frame_len, CHECKSUM_MODE_U16_LE)) {
		ts_err("frame body checksum error");
		ret = -EINVAL;
		goto exit;
	}
	cur_ptr = frame_buf;
	cur_ptr += cd->ic_info.misc.frame_data_head_len;
	cur_ptr += cd->ic_info.misc.fw_attr_len;
	cur_ptr += cd->ic_info.misc.fw_log_len;
	memcpy((u8 *)(info->buff + info->used_size), cur_ptr + 8,
			tx * rx * 2);

exit:
	kfree(frame_buf);
	return ret;
}

static int brld_get_cap_data(struct goodix_ts_core *cd,
		struct ts_rawdata_info *info)
{
	struct goodix_ts_cmd temp_cmd;
	int tx = cd->ic_info.parm.drv_num;
	int rx = cd->ic_info.parm.sen_num;
	int size = tx * rx;
	int ret;

	/* disable irq & close esd */
	brl_irq_enbale(cd, false);
	goodix_ts_blocking_notify(NOTIFY_ESD_OFF, NULL);

	info->buff[0] = rx;
	info->buff[1] = tx;
	info->used_size = 2;

	temp_cmd.cmd = 0x90;
	temp_cmd.data[0] = 0x81;
	temp_cmd.len = 5;
	ret = brl_send_cmd(cd, &temp_cmd);
	if (ret < 0) {
		ts_err("switch rawdata mode failed, exit!");
		goto exit;
	}

	ret = brld_get_framedata(cd, info);
	if (ret < 0) {
		ts_err("brld get rawdata failed");
		goto exit;
	}
	goodix_rotate_abcd2cbad(tx, rx, &info->buff[info->used_size]);
	info->used_size += size;

	temp_cmd.cmd = 0x90;
	temp_cmd.data[0] = 0x82;
	temp_cmd.len = 5;
	ret = brl_send_cmd(cd, &temp_cmd);
	if (ret < 0) {
		ts_err("switch diffdata mode failed, exit!");
		goto exit;
	}

	ret = brld_get_framedata(cd, info);
	if (ret < 0) {
		ts_err("brld get diffdata failed");
		goto exit;
	}
	goodix_rotate_abcd2cbad(tx, rx, &info->buff[info->used_size]);
	info->used_size += size;

	temp_cmd.cmd = 0x90;
	temp_cmd.data[0] = 0x83;
	temp_cmd.len = 5;
	ret = brl_send_cmd(cd, &temp_cmd);
	if (ret < 0) {
		ts_err("switch refdata mode failed, exit!");
		goto exit;
	}

	ret = brld_get_framedata(cd, info);
	if (ret < 0) {
		ts_err("brld get refdata failed");
		goto exit;
	}
	goodix_rotate_abcd2cbad(tx, rx, &info->buff[info->used_size]);
	info->used_size += size;

exit:
	temp_cmd.cmd = 0x90;
	temp_cmd.data[0] = 0x01;
	temp_cmd.data[1] = 0x01;
	temp_cmd.len = 6;
	brl_send_cmd(cd, &temp_cmd);
	/* enable irq & esd */
	goodix_ts_blocking_notify(NOTIFY_ESD_ON, NULL);
	brl_irq_enbale(cd, true);
	return ret;
}

#define GOODIX_CMD_RAWDATA	2
#define GOODIX_CMD_COORD	0
static int brl_get_capacitance_data(struct goodix_ts_core *cd,
		struct ts_rawdata_info *info)
{
	int ret;
	int retry = 20;
	struct goodix_ts_cmd temp_cmd;
	u32 flag_addr = cd->ic_info.misc.touch_data_addr;
	u32 raw_addr = cd->ic_info.misc.mutual_rawdata_addr;
	u32 diff_addr = cd->ic_info.misc.mutual_diffdata_addr;
	u32 ref_addr = cd->ic_info.misc.mutual_refdata_addr;
	int tx = cd->ic_info.parm.drv_num;
	int rx = cd->ic_info.parm.sen_num;
	int size = tx * rx;
	u8 val;

	if (!info) {
		ts_err("input null ptr");
		return -EIO;
	}

	if (cd->bus->ic_type == IC_TYPE_BERLIN_D)
		return brld_get_cap_data(cd, info);

	/* disable irq & close esd */
	brl_irq_enbale(cd, false);
	goodix_ts_blocking_notify(NOTIFY_ESD_OFF, NULL);

    /* switch rawdata mode */
	temp_cmd.cmd = GOODIX_CMD_RAWDATA;
	temp_cmd.len = 4;
	ret = brl_send_cmd(cd, &temp_cmd);
	if (ret < 0) {
		ts_err("switch rawdata mode failed, exit!");
		goto exit;
	}

	/* clean touch event flag */
	val = 0;
	ret = brl_write(cd, flag_addr, &val, 1);
	if (ret < 0) {
		ts_err("clean touch event failed, exit!");
		goto exit;
	}

	while (retry--) {
		usleep_range(5000, 5100);
		ret = brl_read(cd, flag_addr, &val, 1);
		if (!ret && (val & GOODIX_TOUCH_EVENT))
			break;
	}
	if (retry < 0) {
		ts_err("rawdata is not ready val:0x%02x, exit!", val);
		goto exit;
	}

	/* obtain rawdata & diff_rawdata */
	info->buff[0] = rx;
	info->buff[1] = tx;
	info->used_size = 2;

	ret = brl_read(cd, raw_addr, (u8 *)&info->buff[info->used_size],
			size * sizeof(s16));
	if (ret < 0) {
		ts_err("obtian raw_data failed, exit!");
		goto exit;
	}
	goodix_rotate_abcd2cbad(tx, rx, &info->buff[info->used_size]);
	info->used_size += size;

	ret = brl_read(cd, diff_addr, (u8 *)&info->buff[info->used_size],
			size * sizeof(s16));
	if (ret < 0) {
		ts_err("obtian diff_data failed, exit!");
		goto exit;
	}
	goodix_rotate_abcd2cbad(tx, rx, &info->buff[info->used_size]);
	info->used_size += size;

	ret = brl_read(cd, ref_addr, (u8 *)&info->buff[info->used_size],
			size * sizeof(s16));
	if (ret < 0) {
		ts_err("obtian ref_data failed, exit!");
		goto exit;
	}
	goodix_rotate_abcd2cbad(tx, rx, &info->buff[info->used_size]);
	info->used_size += size;

exit:
	/* switch coor mode */
	temp_cmd.cmd = GOODIX_CMD_COORD;
	temp_cmd.len = 4;
	brl_send_cmd(cd, &temp_cmd);
	/* clean touch event flag */
	val = 0;
	brl_write(cd, flag_addr, &val, 1);
	/* enable irq & esd */
	brl_irq_enbale(cd, true);
	goodix_ts_blocking_notify(NOTIFY_ESD_ON, NULL);
	return ret;
}

#define GOODIX_CHARGER_CMD	0xAF
static int brl_charger_on(struct goodix_ts_core *cd, bool on)
{
	struct goodix_ts_cmd cmd;

	if (cd->work_status == TP_SLEEP) {
		ts_info("Unsupported send charger cmd in sleep mode");
		return 0;
	}

#ifdef TOUCH_TRUSTED_SUPPORT
	if (cd->tui_process) {
		if (wait_for_completion_interruptible(&cd->tui_finish)) {
			ts_err("cautious, ERESTARTSYS may cause cmd loss recomand try again");
			return 0;
		}
		ts_info("wait finished, its time to go ahead");
	}
#endif // TOUCH_TRUSTED_SUPPORT

	cmd.cmd = GOODIX_CHARGER_CMD;
	cmd.len = 5;
	cmd.data[0] = (on == true) ? 1 : 0;
	/* ts_info("gesture data :%*ph", 8, cmd.buf); */
	if (cd->hw_ops->send_cmd(cd, &cmd)) {
		ts_err("failed send charger cmd, on = %d", on);
		return -EINVAL;
	} else {
		ts_info("charger mode %s", (on == true) ? "on" : "off");
	}

	return 0;
}

#define GOODIX_PALM_CMD		0x70
static int brl_palm_on(struct goodix_ts_core *cd, bool on)
{
	struct goodix_ts_cmd cmd;
#ifdef TOUCH_THP_SUPPORT
	if (goodix_core_data->enable_touch_raw)
		return 0;
#endif

#ifdef TOUCH_TRUSTED_SUPPORT
	if (cd->tui_process) {
		if (wait_for_completion_interruptible(&cd->tui_finish)) {
			ts_err("cautious, ERESTARTSYS may cause cmd loss recomand try again");
			return 0;
		}
		ts_info("wait finished, its time to go ahead");
	}
#endif // TOUCH_TRUSTED_SUPPORT

	cmd.cmd = GOODIX_PALM_CMD;
	cmd.len = 6;
	cmd.data[0] = (on == true) ? 1 : 0;
	/* ts_info("gesture data :%*ph", 8, cmd.buf); */
	if (cd->hw_ops->send_cmd(cd, &cmd)) {
		ts_err("failed send palm cmd, on = %d", on);
		return -EINVAL;
	} else {
		ts_info("palm mode %s", (on == true) ? "on" : "off");
	}

	return 0;
}

#ifdef GOODIX_XIAOMI_TOUCHFEATURE
#define GOODIX_GAME_CMD      0x17
#define GOODIX_NORMAL_CMD    0x18
static int brl_game(struct goodix_ts_core *cd, u8 data0, u8 data1, bool on)
{
	struct goodix_ts_cmd cmd;

	if (on)
		cmd.cmd = GOODIX_GAME_CMD;
	else
		cmd.cmd = GOODIX_NORMAL_CMD;
	cmd.len = 6;
	cmd.data[0] = data0;
	cmd.data[1] = data1;
	if (cd->hw_ops->send_cmd(cd, &cmd)) {
		ts_err("failed send game cmd, data0 = 0x%x, data1 = 0x%x, on = %d", data0, data1, on);
		return -EINVAL;
	} else {
		ts_info("game data0:0x%x, data1:0x%x, game mode %s", data0, data1, (on == true) ? "on" : "off");
	}

	return 0;
}
#endif


#ifdef TOUCH_THP_SUPPORT
/*
 * set game mode on/off.
 *
 * index: the scan rate index in array ICinfo->param->active_scan_rate[]
 *   If the default interrupt frequency is 240Hz,
 *     index 0x00/0x01 corresponds to off/on.
 *
 * return: 0 on success, otherwise return < 0.
 */
#define GOODIX_CMD_GAME_MODE 0xC2
int goodix_htc_set_game_mode(u8 index)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd tmp_cmd;
	int ret;

	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	tmp_cmd.cmd = GOODIX_CMD_GAME_MODE;
	tmp_cmd.data[0] = index;
	tmp_cmd.len = 5;
	ret = hw_ops->send_cmd(goodix_core_data, &tmp_cmd);

	return ret;
}

#define GOODIX_CMD_FRAMEDATA 0x90
/* enable thp frame data report */
static int goodix_htc_enable_touch_raw(int en)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd raw_cmd;
	int ret;

	ts_info("enter");
	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	raw_cmd.cmd = GOODIX_CMD_FRAMEDATA;
	if (en) {
		if (goodix_core_data->sync_mode == NO_SYNC)
			raw_cmd.data[0] = 0x01;
		else if (goodix_core_data->sync_mode == AUTO_SYNC)
			raw_cmd.data[0] = 0x11;
		else if (goodix_core_data->sync_mode == SYNC)
			raw_cmd.data[0] = 0x81;
		else if (goodix_core_data->sync_mode == DIFF_AUTO_SYNC)
			raw_cmd.data[0] = 0x18;
	}
	else
		raw_cmd.data[0] = 0;

	raw_cmd.len = 5;
	ret = hw_ops->send_cmd(goodix_core_data, &raw_cmd);
	if (ret)
		ts_err("failed send rawdata cmd %d, ret %d", en, ret);
	else {
		ts_info("success send rawdata cmd %d", en);
		goodix_core_data->enable_touch_raw = en;
	}

	return ret;
}

#define GOODIX_CMD_TOUCHDATA 0x91
static int goodix_htc_enable_touch_coord(int en)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd touch_cmd;
	int ret;

	ts_info("enter");
	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	touch_cmd.cmd = GOODIX_CMD_TOUCHDATA;
	if (en) {
		if (goodix_core_data->sync_mode)
			touch_cmd.data[0] = 0x81;
		else
			touch_cmd.data[0] = 0x01;
	}
	else
		touch_cmd.data[0] = 0;

	touch_cmd.len = 5;
	ret = hw_ops->send_cmd(goodix_core_data, &touch_cmd);
	if (ret)
		ts_err("failed send touch data cmd %d, ret %d", en, ret);
	else {
		ts_info("success send touch data cmd %d", en);
	}

	return ret;
}

int goodix_htc_enable(int en)
{
	int ret = 0;

	if (!goodix_core_data)
		return -EINVAL;
	ret = goodix_htc_enable_touch_raw(en);
	if (ret) {
		ts_err("enable touch raw failed, en:%d\n", en);
		return ret;
	}
	ret = goodix_htc_enable_touch_coord(!en);
	if (ret) {
		ts_err("enable touch coord failed, en:%d\n", en);
		return ret;
	}
	goodix_core_data->enable_touch_raw = en;
	return ret;
}

int goodix_htc_enable_b_array(void)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd raw_cmd;
	int ret;

	if (!goodix_core_data)
		return -EINVAL;

	hw_ops = goodix_core_data->hw_ops;
	raw_cmd.cmd = GOODIX_CMD_FRAMEDATA;
	if (goodix_core_data->sync_mode == NO_SYNC)
		raw_cmd.data[0] = 0x07;
	else if (goodix_core_data->sync_mode == AUTO_SYNC)
		raw_cmd.data[0] = 0x17;
	else if (goodix_core_data->sync_mode == SYNC)
		raw_cmd.data[0] = 0x87;
	raw_cmd.len = 5;
	ret = hw_ops->send_cmd(goodix_core_data, &raw_cmd);
	if (ret)
		ts_err("failed send b array cmd %d", ret);
	else
		ts_info("success send b array cmd");

	return ret;
}
#endif

#ifdef TOUCH_DUMP_TIC_SUPPORT
int goodix_htc_dump_tic(struct goodix_ts_core *cd,
		struct tp_frame *tp_frame)
{
	int ret;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	tp_frame->dump_type = cd->dump_type;
	ts_debug("enable_touch_raw:%d, dump_type:%d",
			cd->enable_touch_raw, cd->dump_type);
	memset(tp_frame->tic_raw, 0, GOODIX_TIC_RAW_SIZE);
	memset(tp_frame->tic_base, 0, GOODIX_TIC_RAW_SIZE);

	if (!cd->enable_touch_raw || !cd->dump_type) {
		ts_debug("not support");
		return 0;
	}

	ret = hw_ops->read(cd, TIC_RAW_DATA_ADDR,
			tp_frame->tic_raw, GOODIX_TIC_RAW_SIZE);
	if (ret) {
		ts_err("failed get tic raw data");
		return -EINVAL;
	}

	/* just read base address for the first frame of reporting point DOWN */
	if (cd->dump_type == DUMP_BASE) {
		ret = hw_ops->read(cd, TIC_BASE_DATA_ADDR,
				tp_frame->tic_base, GOODIX_TIC_RAW_SIZE);
		if (ret) {
			ts_err("failed get tic base data");
			return -EINVAL;
		}
		cd->dump_type = DUMP_ON;
	}

	return 0;
}
#endif //TOUCH_DUMP_TIC_SUPPORT

#define GOODIX_CMD_EMPTY_INT 0x11
int goodix_htc_enable_empty_int(bool en)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd touch_cmd;
	int ret;

	ts_info("enter");
	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	touch_cmd.cmd = GOODIX_CMD_EMPTY_INT;
	touch_cmd.data[0] = en ? 1 : 0;
	touch_cmd.len = 5;
	ret = hw_ops->send_cmd(goodix_core_data, &touch_cmd);
	if (ret)
		ts_err("failed send empty int cmd %d, ret %d", en, ret);
	else
		ts_info("success send empty int cmd %d", en);

	return ret;
}

static struct goodix_ic_info_param *get_goodix_ic_info_param(void)
{
	struct goodix_ic_info *ic_info;
	if (!goodix_core_data)
		return NULL;

	ic_info = &goodix_core_data->ic_info;

	if (!ic_info)
		return NULL;

	return &ic_info->parm;
}

int goodix_get_tx_num(void)
{
	struct goodix_ic_info_param *parm = get_goodix_ic_info_param();
	if (!parm)
		return 0;

	return parm->sen_num;
}

int goodix_get_rx_num(void)
{
	struct goodix_ic_info_param *parm = get_goodix_ic_info_param();
	if (!parm)
		return 0;

	return parm->drv_num;
}

int goodix_get_freq_num(void)
{
	struct goodix_ic_info_param *parm = get_goodix_ic_info_param();
	if (!parm)
		return 0;

	return parm->mutual_freq_num;
}

/*
 * set afe enter or exit idle state.
 *
 * @en : set true to enter idle,
 *	false to exit idle.
 *
 * return: 0 on success, otherwise return < 0.
 */
#define GOODIX_CMD_IDLE 0x9F
int goodix_htc_enter_idle(int *value)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd idle_cmd;
	int ret = -1;
	int en = value[0];
	int cycle = value[1];
	int time = value[2];
	ts_debug("idle data0: %d, data1: %d, data2: %d", en, cycle, time);

	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	idle_cmd.cmd = GOODIX_CMD_IDLE;
	if (en)
		idle_cmd.data[0] = 1; // enter idle
	else
		idle_cmd.data[0] = 0; // exit idle

	if (cycle) {
		idle_cmd.data[0] = 5;
		idle_cmd.data[1] = cycle & 0xff;
		idle_cmd.data[2] = (cycle >> 8) & 0xff;
		idle_cmd.data[3] = time & 0xff;
		idle_cmd.data[4] = (time >> 8) & 0xff;
		idle_cmd.len = 9;
	} else {
		idle_cmd.len = 5;
	}

	if (hw_ops->send_cmd)
		ret = hw_ops->send_cmd(goodix_core_data, &idle_cmd);
	ts_info("%s send idle cmd %d, ret %d",
		ret ? "failed" : "success", idle_cmd.data[0], ret);

	return ret;
}

/*
 * update idle base line.
 *
 * return: 0 on success, otherwise return < 0.
 */
#define GOODIX_CMD_UPDATE_IDLE_BASELINE 0xA2
int goodix_htc_update_idle_baseline(void)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd tmp_cmd;
	int ret = -1;

	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	tmp_cmd.cmd = GOODIX_CMD_UPDATE_IDLE_BASELINE;
	tmp_cmd.len = 4;
	if (hw_ops->send_cmd)
		ret = hw_ops->send_cmd(goodix_core_data, &tmp_cmd);
	ts_debug("%s send update idle baseline cmd, ret %d",
		ret ? "failed" : "success", ret);

	return ret;
}

/*
 * set active mode scan/report rate.
 *
 * index: the scan rate index in array ICinfo->param->active_scan_rate[]
 *   If the default interrupt frequency is 240Hz,
 *     index 0x00/0x01/0x02/0x03 corresponds to 240Hz/120Hz/90Hz/60Hz.
 *
 * return: 0 on success, otherwise return < 0.
 *
 * Notes (N11U):
 *   Default (Non-game state and non-tui state): 240Hz
 *   TUI: 120Hz (Manual switching command: adb shell "echo w:10174:7:00:00:05:9d:01:a3:00 > /sys/devices/platform/goodix_ts.0/reg_rw")
 */
#define GOODIX_CMD_ACTIVE_SCAN_RATE 0x9D
int goodix_htc_set_active_scan_rate(u8 index)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd tmp_cmd;
	int ret;

	if (!goodix_core_data)
		return -EINVAL;
	switch (index) {
	case 60:
		tmp_cmd.data[0] = 3;
		break;
	case 68:
		tmp_cmd.data[0] = 0;
		break;
	case 120:
		tmp_cmd.data[0] = 1;
		break;
	case 135:
		tmp_cmd.data[0] = 2;
		break;
	default:
		tmp_cmd.data[0] = 1;
		break;
	}
	hw_ops = goodix_core_data->hw_ops;
	tmp_cmd.cmd = GOODIX_CMD_ACTIVE_SCAN_RATE;
	tmp_cmd.len = 5;
	ret = hw_ops->send_cmd(goodix_core_data, &tmp_cmd);
	ts_info("%s send scan rate index %d, ret %d",
		ret ? "failed" : "success", index, ret);

	return ret;
}

/*
 * set scan freq
 *
 * index: the scan freq corresponding index in array ICinfo->param->mutual_freq[]
 *
 * return: 0 on success, otherwise return < 0.
 */
#define GOODIX_CMD_SCAN_FREQ 0x9C
int goodix_htc_set_scan_freq(u8 index)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd tmp_cmd;
	int ret = -1;

	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	tmp_cmd.cmd = GOODIX_CMD_SCAN_FREQ;
	tmp_cmd.data[0] = index;
	tmp_cmd.len = 5;
	if (hw_ops->send_cmd)
		ret = hw_ops->send_cmd(goodix_core_data, &tmp_cmd);
	ts_info("%s send scan freq index %d, ret %d",
		ret ? "failed" : "success", index, ret);

	return ret;
}

/*
 * set idle wakeup threshold
 *
 * threshold: threshold value
 *
 * return: 0 on success, otherwise return < 0.
 */
#define GOODIX_CMD_IDLE_THRESHOLD 0xA1
int goodix_htc_set_idle_threshold(int threshold)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd tmp_cmd;
	int ret;

	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	tmp_cmd.cmd = GOODIX_CMD_IDLE_THRESHOLD;

	//00: no reduce
	//01: reduce 10% ... 09: reduce 90%
	tmp_cmd.data[0] = (u8)(threshold & 0xFF);
	//tmp_cmd.data[1] = (u8)((threshold >> 8) & 0xFF);
	tmp_cmd.len = 5;
	ret = hw_ops->send_cmd(goodix_core_data, &tmp_cmd);
	ts_debug("%s send idle threshold cmd %d, ret %d",
		ret ? "failed" : "success", threshold, ret);

	return ret;
}

/*
 * set ic freq hopping value 
 *
 *
 * return: 0 on success, otherwise return < 0.
 */
#define GOODIX_CMD_FREQ_HOPPING 0x9C
int goodix_htc_set_freq_hopping(int index)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd tmp_cmd;
	int ret = -1;

	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	tmp_cmd.cmd = GOODIX_CMD_FREQ_HOPPING;
	tmp_cmd.data[0] = index & 0xff;
	tmp_cmd.len = 5;
	if (hw_ops->send_cmd)
		ret = hw_ops->send_cmd(goodix_core_data, &tmp_cmd);
	ts_info("%s send freq hopping index %x, ret %d",
		ret ? "failed" : "success", index, ret);

	return ret;
}

/*
 * set ic soft reset
 *
 *
 * return: 0 on success, otherwise return < 0.
 */
#define GOODIX_CMD_SOFT_RESET 0xBF
int goodix_htc_set_soft_reset(int index)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd tmp_cmd;
	int ret = -1;

	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	tmp_cmd.cmd = GOODIX_CMD_SOFT_RESET;
	tmp_cmd.data[0] = index & 0xff;
	tmp_cmd.len = 5;
	if (hw_ops->send_cmd)
		ret = hw_ops->send_cmd(goodix_core_data, &tmp_cmd);
	ts_info("%s send soft reset index %x, ret %d",
		ret ? "failed" : "success", index, ret);

	return ret;
}

/*
 * set ic double scan
 *
 *
 * return: 0 on success, otherwise return < 0.
 */
#define GOODIX_CMD_DOUBLE_SCAN 0x86
int goodix_htc_set_double_scan(int index)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd tmp_cmd;
	int ret = -1;

	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	tmp_cmd.cmd = GOODIX_CMD_DOUBLE_SCAN;
	tmp_cmd.data[0] = index & 0xff;
	tmp_cmd.len = 5;
	if (hw_ops->send_cmd)
		ret = hw_ops->send_cmd(goodix_core_data, &tmp_cmd);
	ts_info("%s send double scan index %x, ret %d",
		ret ? "failed" : "success", index, ret);

	return ret;
}

/*
 * set ic b matrix study
 *
 * value: xx,yy
 *
 * return: 0 on success, otherwise return < 0.
 */
#define GOODIX_CMD_NORMALIZE_STUDY 0xEE
int goodix_htc_set_normalize_study(int *index)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd tmp_cmd;
	int ret = -1;

	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	tmp_cmd.cmd = GOODIX_CMD_NORMALIZE_STUDY;
	tmp_cmd.data[0] = index[0] & 0xff;
	tmp_cmd.data[1] = index[1] & 0xff;
	tmp_cmd.len = 6;
	if (hw_ops->send_cmd)
		ret = hw_ops->send_cmd(goodix_core_data, &tmp_cmd);
	ts_info("%s send normalize study index %x %x, ret %d",
		ret ? "failed" : "success", index[0], index[1], ret);

	return ret;
}

/*
 * set ic gesture_baseline_feedback
 *
 *
 * return: 0 on success, otherwise return < 0.
 */
#define GOODIX_CMD_GESTRUE_FEEDBACK 0xD1
int goodix_htc_set_gesture_feedback(int  index)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd tmp_cmd;
	int ret = -1;

	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	tmp_cmd.cmd = GOODIX_CMD_GESTRUE_FEEDBACK;
	tmp_cmd.data[0] = index & 0xff;
	tmp_cmd.len = 5;
	if (hw_ops->send_cmd)
		ret = hw_ops->send_cmd(goodix_core_data, &tmp_cmd);
	ts_info("%s send gesture feedback index %d, ret %d",
		ret ? "failed" : "success", index, ret);

	return ret;
}

/*
 * enable or disable freq shift
 *
 * en: set true to enable freq shift,
 *     false to disable freq shift.
 *
 * return: 0 on success, otherwise return < 0.
 */
#define GOODIX_CMD_FREQ_SHIFT 0x9B
int goodix_htc_enable_freq_shift(bool en)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd tmp_cmd;
	int ret;

	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	tmp_cmd.cmd = GOODIX_CMD_FREQ_SHIFT;
	tmp_cmd.data[0] = en ? 1 : 0;

	tmp_cmd.len = 5;
	ret = hw_ops->send_cmd(goodix_core_data, &tmp_cmd);
	ts_debug("%s send freq shift cmd %d, ret %d",
		ret ? "failed" : "success", en, ret);

	return ret;
}

/*
 * start do touch panel clibration
 *
 * return: 0 on success, otherwise return < 0.
 */
#define GOODIX_CMD_START_FREQ_SCAN 0xA8
int goodix_htc_start_calibration(void)
{
	struct goodix_ts_hw_ops *hw_ops;
	struct goodix_ts_cmd tmp_cmd;
	int ret = -1;

	if (!goodix_core_data)
		return -EINVAL;
	hw_ops = goodix_core_data->hw_ops;
	tmp_cmd.cmd = GOODIX_CMD_START_FREQ_SCAN;
	tmp_cmd.data[0] = 1;

	tmp_cmd.len = 4;
	if (hw_ops->send_cmd)
		ret = hw_ops->send_cmd(goodix_core_data, &tmp_cmd);
	ts_info("%s send freq scan/calibration cmd, ret %d",
		ret ? "failed" : "success", ret);
	return ret;
}


static int brl_get_frame_data(struct goodix_ts_core *cd, struct ts_framedata *info)
{
	struct goodix_ts_cmd temp_cmd;
	u32 flag_addr = cd->ic_info.misc.frame_data_addr;
	struct frame_head *frame_head;
	int retry = 20;
	int ret;
	u8 val;

	if (!info) {
		ts_err("input null ptr");
		return -EIO;
	}

	/* disable irq & close esd */
	brl_irq_enbale(cd, false);
	goodix_ts_blocking_notify(NOTIFY_ESD_OFF, NULL);

	temp_cmd.cmd = 0x90;
	temp_cmd.data[0] = 0x81;
	temp_cmd.len = 5;
	ret = brl_send_cmd(cd, &temp_cmd);
	if (ret < 0) {
		ts_err("switch framedata mode failed, exit!");
		goto exit;
	}

	/* clean touch event flag */
	val = 0;
	ret = brl_write(cd, flag_addr, &val, 1);
	if (ret < 0) {
		ts_err("clean framedata sync flag failed, exit!");
		goto exit;
	}

	while (retry--) {
		usleep_range(2000, 2100);
		ret = brl_read(cd, flag_addr, &val, 1);
		if (!ret && (val & GOODIX_TOUCH_EVENT))
			break;
	}
	if (retry < 0) {
		ts_err("framedata is not ready val:0x%02x, exit!", val);
		ret = -EINVAL;
		goto exit;
	}

	ret = brl_read(cd, flag_addr, info->buff, FRAME_DATA_MAX_LEN);
	if (ret < 0) {
		ts_err("reaf frame data failed");
		goto exit;
	}

	if (checksum_cmp(info->buff, cd->ic_info.misc.frame_data_head_len, CHECKSUM_MODE_U8_LE)) {
		ts_err("frame head checksum error");
		ret = -EINVAL;
		goto exit;
	}

	frame_head = (struct frame_head *)info->buff;
	if (checksum_cmp(info->buff, frame_head->cur_frame_len, CHECKSUM_MODE_U16_LE)) {
		ts_err("frame body checksum error");
		ret = -EINVAL;
		goto exit;
	}
	info->used_size = frame_head->cur_frame_len;

exit:
	/* switch coor mode */
	temp_cmd.cmd = 0x90;
	temp_cmd.data[0] = 0;
	temp_cmd.len = 5;
	brl_send_cmd(cd, &temp_cmd);
	/* clean touch event flag */
	val = 0;
	brl_write(cd, flag_addr, &val, 1);
	/* enable irq & esd */
	brl_irq_enbale(cd, true);
	goodix_ts_blocking_notify(NOTIFY_ESD_ON, NULL);
	return ret;
}

#define GOODIX_HIGH_RATE_CMD		0xC1
int brl_switch_report_rate(struct goodix_ts_core *cd, bool on)
{
	struct goodix_ts_cmd cmd;
	static bool last_status;
	if (last_status == on)
		return 0;

	cmd.cmd = GOODIX_HIGH_RATE_CMD;
	cmd.len = 5;
	cmd.data[0] = (on == true) ? 1 : 0;
	if (cd->hw_ops->send_cmd(cd, &cmd)) {
		ts_err("failed send report rate cmd, on = %d", on);
		return -EINVAL;
	} else {
		ts_info("reprot rate switch: %s", (on == true) ? "480HZ" : "240HZ");
	}
	last_status = on;
	return 0;
}



static struct goodix_ts_hw_ops brl_hw_ops = {
	.power_on = brl_power_on,
	.dev_confirm = brl_dev_confirm,
	.resume = brl_resume,
	.suspend = brl_suspend,
	.gesture = brl_gesture,
	.reset = brl_reset,
	.irq_enable = brl_irq_enbale,
	.read = brl_read,
	.write = brl_write,
	.read_flash = brl_flash_read,
	.write_flash = brl_flash_write,
	.send_cmd = brl_send_cmd,
	.send_config = brl_send_config,
	.read_config = brl_read_config,
	.read_version = brl_read_version,
	.get_ic_info = brl_get_ic_info,
	.esd_check = brl_esd_check,
	.event_handler = brl_event_handler,
	.after_event_handler = brl_after_event_handler,
	.get_capacitance_data = brl_get_capacitance_data,
	.charger_on = brl_charger_on,
	.palm_on = brl_palm_on,
#ifdef GOODIX_XIAOMI_TOUCHFEATURE
	.game = brl_game,
#endif
	.get_frame_data = brl_get_frame_data,
	.switch_report_rate = brl_switch_report_rate,
};

struct goodix_ts_hw_ops *goodix_get_hw_ops(void)
{
	return &brl_hw_ops;
}
