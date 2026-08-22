/*
 * Copyright (c) 2023 Xiaomi, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Xiaomi, Inc.
 */

#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/dma-mapping.h>

#include "xiaomi_touch.h"
#include "xiaomi_touch_type_common.h"

static const u32 IOCTL_MAGIC_NUMBER = 'T';

static xiaomi_touch_t *xiaomi_touch;

static int xiaomi_touch_dev_open(struct inode *inode, struct file *file)
{
	private_data_t *client_private_data = kzalloc_retry(sizeof(private_data_t), 3);
	if (!client_private_data) {
		LOG_ERROR("alloc private memory failed!");
		return -1;
	}
	xiaomi_touch->use_count++;
	client_private_data->touch_id = -1;

	init_waitqueue_head(&client_private_data->poll_wait_queue_head);
	init_waitqueue_head(&client_private_data->poll_wait_queue_head_for_cmd);
	init_waitqueue_head(&client_private_data->poll_wait_queue_head_for_frame);
	init_waitqueue_head(&client_private_data->poll_wait_queue_head_for_raw);

	file->private_data = client_private_data;
	LOG_DEBUG("open xiaomi_touch sucess! private data is %p, open count %d", client_private_data, xiaomi_touch->use_count);
	return 0;
}

static int xiaomi_touch_dev_release(struct inode *inode, struct file *file)
{
	private_data_t *client_private_data = file->private_data;
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(client_private_data->touch_id);
	if (!xiaomi_touch_data) {
		goto release_end;
	}

	spin_lock(&xiaomi_touch_data->private_data_lock);
	list_del_rcu(&client_private_data->node);
	spin_unlock(&xiaomi_touch_data->private_data_lock);
	synchronize_rcu();

release_end:
	xiaomi_touch->use_count--;
	LOG_DEBUG("close xiaomi_touch sucess! private data is %p, open count %d", client_private_data, xiaomi_touch->use_count);
	kzalloc_free(client_private_data);
	return 0;
}

static ssize_t xiaomi_touch_dev_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	private_data_t *client_private_data = file->private_data;
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(client_private_data->touch_id);
	int copy_size = 0;

	if (!xiaomi_touch_data)
		return 0;

	if (count != sizeof(common_data_t)) {
		LOG_ERROR("error count %zu to read, need %lu", count, sizeof(common_data_t));
		return 0;
	}

	mutex_lock(&xiaomi_touch_data->common_data_buf_lock);
	LOG_DEBUG("index %d, buf index %d", atomic_read(&client_private_data->common_data_index),
		atomic_read(&xiaomi_touch_data->common_data_buf_index));
	if (atomic_read(&client_private_data->common_data_index) == atomic_read(&xiaomi_touch_data->common_data_buf_index)) {
		LOG_DEBUG("empty common data!");
		count = 0;
		goto read_common_data_end;
	}

	LOG_DEBUG("start read common data! count = %zu", count);
	copy_size = copy_to_user((void __user *)buf,
		&xiaomi_touch_data->common_data_buf[atomic_read(&client_private_data->common_data_index)], count);
	if (copy_size) {
		LOG_ERROR("read common data hasn't complete! value %d", copy_size);
		count = -1;
		goto read_common_data_end;
	}

	atomic_inc(&client_private_data->common_data_index);
	if (atomic_read(&client_private_data->common_data_index) >= COMMON_DATA_BUF_SIZE) {
		atomic_set(&client_private_data->common_data_index, 0);
	}

read_common_data_end:
	LOG_DEBUG("end read common data! count = %zu", count);
	mutex_unlock(&xiaomi_touch_data->common_data_buf_lock);
	return count;
}

static ssize_t xiaomi_touch_dev_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	int copy_size = 0;
	static char temp_buf[1024];

	if (count >= 1024) {
		return count;
	}
	copy_size = copy_from_user(temp_buf, (void __user *)buf, count);
	if (copy_size) {
		LOG_ERROR("copy from user failed! copy size %d", copy_size);
		return count;
	}
	if (count == 32 && !strncmp("thp_time", temp_buf, 8)) {
		s64 *time = (s64 *)&temp_buf[8];
		add_input_event_timeline_before_event_time(1, time[2], time[0], time[1]);
		/* LOG_INFO("frame count is %ld, time is %ld, %ld", time[2], time[0], time[1]); */
		return count;
	}
	if ((count == 22 || count == 23) && !strncmp("enable_input_timeline", temp_buf, 21)) {
		int value = 0;
		if (temp_buf[21] == '1') {
			value = init_input_event_timeline();
		} else if (temp_buf[21] == '0') {
			release_input_event_timeline();
		}
		add_common_data_to_buf(0, SET_CUR_VALUE, DATA_MODE_133, 1, &value);
		return count;
	}

	return count;
}

static unsigned int xiaomi_touch_dev_poll(struct file *file, poll_table *wait)
{
	static u32 poll_event = POLLPRI | POLLRDNORM | POLLRDBAND;
	u32 wait_event = wait->_key & poll_event;
	unsigned int mask = 0;
	private_data_t *client_private_data = file->private_data;
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(client_private_data->touch_id);

	if (!xiaomi_touch_data || wait_event == 0) {
		return 0;
	}

	if (wait_event == POLLPRI) {
		poll_wait(file, &client_private_data->poll_wait_queue_head_for_frame, wait);
	} else if (wait_event == POLLRDNORM) {
		poll_wait(file, &client_private_data->poll_wait_queue_head_for_cmd, wait);
	} else if (wait_event == POLLRDBAND) {
		poll_wait(file, &client_private_data->poll_wait_queue_head_for_raw, wait);
	} else {
		poll_wait(file, &client_private_data->poll_wait_queue_head, wait);
	}

	if (atomic_read(&client_private_data->common_data_index) != atomic_read(&xiaomi_touch_data->common_data_buf_index)) {
		mask |= POLLRDNORM;
	}
	if (atomic_read(&client_private_data->frame_data_index) != atomic_read(&xiaomi_touch_data->frame_data_buf_index)) {
		mask |= POLLPRI;
	}
	if (atomic_read(&client_private_data->raw_data_index) != atomic_read(&xiaomi_touch_data->raw_data_buf_index)) {
		mask |= POLLRDBAND;
	}
	LOG_DEBUG("poll mask is %d", mask);
	return mask;
}

static int xiaomi_touch_dev_mmap(struct file *file, struct vm_area_struct *vma)
{
	private_data_t *client_private_data = file->private_data;
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long page;
	unsigned long pos;
	dma_addr_t temp_phy_bas = 0;
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(client_private_data->touch_id);
	if (!xiaomi_touch_data && client_private_data->mmap_area != 3) {
		LOG_ERROR("please select touch id first!");
		return -1;
	}

	LOG_INFO("select mmap area %d", client_private_data->mmap_area);
	if (client_private_data->mmap_area == 1) {
		LOG_INFO("mmap frame data mmap");
		temp_phy_bas = xiaomi_touch_data->frame_data_mmap_phy_base;
	} else if (client_private_data->mmap_area == 2) {
		LOG_INFO("mmap raw data mmap");
		temp_phy_bas = xiaomi_touch_data->raw_data_mmap_phy_base;
	} else if (client_private_data->mmap_area == 3) {
		LOG_INFO("mmap input event buf mmap");
		temp_phy_bas = xiaomi_touch->input_event_time_line_phy_base;
	} else if (client_private_data->mmap_area == 4) {
		LOG_INFO("mmap report point buf mmap");
		temp_phy_bas = get_report_point_info_phy_addr();
	}
	if (!temp_phy_bas) {
		LOG_ERROR("phy bas is NULL, return!");
		return -1;
	}
	pos = (unsigned long)temp_phy_bas + offset;
	page = pos >> PAGE_SHIFT;

	if(remap_pfn_range(vma, start, page, size, PAGE_SHARED)) {
		LOG_ERROR("remap_pfn_range %u, size:%ld failed\n", (unsigned int)page, size);
		return -1;
	}
	LOG_INFO("remap_pfn_range %u, size:%ld, success\n", (unsigned int)page, size);
	return 0;
}

static long xiaomi_touch_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	private_data_t *client_private_data = file->private_data;
	xiaomi_touch_data_t *xiaomi_touch_data = NULL;
	u32 user_cmd = _IOC_NR(cmd);
	u32 user_size = _IOC_SIZE(cmd);
	u32 user_magic_number = _IOC_TYPE(cmd);
	u8 temp_index = 0;

	if (IOCTL_MAGIC_NUMBER != user_magic_number)
		return -EINVAL;
	LOG_DEBUG("user cmd %d, user size %d, user magic number %d", user_cmd, user_size, user_magic_number);
	switch (user_cmd) {
	case RAW_DATA_INDEX:
		xiaomi_touch_data = get_xiaomi_touch_data(client_private_data->touch_id);
		if (!xiaomi_touch_data)
			return -1;
		if (_IOC_DIR(cmd) == _IOC_WRITE) {
			LOG_DEBUG("raw data update, buf index is %d", atomic_read(&xiaomi_touch_data->raw_data_buf_index));
			atomic_inc(&xiaomi_touch_data->raw_data_buf_index);
			if (atomic_read(&xiaomi_touch_data->raw_data_buf_index) >= xiaomi_touch_data->raw_data_buf_size)
				atomic_set(&xiaomi_touch_data->raw_data_buf_index, 0);
			notify_xiaomi_touch(xiaomi_touch_data, RAW_DATA_NOTIFY);
			return 0;
		} else if (_IOC_DIR(cmd) == _IOC_READ) {
			temp_index = atomic_read(&client_private_data->raw_data_index);
			if (temp_index == atomic_read(&xiaomi_touch_data->raw_data_buf_index)) {
				return -1;
			}
			atomic_inc(&client_private_data->raw_data_index);
			if (atomic_read(&client_private_data->raw_data_index) >= xiaomi_touch_data->raw_data_buf_size)
				atomic_set(&client_private_data->raw_data_index, 0);
			LOG_DEBUG("get raw data index is %d", temp_index);
			return temp_index;
		}
		break;
	case GET_FRAME_DATA_INDEX:
		xiaomi_touch_data = get_xiaomi_touch_data(client_private_data->touch_id);
		if (!xiaomi_touch_data)
			return -1;
		temp_index = atomic_read(&client_private_data->frame_data_index);
		if (temp_index == atomic_read(&xiaomi_touch_data->frame_data_buf_index))
			return -1;

		atomic_inc(&client_private_data->frame_data_index);
		if (atomic_read(&client_private_data->frame_data_index) >= xiaomi_touch_data->frame_data_buf_size)
			atomic_set(&client_private_data->frame_data_index, 0);
		LOG_DEBUG("get frame data index is %d", temp_index);
		return temp_index;

	case SELECT_MMAP_CMD:
		LOG_INFO("select mmap area %lu", arg);
		client_private_data->mmap_area = arg;
		return 0;
	case UPDATE_REPORT_POINT:
		report_touch_event(client_private_data->touch_id, arg);
		return 0;
	case SELECT_TOUCH_ID:
		if (client_private_data->touch_id < 0) {
			if (IS_TOUCH_ID_INVALID(arg)) {
				LOG_INFO("select touch id %lu is out of range, ignore it!", arg);
				return -1;
			}
			LOG_INFO("select touch id %lu", arg);
			xiaomi_touch_data = get_xiaomi_touch_data(arg);
			if (!xiaomi_touch_data)
				return -1;
			client_private_data->touch_id = arg;
			atomic_set(&client_private_data->frame_data_index, atomic_read(&xiaomi_touch_data->frame_data_buf_index));
			LOG_INFO("start frame data buf index is %d", atomic_read(&client_private_data->frame_data_index));
			atomic_set(&client_private_data->raw_data_index, atomic_read(&xiaomi_touch_data->raw_data_buf_index));
			LOG_INFO("start raw data buf index is %d", atomic_read(&client_private_data->raw_data_index));
			spin_lock(&xiaomi_touch_data->private_data_lock);
			list_add_tail_rcu(&client_private_data->node, &xiaomi_touch_data->private_data_list);
			spin_unlock(&xiaomi_touch_data->private_data_lock);
		} else {
			LOG_INFO("touch id has set, don't select again!");
		}
		return 0;
	case HARDWARE_PARAM_CMD:
		if (user_size != sizeof(hardware_param_t)) {
			LOG_ERROR("error get hw param size %d %lu, return!", user_size, sizeof(hardware_param_t));
			return -EINVAL;
		} else {
			xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(client_private_data->touch_id);
			if (!xiaomi_touch_driver_param) {
				LOG_ERROR("get hardware info failed, please select touch id first");
				return -1;
			}
			return copy_to_user((void __user *)arg, &xiaomi_touch_driver_param->hardware_param, user_size);
		}
		return -1;
	case COMMON_DATA_CMD:
		return xiaomi_touch_mode(client_private_data, user_size, arg);
	default:
		LOG_ERROR("unrecognize user cmd, magic number is %d, cmd is %d, size is %d, return!", user_magic_number, user_cmd, user_size);
		return -EINVAL;
	}
	return -EINVAL;
}

static const struct file_operations xiaomitouch_dev_fops = {
	.owner = THIS_MODULE,
	.open = xiaomi_touch_dev_open,
	.read = xiaomi_touch_dev_read,
	.write = xiaomi_touch_dev_write,
	.poll = xiaomi_touch_dev_poll,
	.mmap = xiaomi_touch_dev_mmap,
	.unlocked_ioctl = xiaomi_touch_dev_ioctl,
	.compat_ioctl = xiaomi_touch_dev_ioctl,
	.release = xiaomi_touch_dev_release,
	.llseek = no_llseek,
};

static struct miscdevice misc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "xiaomi-touch",
	.fops = &xiaomitouch_dev_fops,
	.parent = NULL,
};

int xiaomi_touch_operation_init(xiaomi_touch_t *temp_xiaomi_touch)
{
	int ret = 0;
	if (!temp_xiaomi_touch) {
		LOG_ERROR("xiaomi touch is NULL, return!");
		return -1;
	}
	ret = misc_register(&misc_dev);
	xiaomi_touch = temp_xiaomi_touch;
	if (ret)
		LOG_ERROR("create misc device err:%d\n", ret);
	return ret;
}

int xiaomi_touch_operation_remove(void)
{
	misc_deregister(&misc_dev);
	return 0;
}
