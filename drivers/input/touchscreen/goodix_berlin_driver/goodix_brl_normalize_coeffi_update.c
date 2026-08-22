#include <linux/slab.h>
#include <linux/string.h>
#include "goodix_ts_core.h"
#include "goodix_brl_normalize_coeffi.h"


#define NORMALIZE_K_FILE_NAME		"goodix_normalize.csv"

static u16 g_cfg_checksum;
static struct normalize_k_param normalize_k_array[MAX_SCAN_FREQ_NUM];

/*
 * Description: tp_flash_write, write 4K once at most
 * @addr: flash addr
 * @data_len: data len
 * @buf: input data
 *
 * return: (0:ok other:fail)
 */
static int gdix_tp_flash_write(struct goodix_ts_core *cd,
		unsigned int addr, unsigned char *buf,
		unsigned int len)
{
	int ret = -1;
	u32 have_written_len = 0;
	u32 write_len = 0;

	while (have_written_len < len) {
		if (have_written_len + FLASH_WRITE_MAX_LEN < len)
				write_len = FLASH_WRITE_MAX_LEN;
			else
				write_len = len - have_written_len;

		ret = cd->hw_ops->write_flash(cd, addr + have_written_len,
			buf + have_written_len, write_len);
		if (ret) {
			ts_err("tp_protocol_flash_write failed!");
			break;
		}
		have_written_len += write_len;
	}

	return ret;
}

/*
 * Description: tp_flash_read, read 4K once at most
 * @addr: flash addr
 * @data_len: data len
 * @buf: output data
 *
 * return: (0:ok other:fail)
 */
static int gdix_tp_flash_read(struct goodix_ts_core *cd,
		unsigned int addr, unsigned char *buf,
		unsigned int len)
{
	int ret = -1;
	u32 have_read_len = 0;
	u32 read_len = 0;

	while (have_read_len < len) {
		if (have_read_len + FLASH_READ_MAX_LEN < len)
			read_len = FLASH_READ_MAX_LEN;
		else
			read_len = len - have_read_len;

		ret = cd->hw_ops->read_flash(cd, addr + have_read_len,
			buf + have_read_len, read_len);
		if (ret) {
			ts_err("flash read failed!");
			break;
		}
		have_read_len += read_len;
	}

	return ret;
}

static void goto_next_line(char **ptr)
{
	do {
		*ptr = *ptr + 1;
	} while (**ptr != '\n' && **ptr != '\0');
	if (**ptr == '\0')
		return;
	*ptr = *ptr + 1;
}

static int parse_csvfile(char *buf, size_t size, char *target_name,
		struct normalize_k_param *k_param)
{
	int ret = 0;
	char *ptr = NULL;
	char *token = NULL;
	int i;
	s32 val;

	if (size <= 0)
		return -ENXIO;

	memset((u8 *)k_param, 0, sizeof(*k_param));

	ptr = buf;
	ptr = strstr(ptr, target_name);
	if (!ptr) {
		ts_err("load %s failed 1, maybe not this item", target_name);
		return -EINTR;
	}
	strsep(&ptr, ",");

	token = strsep(&ptr, ",");
	if (!token)
		return -EINTR;
	if (kstrtos32(token, 10, &val))
		return -EINTR;
	k_param->head.smp_point_num = val;

	token = strsep(&ptr, ",");
	if (!token)
		return -EINTR;
	if (kstrtos32(token, 10, &val))
		return -EINTR;
	k_param->head.freq_factor = val;

	token = strsep(&ptr, ",");
	if (!token)
		return -EINTR;
	if (kstrtos32(token, 10, &val))
		return -EINTR;
	k_param->head.gain_c = val;

	token = strsep(&ptr, ",");
	if (!token)
		return -EINTR;
	if (kstrtos32(token, 10, &val))
		return -EINTR;
	k_param->head.dump_ssl = val;

	token = strsep(&ptr, ",");
	if (!token)
		return -EINTR;
	if (kstrtos32(token, 10, &val))
		return -EINTR;
	k_param->head.rx_num = val;

	token = strsep(&ptr, ",");
	if (!token)
		return -EINTR;
	if (kstrtos32(token, 10, &val))
		return -EINTR;
	k_param->head.tx_num = val;

	token = strsep(&ptr, ",");
	if (!token)
		return -EINTR;
	if (kstrtos32(token, 10, &val))
		return -EINTR;
	k_param->head.version = val;

	token = strsep(&ptr, ",");
	if (!token)
		return -EINTR;
	if (kstrtos32(token, 10, &val))
		return -EINTR;
	k_param->head.freq_scan_state = val;

	token = strsep(&ptr, ",");
	if (!token)
		return -EINTR;
	if (kstrtos32(token, 10, &val))
		return -EINTR;
	k_param->head.flash_write_time = val;

	ts_debug("smp_point_num:%d freq_factor:%d gain_c:%d dump_ssl:%d rx_num:%d tx_num:%d version:%d freq_scan_state:%d flash_write_time:%d",
		k_param->head.smp_point_num, k_param->head.freq_factor, k_param->head.gain_c, k_param->head.dump_ssl,
		k_param->head.rx_num, k_param->head.tx_num, k_param->head.version, k_param->head.freq_scan_state,
		k_param->head.flash_write_time);

	k_param->size = k_param->head.tx_num * k_param->head.rx_num;
	k_param->data = kcalloc(k_param->size, sizeof(u16), GFP_KERNEL);
	if (!k_param->data)
		return -ENOMEM;

	goto_next_line(&ptr);
	if (!ptr || (0 == strlen(ptr))) {
		ret = -EIO;
		goto err_exit;
	}

	for (i = 0; i < k_param->size; i++) {
		token = strsep(&ptr, ",");
		if (!token) {
			ret = -EINTR;
			goto err_exit;
		}
		if (kstrtos32(token, 10, &val)) {
			ret = -EINTR;
			goto err_exit;
		}
		k_param->data[i] = val;
		if (((i + 1) % k_param->head.rx_num == 0) && (i < k_param->size - 1)) {
			goto_next_line(&ptr);
			if (!ptr || (0 == strlen(ptr))) {
				ret = -EIO;
				goto err_exit;
			}
		}
	}

	return 0;
err_exit:
	if (k_param->data) {
		kfree(k_param->data);
		k_param->data = NULL;
	}
	k_param->size = 0;
	return ret;
}

static int parse_normalize_k_file(struct goodix_ts_core *cd)
{
	const struct firmware *firmware = NULL;
	struct device *dev = &cd->pdev->dev;
	char *temp_buf = NULL;
	char target[10];
	int i;
	int ret;

	ret = request_firmware(&firmware, NORMALIZE_K_FILE_NAME, dev);
	if (ret < 0) {
		ts_err("normalize k file [%s] not available", NORMALIZE_K_FILE_NAME);
		return -EINVAL;
	}
	if (firmware->size < 100) {
		ts_err("request_firmware, normalize param length error,len:%zu",
			firmware->size);
		ret = -EINVAL;
		goto exit_free;
	}
	temp_buf = kzalloc(firmware->size + 1, GFP_KERNEL);
	if (!temp_buf) {
		ts_err("kzalloc bytes failed.");
		ret = -ENOMEM;
		goto exit_free;
	}

	for (i = 0; i < cd->ic_info.parm.mutual_freq_num; i++) {
		snprintf(target, 10, "freq%d", i + 1);
		memcpy(temp_buf, firmware->data, firmware->size);
		ret = parse_csvfile(temp_buf, firmware->size, target, &normalize_k_array[i]);
		if (ret == 0)
			ts_info("get target[%s] ok", target);
		else
			ts_err("get target[%s] fail", target);
	}

exit_free:
	if (temp_buf)
		kfree(temp_buf);
	if (firmware)
		release_firmware(firmware);
	return ret;
}

/* return: if need update return < 0. otherwise return 0 */
static int check_for_normalize_k_update(u16 current_ver)
{
	normalize_k_head_t *normalize_head = NULL;


	if (normalize_k_array[0].size == 0 || !normalize_k_array[0].data) {
		ts_err("can't find valid normalize param, skip update normalize coeffi");
		return 0;
	}

	if (current_ver == 0xFFFF) {
		ts_info("current normalize K version 0x%x, need update", current_ver);
		return -1;
	}

	normalize_head = &normalize_k_array[0].head;
	if (current_ver != normalize_head->version) {
		ts_info("K version unequal need update 0x%x != 0x%x",
		current_ver, normalize_head->version);
		return -1;
	}
	ts_info("no need update normalize coeffi, k version: %d", current_ver);
	return 0;
}

static u16 get_config_checksum(u8 *module_config)
{
	int i, sub_cfg_len, cfg_num, module_index;
	u8 temp_buf[260];
	u16 totcal_checksum = 0;
	u16 ret;

	cfg_num = module_config[61];

	module_config += 64;
	for (i = 0; i < cfg_num; i++) {
		sub_cfg_len = module_config[0] - 2;
		module_index = module_config[1];
		if (module_index == 1 || module_index == 2 || module_index == 20 || module_index == 82) {
			memcpy(temp_buf, &module_config[2], sub_cfg_len);
			ret = goodix_append_checksum(temp_buf, sub_cfg_len, CHECKSUM_MODE_U16_LE);
			totcal_checksum += ret;
		}
		module_config += (sub_cfg_len + 2);
	}

	ts_info("cfg_checksum:%x", totcal_checksum);
	return totcal_checksum;
}

/*
 * @prj_id: project id
 * @freq_index: from 0 - 4
 * @tx: Tx num
 * @rx: Rx num
 * @pack: buffer for store final normalize data
 *
 * return: on success return package length, otherwise -1 is returned.
 */
static int get_single_freq_norm_pack(int freq_index, int tx, int rx, u8 *pack)
{
	int pack_len = 0, i;
	u32 u32_checksum = 0;
	u16 *u16_ptr = NULL;
	normalize_k_head_t *normalize_head = NULL;
	u16 *normalize_k = NULL;

	normalize_head = &normalize_k_array[freq_index].head;
	normalize_k = normalize_k_array[freq_index].data;

	if (rx != normalize_head->rx_num || tx != normalize_head->tx_num) {
		ts_err("rx_num or tx_num not match, need update fw");
		ts_err("RX: %d, rx_num: %d, TX: %d, tx_num %d",
			rx, normalize_head->rx_num, tx, normalize_head->tx_num);
		return -1;
	}
	memcpy(pack, normalize_k, sizeof(*normalize_k) * tx * rx);
	pack_len = sizeof(*normalize_k) * tx * rx;
	memcpy(pack + pack_len, normalize_head, sizeof(*normalize_head));
	pack_len += sizeof(*normalize_head);

	/* get normalize param k version*/
	ts_info("norm parm: smp point %d, freq_factor %d, rx %d, tx %d, k_ver %d",
		normalize_head->smp_point_num, normalize_head->freq_factor,
		normalize_head->rx_num, normalize_head->tx_num, normalize_head->version);

	/* change byte order of K array */
	u16_ptr = (u16 *)pack;
	for (i = 0; i < tx * rx; i++)
		*(u16_ptr + i) = cpu_to_le16(*(u16_ptr + i));

	/* change byte order of head */
	normalize_head = (normalize_k_head_t *)(pack + sizeof(*normalize_k) * tx * rx);
	normalize_head->smp_point_num = cpu_to_le16(normalize_head->smp_point_num);
	normalize_head->freq_factor = cpu_to_le16(normalize_head->freq_factor);
	normalize_head->version = cpu_to_le16(normalize_head->version);
	normalize_head->config_checksum = g_cfg_checksum;

	/*calculate checksum*/
	u32_checksum = 0;
	for (i = 0; i < pack_len; i += 2)
		u32_checksum += pack[i] | (pack[i + 1] << 8);

	/* add checksum to the end of package */
	u32_checksum = cpu_to_le32(u32_checksum);
	memcpy(pack + pack_len, &u32_checksum, sizeof(u32_checksum));
	pack_len += sizeof(u32_checksum);

	if (checksum_cmp(pack, pack_len, CHECKSUM_MODE_U16_LE)) {
		ts_err("checksum error");
		return -1;
	}

	return pack_len;
}

static int get_normalize_info_pack(struct goodix_ts_core *cd, u8 *output_pack)
{
	int i = 0;
	int single_pack_len = 0, total_pack_len = 0;

	g_cfg_checksum = get_config_checksum(cd->ic_configs[CONFIG_TYPE_NORMAL]->data);

	for (i = 0; i < cd->ic_info.parm.mutual_freq_num; i++) {
		single_pack_len = get_single_freq_norm_pack(i,
			cd->ic_info.parm.drv_num, cd->ic_info.parm.sen_num,
			output_pack + total_pack_len);
		if (single_pack_len <= 0)
			return -1;
		total_pack_len += single_pack_len;
	}

	return total_pack_len;
}

#define NORMALIZE_K_FLASH_ADDR 0x20000
int goodix_normalize_coeffi_update(struct goodix_ts_core *cd)
{
	int pack_len = 0, ret = -1;
	int retry = 0;
	int i;
	u8 *output_pack = NULL;
	u8 *read_back_pack = NULL;

	if (!cd)
		return -1;

	ret = parse_normalize_k_file(cd);
	if (ret < 0) {
		ts_err("parse normalize k file failed");
		goto err_out;
	}

	if (!check_for_normalize_k_update(cd->ic_info.misc.normalize_k_version)) {
		ret = 0;
		goto err_out;
	}

	output_pack = kzalloc(1024 * 16, GFP_KERNEL);
	read_back_pack = kzalloc(1024 * 16, GFP_KERNEL);
	if (!output_pack || !read_back_pack) {
		ts_err("failed alloc memory");
		ret = -1;
		goto err_out;
	}

	pack_len = get_normalize_info_pack(cd, output_pack);
	if (pack_len <= 0) {
		ts_err("get normalize info pack failed, pack_len:%d!", pack_len);
		ret = -1;
		goto err_out;
	}

	for (retry = 0; retry < 3; retry++) {
		ret = gdix_tp_flash_write(cd, NORMALIZE_K_FLASH_ADDR, output_pack, (u32)pack_len);
		if (ret) {
			ts_err("flash write normalize coeffi failed, ret:%d", ret);
			continue;
		}
		ts_info(">>>>>>>write normalize K to flash");
		ret = gdix_tp_flash_read(cd, NORMALIZE_K_FLASH_ADDR, read_back_pack, (u32)pack_len);
		if (ret) {
			ts_err("flash read normalize coeffi failed, ret:%d", ret);
			continue;
		}

		if (memcmp(output_pack, read_back_pack, pack_len)) {
			ts_err("write normalize coeffi not equal to read back data, reflash failed, retry:%d", retry);
			ret = -1;
			continue;
		}

		/*reset IC*/
		cd->hw_ops->reset(cd, 350);
		ts_info("read back ok, write normalize coeffi success!");
		break;
	}

err_out:
	if (output_pack) {
		kfree(output_pack);
		output_pack = NULL;
	}
	if (read_back_pack) {
		kfree(read_back_pack);
		read_back_pack = NULL;
	}
	for (i = 0; i < MAX_SCAN_FREQ_NUM; i++) {
		kfree(normalize_k_array[i].data);
		normalize_k_array[i].data = NULL;
	}
	return ret;
}
