/*Add synaptics new driver "Synaptics DSX I2C V2.0"*/
/*
 * Synaptics DSX touchscreen driver
 *
 * Copyright (C) 2012 Synaptics Incorporated
 *
 * Copyright (C) 2012 Alexandra Chin <alexandra.chin@tw.synaptics.com>
 * Copyright (C) 2012 Scott Lin <scott.lin@tw.synaptics.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
/*Add for aging test*/
#include <linux/synaptics_dsx.h>
#include <linux/huawei_tp_adapter.h>
#include "synaptics_dsx_i2c.h"
#ifdef KERNEL_ABOVE_2_6_38
#include <linux/input/mt.h>
#endif
#include <linux/of_gpio.h>

#ifdef CONFIG_HUAWEI_KERNEL
#ifdef CONFIG_HUAWEI_HW_DEV_DCT
#include <linux/hw_dev_dec.h>
#endif
#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif
#include <linux/hw_tp_common.h>
#include <linux/pm_runtime.h>
int synaptics_dsx_debug_mask = TP_INFO;
module_param_named(synaptics_dsx_debug_mask, synaptics_dsx_debug_mask, int, 0664);

//#define DEBUG
#define USE_IRQ_THREAD 1
#define USE_BTN_TOOL_FINGER 0
#define LCD_X_DEFAULT 480
#define LCD_Y_DEFAULT 800
#define LCD_ALL_DEFAULT 854
#define USE_WAKEUP_GESTURE 1
#define REPORT_GESTURE_LOCUS 0


#if USE_WAKEUP_GESTURE
#define GESTURE_FROM_APP(_x) (_x)

#define F11_2D_DATA28_OFFSET 53
#define F11_2D_DATA38_OFFSET 54
#define F11_2D_DATA39_OFFSET 55
#define F51_CUSTOM_DATA24_OFFSET 24

#if REPORT_GESTURE_LOCUS
#define F51_LOCUS_DATA_NUM 24
#define F51_LOCUS_DATA_LENS 4
#define F51_LETTER_LOCUS_NUM 6
#define F51_LINEAR2_LOCUS_NUM 4
#define F51_LINEAR_LOCUS_NUM 2
#endif/*REPORT_GESTURE_LOCUS*/

/*Gesture register(F11_2D_data38) value*/
#define DOUBLE_CLICK_WAKEUP  (1<<0)
#define LINEAR_SLIDE_DETECTED  (1<<1)
#define CIRCLE_SLIDE_DETECTED  (1<<3)
#define SPECIFIC_LETTER_DETECTED  (1<<6)

/*Linear esture register(F51_custom_data0) value*/
#define LINEAR_SLIDE_LEFT_TO_RIGHT 0X41
#define LINEAR_SLIDE_RIGHT_TO_LEFT 0X42
#define LINEAR_SLIDE_TOP_TO_BOTTOM 0X44
#define LINEAR_SLIDE_BOTTOM_TO_TOP 0X48
#define LINEAR_SLIDE_TOP_TO_BOTTOM2 0X80

/*Letter gesture register(F11_2D_DATA39 (04)/01) value*/
#define SPECIFIC_LETTER_c 0X63
#define SPECIFIC_LETTER_e 0X65
#define SPECIFIC_LETTER_m 0X6D
#define SPECIFIC_LETTER_w 0X77

#define GESTURE_TIMER_INTERVAL  HZ //jiffies 1S

enum synaptics_gesture_num {
	GESTURE_DOUBLE_CLICK = 0,
	GESTURE_SLIDE_L2R,
	GESTURE_SLIDE_R2L,
	GESTURE_SLIDE_T2B,
	GESTURE_SLIDE_B2T,
	GESTURE_SLIDE_T2B2,
	GESTURE_CIRCLE_SLIDE,
	GESTURE_LETTER_c,
	GESTURE_LETTER_e,
	GESTURE_LETTER_m,
	GESTURE_LETTER_w,
	GESTURE_PALM_COVERED,
	GESTURE_MAX,
};

#define APP_ENABLE_GESTURE(x)  ((u32)(1<<(x)))
#define APP_ENABLE_LINEAR  ((u32)((1<<GESTURE_SLIDE_L2R)|\
										(1<<GESTURE_SLIDE_R2L)|\
										(1<<GESTURE_SLIDE_T2B)|\
										(1<<GESTURE_SLIDE_B2T)|\
										(1<<GESTURE_SLIDE_T2B2)))
#define APP_ENABLE_LETTERS  ((u32)((1<<GESTURE_LETTER_c)|\
									(1<<GESTURE_LETTER_e)|\
									(1<<GESTURE_LETTER_m)|\
									(1<<GESTURE_LETTER_w)))

#define APP_ENABLE_WAKEUP_GESTURE  ((u32)(APP_ENABLE_GESTURE(GESTURE_DOUBLE_CLICK))|\
										(APP_ENABLE_LINEAR)|\
										(APP_ENABLE_GESTURE(GESTURE_CIRCLE_SLIDE))|\
										(APP_ENABLE_LETTERS))
	
#endif/*USE_WAKEUP_GESTURE*/

static struct device *core_dev_ptr = NULL;

#ifdef USE_IRQ_THREAD
#else
static struct workqueue_struct *synaptics_wq;
#endif

#ifdef ENABLE_VIRTUAL_KEYS
static struct kobject *board_properties_kobj;
#endif
#endif /*CONFIG_HUAWEI_KERNEL*/



#define DRIVER_NAME "synaptics_dsx_i2c"
#define INPUT_PHYS_NAME "synaptics_dsx_i2c/input0"

#ifdef KERNEL_ABOVE_2_6_38
#define TYPE_B_PROTOCOL
#endif

#define NO_0D_WHILE_2D
/*
#define REPORT_2D_Z
*/
#define REPORT_2D_W

#define F12_DATA_15_WORKAROUND

#ifdef CONFIG_HUAWEI_KERNEL
#define IGNORE_FN_INIT_FAILURE
#endif /*CONFIG_HUAWEI_KERNEL*/
	

#define RPT_TYPE (1 << 0)
#define RPT_X_LSB (1 << 1)
#define RPT_X_MSB (1 << 2)
#define RPT_Y_LSB (1 << 3)
#define RPT_Y_MSB (1 << 4)
#define RPT_Z (1 << 5)
#define RPT_WX (1 << 6)
#define RPT_WY (1 << 7)
#define RPT_DEFAULT (RPT_TYPE | RPT_X_LSB | RPT_X_MSB | RPT_Y_LSB | RPT_Y_MSB)

#define EXP_FN_WORK_DELAY_MS 1000 /* ms */
#define SYN_I2C_RETRY_TIMES 10
#define MAX_F11_TOUCH_WIDTH 15

#define CHECK_STATUS_TIMEOUT_MS 100

#define F01_STD_QUERY_LEN 21
#define F01_BUID_ID_OFFSET 18
#define F11_STD_QUERY_LEN 9
#define F11_STD_CTRL_LEN 10
#define F11_STD_DATA_LEN 12

#define STATUS_NO_ERROR 0x00
#define STATUS_RESET_OCCURRED 0x01
#define STATUS_INVALID_CONFIG 0x02
#define STATUS_DEVICE_FAILURE 0x03
#define STATUS_CONFIG_CRC_FAILURE 0x04
#define STATUS_FIRMWARE_CRC_FAILURE 0x05
#define STATUS_CRC_IN_PROGRESS 0x06

#define NORMAL_OPERATION (0 << 0)
#define SENSOR_SLEEP (1 << 0)
#define NO_SLEEP_OFF (0 << 2)
#define NO_SLEEP_ON (1 << 2)
#define CONFIGURED (1 << 7)

static int synaptics_rmi4_i2c_read(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data,
		unsigned short length);

static int synaptics_rmi4_i2c_write(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data,
		unsigned short length);

static int synaptics_rmi4_f12_set_enables(struct synaptics_rmi4_data *rmi4_data,
		unsigned short ctrl28);

static int synaptics_rmi4_free_fingers(struct synaptics_rmi4_data *rmi4_data);
static int synaptics_rmi4_reinit_device(struct synaptics_rmi4_data *rmi4_data);
static int synaptics_rmi4_reset_device(struct synaptics_rmi4_data *rmi4_data);
static int synaptics_gpio_setup(int gpio, bool configure, int dir, int state);

#ifdef CONFIG_HAS_EARLYSUSPEND
static ssize_t synaptics_rmi4_full_pm_cycle_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_full_pm_cycle_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static void synaptics_rmi4_early_suspend(struct early_suspend *h);

static void synaptics_rmi4_late_resume(struct early_suspend *h);
#endif

static int synaptics_rmi4_suspend(struct device *dev);

static int synaptics_rmi4_resume(struct device *dev);

static ssize_t synaptics_rmi4_f01_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t synaptics_rmi4_f01_productinfo_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_f01_buildid_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_f01_flashprog_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_0dbutton_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t synaptics_rmi4_0dbutton_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t synaptics_rmi4_suspend_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static ssize_t synaptics_easy_wakeup_supported_gestures_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t synaptics_easy_wakeup_gesture_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t synaptics_easy_wakeup_gesture_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size);

struct synaptics_rmi4_f01_device_status {
	union {
		struct {
			unsigned char status_code:4;
			unsigned char reserved:2;
			unsigned char flash_prog:1;
			unsigned char unconfigured:1;
		} __packed;
		unsigned char data[1];
	};
};

struct synaptics_rmi4_f12_query_5 {
	union {
		struct {
			unsigned char size_of_query6;
			struct {
				unsigned char ctrl0_is_present:1;
				unsigned char ctrl1_is_present:1;
				unsigned char ctrl2_is_present:1;
				unsigned char ctrl3_is_present:1;
				unsigned char ctrl4_is_present:1;
				unsigned char ctrl5_is_present:1;
				unsigned char ctrl6_is_present:1;
				unsigned char ctrl7_is_present:1;
			} __packed;
			struct {
				unsigned char ctrl8_is_present:1;
				unsigned char ctrl9_is_present:1;
				unsigned char ctrl10_is_present:1;
				unsigned char ctrl11_is_present:1;
				unsigned char ctrl12_is_present:1;
				unsigned char ctrl13_is_present:1;
				unsigned char ctrl14_is_present:1;
				unsigned char ctrl15_is_present:1;
			} __packed;
			struct {
				unsigned char ctrl16_is_present:1;
				unsigned char ctrl17_is_present:1;
				unsigned char ctrl18_is_present:1;
				unsigned char ctrl19_is_present:1;
				unsigned char ctrl20_is_present:1;
				unsigned char ctrl21_is_present:1;
				unsigned char ctrl22_is_present:1;
				unsigned char ctrl23_is_present:1;
			} __packed;
			struct {
				unsigned char ctrl24_is_present:1;
				unsigned char ctrl25_is_present:1;
				unsigned char ctrl26_is_present:1;
				unsigned char ctrl27_is_present:1;
				unsigned char ctrl28_is_present:1;
				unsigned char ctrl29_is_present:1;
				unsigned char ctrl30_is_present:1;
				unsigned char ctrl31_is_present:1;
			} __packed;
		};
		unsigned char data[5];
	};
};

struct synaptics_rmi4_f12_query_8 {
	union {
		struct {
			unsigned char size_of_query9;
			struct {
				unsigned char data0_is_present:1;
				unsigned char data1_is_present:1;
				unsigned char data2_is_present:1;
				unsigned char data3_is_present:1;
				unsigned char data4_is_present:1;
				unsigned char data5_is_present:1;
				unsigned char data6_is_present:1;
				unsigned char data7_is_present:1;
			} __packed;
			struct {
				unsigned char data8_is_present:1;
				unsigned char data9_is_present:1;
				unsigned char data10_is_present:1;
				unsigned char data11_is_present:1;
				unsigned char data12_is_present:1;
				unsigned char data13_is_present:1;
				unsigned char data14_is_present:1;
				unsigned char data15_is_present:1;
			} __packed;
		};
		unsigned char data[3];
	};
};

struct synaptics_rmi4_f12_ctrl_8 {
	union {
		struct {
			unsigned char max_x_coord_lsb;
			unsigned char max_x_coord_msb;
			unsigned char max_y_coord_lsb;
			unsigned char max_y_coord_msb;
			unsigned char rx_pitch_lsb;
			unsigned char rx_pitch_msb;
			unsigned char tx_pitch_lsb;
			unsigned char tx_pitch_msb;
			unsigned char low_rx_clip;
			unsigned char high_rx_clip;
			unsigned char low_tx_clip;
			unsigned char high_tx_clip;
			unsigned char num_of_rx;
			unsigned char num_of_tx;
		};
		unsigned char data[14];
	};
};

struct synaptics_rmi4_f12_ctrl_23 {
	union {
		struct {
			unsigned char obj_type_enable;
			unsigned char max_reported_objects;
		};
		unsigned char data[2];
	};
};

struct synaptics_rmi4_f12_finger_data {
	unsigned char object_type_and_status;
	unsigned char x_lsb;
	unsigned char x_msb;
	unsigned char y_lsb;
	unsigned char y_msb;
#ifdef REPORT_2D_Z
	unsigned char z;
#endif
#ifdef REPORT_2D_W
	unsigned char wx;
	unsigned char wy;
#endif
};

struct synaptics_rmi4_f1a_query {
	union {
		struct {
			unsigned char max_button_count:3;
			unsigned char reserved:5;
			unsigned char has_general_control:1;
			unsigned char has_interrupt_enable:1;
			unsigned char has_multibutton_select:1;
			unsigned char has_tx_rx_map:1;
			unsigned char has_perbutton_threshold:1;
			unsigned char has_release_threshold:1;
			unsigned char has_strongestbtn_hysteresis:1;
			unsigned char has_filter_strength:1;
		} __packed;
		unsigned char data[2];
	};
};

struct synaptics_rmi4_f1a_control_0 {
	union {
		struct {
			unsigned char multibutton_report:2;
			unsigned char filter_mode:2;
			unsigned char reserved:4;
		} __packed;
		unsigned char data[1];
	};
};

struct synaptics_rmi4_f1a_control {
	struct synaptics_rmi4_f1a_control_0 general_control;
	unsigned char button_int_enable;
	unsigned char multi_button;
	unsigned char *txrx_map;
	unsigned char *button_threshold;
	unsigned char button_release_threshold;
	unsigned char strongest_button_hysteresis;
	unsigned char filter_strength;
};

struct synaptics_rmi4_f1a_handle {
	int button_bitmask_size;
	unsigned char max_count;
	unsigned char valid_button_count;
	unsigned char *button_data_buffer;
	unsigned char *button_map;
	struct synaptics_rmi4_f1a_query button_query;
	struct synaptics_rmi4_f1a_control button_control;
};

struct synaptics_rmi4_exp_fhandler {
	struct synaptics_rmi4_exp_fn *exp_fn;
	bool insert;
	bool remove;
	struct list_head link;
};

struct synaptics_rmi4_exp_fn_data {
	bool initialized;
	bool queue_work;
	struct mutex mutex;
	struct list_head list;
	struct delayed_work work;
	struct workqueue_struct *workqueue;
	struct synaptics_rmi4_data *rmi4_data;
};

static struct synaptics_rmi4_exp_fn_data exp_data;

/*Add for CTS*/
static struct device_attribute attrs[] = {
#ifdef CONFIG_HAS_EARLYSUSPEND
	__ATTR(full_pm_cycle, (S_IRUGO | S_IWUSR|S_IWGRP),
			synaptics_rmi4_full_pm_cycle_show,
			synaptics_rmi4_full_pm_cycle_store),
#endif
	__ATTR(reset, S_IWUSR|S_IWGRP,
			synaptics_rmi4_show_error,
			synaptics_rmi4_f01_reset_store),
	__ATTR(productinfo, S_IRUGO,
			synaptics_rmi4_f01_productinfo_show,
			synaptics_rmi4_store_error),
	__ATTR(buildid, S_IRUGO,
			synaptics_rmi4_f01_buildid_show,
			synaptics_rmi4_store_error),
	__ATTR(flashprog, S_IRUGO,
			synaptics_rmi4_f01_flashprog_show,
			synaptics_rmi4_store_error),
	__ATTR(0dbutton, (S_IRUGO | S_IWUSR|S_IWGRP),
			synaptics_rmi4_0dbutton_show,
			synaptics_rmi4_0dbutton_store),
	__ATTR(suspend, S_IWUSR|S_IWGRP,
			synaptics_rmi4_show_error,
			synaptics_rmi4_suspend_store),
	__ATTR(easy_wakeup_gesture, (S_IRUGO | S_IWUSR|S_IWGRP),
			synaptics_easy_wakeup_gesture_show,
			synaptics_easy_wakeup_gesture_store),
	__ATTR(easy_wakeup_supported_gestures, S_IRUGO,
			synaptics_easy_wakeup_supported_gestures_show,
			synaptics_rmi4_store_error),
};

#ifdef CONFIG_HAS_EARLYSUSPEND
static ssize_t synaptics_rmi4_full_pm_cycle_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->full_pm_cycle);
}

static ssize_t synaptics_rmi4_full_pm_cycle_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	rmi4_data->full_pm_cycle = input > 0 ? 1 : 0;

	return count;
}
#endif

static ssize_t synaptics_rmi4_f01_reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int reset;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	if (sscanf(buf, "%u", &reset) != 1)
		return -EINVAL;

	if (reset != 1)
		return -EINVAL;

	retval = synaptics_rmi4_reset_device(rmi4_data);
	if (retval < 0) {
		dev_err(dev,
				"%s: Failed to issue reset command, error = %d\n",
				__func__, retval);
		return retval;
	}

	return count;
}

static ssize_t synaptics_rmi4_f01_productinfo_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "0x%02x 0x%02x\n",
			(rmi4_data->rmi4_mod_info.product_info[0]),
			(rmi4_data->rmi4_mod_info.product_info[1]));
}

static ssize_t synaptics_rmi4_f01_buildid_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->firmware_id);
}

static ssize_t synaptics_rmi4_f01_flashprog_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int retval;
	struct synaptics_rmi4_f01_device_status device_status;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr,
			device_status.data,
			sizeof(device_status.data));
	if (retval < 0) {
		dev_err(dev,
				"%s: Failed to read device status, error = %d\n",
				__func__, retval);
		return retval;
	}

	return snprintf(buf, PAGE_SIZE, "%u\n",
			device_status.flash_prog);
}

static ssize_t synaptics_rmi4_0dbutton_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
			rmi4_data->button_0d_enabled);
}

static ssize_t synaptics_rmi4_0dbutton_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int retval;
	unsigned int input;
	unsigned char ii;
	unsigned char intr_enable;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	input = input > 0 ? 1 : 0;

	if (rmi4_data->button_0d_enabled == input)
		return count;

	if (list_empty(&rmi->support_fn_list))
		return -ENODEV;

	list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
		if (fhandler->fn_number == SYNAPTICS_RMI4_F1A) {
			ii = fhandler->intr_reg_num;

			retval = synaptics_rmi4_i2c_read(rmi4_data,
					rmi4_data->f01_ctrl_base_addr + 1 + ii,
					&intr_enable,
					sizeof(intr_enable));
			if (retval < 0)
				return retval;

			if (input == 1)
				intr_enable |= fhandler->intr_mask;
			else
				intr_enable &= ~fhandler->intr_mask;

			retval = synaptics_rmi4_i2c_write(rmi4_data,
					rmi4_data->f01_ctrl_base_addr + 1 + ii,
					&intr_enable,
					sizeof(intr_enable));
			if (retval < 0)
				return retval;
		}
	}

	rmi4_data->button_0d_enabled = input;

	return count;
}

static ssize_t synaptics_rmi4_suspend_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;

	if (sscanf(buf, "%u", &input) != 1)
		return -EINVAL;

	if (input == 1)
		synaptics_rmi4_suspend(dev);
	else if (input == 0)
		synaptics_rmi4_resume(dev);
	else
		return -EINVAL;

	return count;
}

static ssize_t synaptics_easy_wakeup_supported_gestures_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&rmi4_data->rmi4_sysfs_mutex);

	tp_log_debug("%s: easy_wakeup_supported_gestures=0x%04x", __func__, rmi4_data->board->easy_wakeup_supported_gestures);
	ret = snprintf(buf, PAGE_SIZE, "0x%04x\n",rmi4_data->board->easy_wakeup_supported_gestures);
	mutex_unlock(&rmi4_data->rmi4_sysfs_mutex);
	return ret;
}

static ssize_t synaptics_easy_wakeup_gesture_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&rmi4_data->rmi4_sysfs_mutex);

	tp_log_debug("%s: easy_wakeup_gesture=0x%04x", __func__, rmi4_data->easy_wakeup_gesture);
	ret = snprintf(buf, PAGE_SIZE, "0x%04x\n",rmi4_data->easy_wakeup_gesture);
	mutex_unlock(&rmi4_data->rmi4_sysfs_mutex);
	return ret;
}

static ssize_t synaptics_easy_wakeup_gesture_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	unsigned long value = 0;
	unsigned long gesture_flag = 0;
	unsigned long palm_flag = 0;
	int ret;

	ret = kstrtoul(buf, 10, &value);
	if (ret < 0){
		tp_log_err("%s: kstrtoul error,ret=%d", __func__,ret);
		return ret;
	}

	tp_log_debug("%s: value=0x%04x", __func__, (unsigned int)value);
	if (value > 0xFFFF) {
		tp_log_err("%s: Invalid value:%ld", __func__, value);
		return -EINVAL;
	}

	tp_log_debug("%s: gesture_enabled_0=%d", __func__, rmi4_data->gesture_enabled);
	tp_log_debug("%s: palm_enabled_0=%d", __func__, rmi4_data->palm_enabled);

	tp_log_debug("%s:line%d , rt_counter=%d\n",__func__,__LINE__, rmi4_data->i2c_client->dev.power.usage_count.counter);
	pm_runtime_get_sync(&rmi4_data->i2c_client->dev);
	tp_log_debug("%s:line%d , rt_counter=%d\n",__func__,__LINE__, rmi4_data->i2c_client->dev.power.usage_count.counter);

	mutex_lock(&rmi4_data->rmi4_sysfs_mutex);
	gesture_flag = value & APP_ENABLE_WAKEUP_GESTURE;
	tp_log_debug("%s: gesture_flag=%ld", __func__, gesture_flag);
	if (0 != gesture_flag)
		rmi4_data->gesture_enabled = RMI4_EASY_WAKE_UP_GESTURE_ENABLED;
	else
		rmi4_data->gesture_enabled = RMI4_EASY_WAKE_UP_GESTURE_DISABLED;
	tp_log_debug("%s: gesture_enabled=%d", __func__, rmi4_data->gesture_enabled);

	palm_flag = value & APP_ENABLE_GESTURE(GESTURE_PALM_COVERED);
	tp_log_debug("%s: palm_flag=%ld", __func__, palm_flag);
	if (0 != palm_flag)
		rmi4_data->palm_enabled = RMI4_PALM_SLEEP_GESTURE_ENABLED;
	else
		rmi4_data->palm_enabled = RMI4_PALM_SLEEP_GESTURE_DISABLED;
	tp_log_debug("%s: palm_enabled=%d", __func__, rmi4_data->palm_enabled);

	tp_log_debug("%s: GESTURE_FROM_APP=%d", __func__, (u32)GESTURE_FROM_APP(value));
	if (rmi4_data->easy_wakeup_gesture != ((u32)GESTURE_FROM_APP(value)))
		rmi4_data->easy_wakeup_gesture = (u32)GESTURE_FROM_APP(value);
	tp_log_debug("%s: easy_wakeup_gesture=%d", __func__, rmi4_data->easy_wakeup_gesture);
	mutex_unlock(&rmi4_data->rmi4_sysfs_mutex);

	tp_log_debug("%s:line%d , rt_counter=%d\n",__func__,__LINE__, rmi4_data->i2c_client->dev.power.usage_count.counter);
	pm_runtime_put(&rmi4_data->i2c_client->dev);
	tp_log_debug("%s:line%d , rt_counter=%d\n",__func__,__LINE__, rmi4_data->i2c_client->dev.power.usage_count.counter);

	if (ret)
		return ret;

	return size;
}

static ssize_t hw_synaptics_easy_wakeup_gesture_show(struct kobject *dev,
		struct kobj_attribute *attr, char *buf)
{
	struct device *cdev = core_dev_ptr;
	if (!cdev){
		tp_log_err("%s: device is null", __func__);
		return -EINVAL;
	}
	return synaptics_easy_wakeup_gesture_show(cdev, NULL, buf);
}

static ssize_t hw_synaptics_easy_wakeup_gesture_store(struct kobject *dev,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	struct device *cdev = core_dev_ptr;
	if (!cdev){
		tp_log_err("%s: device is null", __func__);
		return -EINVAL;
	}
	return synaptics_easy_wakeup_gesture_store(cdev, NULL, buf, size);
}

static ssize_t hw_synaptics_easy_wakeup_supported_gestures_show(struct kobject *dev,
		struct kobj_attribute *attr, char *buf)
{
	struct device *cdev = core_dev_ptr;
	if (!cdev){
		tp_log_err("%s: device is null", __func__);
		return -EINVAL;
	}
	return synaptics_easy_wakeup_supported_gestures_show(cdev, NULL, buf);
}

static ssize_t synaptics_glove_func_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	ssize_t ret;

	mutex_lock(&rmi4_data->rmi4_sysfs_mutex);

	tp_log_debug("%s: glove_enabled=%d", __func__, rmi4_data->glove_enabled);
	ret = snprintf(buf, PAGE_SIZE, "%d\n",rmi4_data->glove_enabled);
	mutex_unlock(&rmi4_data->rmi4_sysfs_mutex);
	return ret;
}

static ssize_t synaptics_glove_func_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	unsigned long value;
	int ret;
	unsigned char device_ctrl;

	ret = kstrtoul(buf, 10, &value);
	if (ret < 0){
		tp_log_err("%s: kstrtoul error,ret=%d", __func__,ret);
		return ret;
	}

	tp_log_debug("%s: value=%d", __func__, (unsigned int)value);
	if (value < 0 || value > true) {
		tp_log_err("%s: Invalid value:%ld", __func__, value);
		return -EINVAL;
	}

	tp_log_debug("%s: glove_enabled=%d", __func__, rmi4_data->glove_enabled);

	tp_log_debug("%s:line%d , rt_counter=%d\n",__func__,__LINE__, rmi4_data->i2c_client->dev.power.usage_count.counter);
	pm_runtime_get_sync(&rmi4_data->i2c_client->dev);
	tp_log_debug("%s:line%d , rt_counter=%d\n",__func__,__LINE__, rmi4_data->i2c_client->dev.power.usage_count.counter);

	mutex_lock(&rmi4_data->rmi4_sysfs_mutex);
	if (true == value)
	{
		rmi4_data->glove_enabled = RMI4_GLOV_FUNC_ENABLED;
		device_ctrl = SYSTEM_START_IN_GLOVE_MODE;
	}
	else
	{
		rmi4_data->glove_enabled = RMI4_GLOV_FUNC_DISABLED;
		device_ctrl = SYSTEM_LOCKED_TO_SKIN_MODE;
	}
	tp_log_debug("%s: glove_enabled=%d", __func__, rmi4_data->glove_enabled);
	mutex_unlock(&rmi4_data->rmi4_sysfs_mutex);

	ret = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f51_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (ret < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to enter glove mode\n",
				__func__);
	} 

	tp_log_debug("%s:line%d , rt_counter=%d\n",__func__,__LINE__, rmi4_data->i2c_client->dev.power.usage_count.counter);
	pm_runtime_put(&rmi4_data->i2c_client->dev);
	tp_log_debug("%s:line%d , rt_counter=%d\n",__func__,__LINE__, rmi4_data->i2c_client->dev.power.usage_count.counter);

	if (ret)
		return ret;

	return size;
}

static ssize_t hw_glove_func_show(struct kobject *dev,
		struct kobj_attribute *attr, char *buf)
{
	struct device *cdev = core_dev_ptr;
	if (!cdev){
		tp_log_err("%s: device is null", __func__);
		return -EINVAL;
	}
	return synaptics_glove_func_show(cdev, NULL, buf);
}

static ssize_t hw_glove_func_store(struct kobject *dev,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	struct device *cdev = core_dev_ptr;
	if (!cdev){
		tp_log_err("%s: device is null", __func__);
		return -EINVAL;
	}
	return synaptics_glove_func_store(cdev, NULL, buf, size);
}


static inline ssize_t hw_synaptics_rmi4_store_error(struct kobject *dev,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	struct device *cdev = core_dev_ptr;
	if (!cdev){
		tp_log_err("%s: device is null", __func__);
		return -EINVAL;
	}
	return synaptics_rmi4_store_error(cdev, NULL, buf, size);
}

static struct kobj_attribute easy_wakeup_gesture = {
	.attr = {.name = "easy_wakeup_gesture", .mode = (S_IRUGO | S_IWUSR | S_IWGRP)},
	.show = hw_synaptics_easy_wakeup_gesture_show,
	.store = hw_synaptics_easy_wakeup_gesture_store,
};

static struct kobj_attribute easy_wakeup_supported_gestures = {
	.attr = {.name = "easy_wakeup_supported_gestures", .mode = S_IRUGO},
	.show = hw_synaptics_easy_wakeup_supported_gestures_show,
	.store = hw_synaptics_rmi4_store_error,
};

static struct kobj_attribute glove_func = {
	.attr = {.name = "signal_disparity", .mode = (S_IRUGO | S_IWUSR | S_IWGRP)},
	.show = hw_glove_func_show,
	.store = hw_glove_func_store,
};



static int add_easy_wakeup_interfaces(struct device *dev)
{
	int error = 0;
	struct kobject *properties_kobj;
	struct kobject *glove_kobj;
	
	properties_kobj = tp_get_touch_screen_obj();
	if( NULL == properties_kobj )
	{
		tp_log_err("%s: Error, get kobj failed!\n", __func__);
		return -1;
	}
	
	/*add the node easy_wakeup_gesture_supported for apk to read*/
	error = sysfs_create_file(properties_kobj, &easy_wakeup_supported_gestures.attr);
	if (error)
	{
		kobject_put(properties_kobj);
		tp_log_err("%s: synaptics_easy_wakeup_gesture_supported create file error\n", __func__);
		return -ENODEV;
	}
	
	/*add the node easy_wakeup_gesture apk to write*/
	error = sysfs_create_file(properties_kobj, &easy_wakeup_gesture.attr);
	if (error)
	{
		kobject_put(properties_kobj);
		tp_log_err("%s: synaptics_easy_wakeup_gesture create file error\n", __func__);
		return -ENODEV;
	}

	glove_kobj = tp_get_glove_func_obj();
	if( NULL == glove_kobj )
	{
		tp_log_err("%s: Error, get kobj failed!\n", __func__);
		return -1;
	}
	
	/*add the node glove_func/signal_disparity for apk to write*/
	error = sysfs_create_file(glove_kobj, &glove_func.attr);
	if (error)
	{
		kobject_put(glove_kobj);
		error = sysfs_create_file(glove_kobj, &glove_func.attr);
		if (error)
		{
			kobject_put(glove_kobj);
			tp_log_err("%s: glove_func create file error\n", __func__);
			return -ENODEV;
		}
	}
	
	return 0;
}
static int synaptics_gpio_setup(int gpio, bool configure, int dir, int state)
{
	int retval = 0;
	unsigned char buf[16];

	if (configure) {
		snprintf(buf, PAGE_SIZE, "dsx_gpio_%u\n", gpio);

		retval = gpio_request(gpio, buf);
		if (retval) {
			pr_err("%s: Failed to get gpio %d (code: %d)",
					__func__, gpio, retval);
			return retval;
		}

		if (dir == 0)
			retval = gpio_direction_input(gpio);
		else
			retval = gpio_direction_output(gpio, state);
		if (retval) {
			pr_err("%s: Failed to set gpio %d direction",
					__func__, gpio);
			return retval;
		}
	} else {
		tp_log_info("%s: free = %d ",__func__, gpio);
		gpio_free(gpio);
	}

	return retval;
}

 /**
 * synaptics_rmi4_set_page()
 *
 * Called by synaptics_rmi4_i2c_read() and synaptics_rmi4_i2c_write().
 *
 * This function writes to the page select register to switch to the
 * assigned page.
 */
static int synaptics_rmi4_set_page(struct synaptics_rmi4_data *rmi4_data,
		unsigned int address)
{
	int retval = 0;
	unsigned char retry;
	unsigned char buf[PAGE_SELECT_LEN];
	unsigned char page;
	struct i2c_client *i2c = rmi4_data->i2c_client;

	page = ((address >> 8) & MASK_8BIT);
	if (page != rmi4_data->current_page) {
		buf[0] = MASK_8BIT;
		buf[1] = page;
		for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
			retval = i2c_master_send(i2c, buf, PAGE_SELECT_LEN);
			if (retval != PAGE_SELECT_LEN) {
				dev_err(&i2c->dev,
						"%s: I2C retry %d\n",
						__func__, retry + 1);
				msleep(20);
			} else {
				rmi4_data->current_page = page;
				break;
			}
		}
	} else {
		retval = PAGE_SELECT_LEN;
	}

	return retval;
}

 /**
 * synaptics_rmi4_i2c_read()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function reads data of an arbitrary length from the sensor,
 * starting from an assigned register address of the sensor, via I2C
 * with a retry mechanism.
 */
static int synaptics_rmi4_i2c_read(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char buf;
	struct i2c_msg msg[] = {
		{
			.addr = rmi4_data->i2c_client->addr,
			.flags = 0,
			.len = 1,
			.buf = &buf,
		},
		{
			.addr = rmi4_data->i2c_client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = data,
		},
	};

	buf = addr & MASK_8BIT;

	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));

	retval = synaptics_rmi4_set_page(rmi4_data, addr);
	if (retval != PAGE_SELECT_LEN)
		goto exit;

	for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(rmi4_data->i2c_client->adapter, msg, 2) == 2) {
			retval = length;
			break;
		}
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: I2C retry %d\n",
				__func__, retry + 1);
		msleep(20);
	}

	if (retry == SYN_I2C_RETRY_TIMES) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: I2C read over retry limit\n",
				__func__);
		retval = -EIO;
	}

exit:
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));

	return retval;
}

 /**
 * synaptics_rmi4_i2c_write()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function writes data of an arbitrary length to the sensor,
 * starting from an assigned register address of the sensor, via I2C with
 * a retry mechanism.
 */
static int synaptics_rmi4_i2c_write(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char buf[length + 1];
	struct i2c_msg msg[] = {
		{
			.addr = rmi4_data->i2c_client->addr,
			.flags = 0,
			.len = length + 1,
			.buf = buf,
		}
	};

	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));

	retval = synaptics_rmi4_set_page(rmi4_data, addr);
	if (retval != PAGE_SELECT_LEN)
		goto exit;

	buf[0] = addr & MASK_8BIT;
	memcpy(&buf[1], &data[0], length);

	for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(rmi4_data->i2c_client->adapter, msg, 1) == 1) {
			retval = length;
			break;
		}
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: I2C retry %d\n",
				__func__, retry + 1);
		msleep(20);
	}

	if (retry == SYN_I2C_RETRY_TIMES) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: I2C write over retry limit\n",
				__func__);
		retval = -EIO;
	}

exit:
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));

	return retval;
}

#ifdef CONFIG_HUAWEI_KERNEL
 static uint16_t check_scope_x(uint16_t x,int lcd_x)
 {
	 uint16_t temp = x;
	 if (x >= lcd_x -1)
	 {
		 temp = lcd_x -2;
	 }
	 if (x <= 1)
	 {
		 temp = 1;
	 }
 
	 return temp;
 }
#if USE_WAKEUP_GESTURE
static int init_easy_wakeup_key_value(struct synaptics_rmi4_data *rmi4_data)
{
	int i = 0;
	
	if(NULL  == rmi4_data->board->wakeup_keys)
	{
		tp_log_err("%s: wakeup keys is NULL!",__func__);
		return -1;
	}

	if(rmi4_data->board->wakeup_keys->size > GESTURE_MAX)
	{
		tp_log_err("%s: wakeup keys size not match!",__func__);
		return -1;
	}

	set_bit(KEY_POWER, rmi4_data->input_dev->keybit);
	
	for (i = 0; i < rmi4_data->board->wakeup_keys->size; ++i){
		tp_log_debug("%s: wakeup_keys[%d] = %d",__func__,i,rmi4_data->board->wakeup_keys->keys[i]);
		set_bit(rmi4_data->board->wakeup_keys->keys[i], rmi4_data->input_dev->keybit);
	}

	return 0;
}


#if REPORT_GESTURE_LOCUS
static int easy_wakeup_gesture_report_locus(struct synaptics_rmi4_data *rmi4_data,
		unsigned int reprot_gesture_point_num)
{
	int retval = 0;
	unsigned char f51_custom_data[F51_LOCUS_DATA_NUM] = {0};
	int x = 0;
	int y = 0;
	int i = 0;
	int x_lcd = 0;
	int y_lcd_all = 0;
	
	x_lcd = rmi4_data->board->lcd_x;
	y_lcd_all = rmi4_data->board->lcd_all;
	
	if (reprot_gesture_point_num != 0)
	{
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				rmi4_data->f51_data_base_addr,
				f51_custom_data,
				(reprot_gesture_point_num*F51_LOCUS_DATA_LENS));
		if (retval < 0)
		{
			tp_log_err("%s:f51_data_base_addr read error!retval = %d",__func__,retval);
			return retval;
		}
	
		for(i = 0;i < reprot_gesture_point_num;i ++)
		{
			/*
			  *F51_CUSTOM_DATA[00]~[23],every 4 bytes save 1 x/y position of the gesture locus;
			  *i.e.Point 1 has 4 bytes from [00] to [03] :
			  * [00] means LSB of x position,[01] means MSB of x position,
			  * [02] means LSB of y position,[03] means MSB of y position,
			  *The most points num is 6,point from 1(lower address) to 6(higher address) means:
			  *1.beginning 2.end 3.top 4.leftmost 5.bottom 6.rightmost
			  */
			x = ((f51_custom_data[F51_LOCUS_DATA_LENS*i + 1] << 8) | (f51_custom_data[F51_LOCUS_DATA_LENS*i + 0]));
			y = ((f51_custom_data[F51_LOCUS_DATA_LENS*i + 3] << 8) | (f51_custom_data[F51_LOCUS_DATA_LENS*i + 2]));
			tp_log_vdebug("%s:f51_custom_data[%d] = 0x%x",__func__,(4*i + 0),f51_custom_data[4*i + 0]);
			tp_log_vdebug("%s:f51_custom_data[%d] = 0x%x",__func__,(4*i + 1),f51_custom_data[4*i + 1]);
			tp_log_vdebug("%s:f51_custom_data[%d] = 0x%x",__func__,(4*i + 2),f51_custom_data[4*i + 2]);
			tp_log_vdebug("%s:f51_custom_data[%d] = 0x%x",__func__,(4*i + 3),f51_custom_data[4*i + 3]);
			
			x = x * x_lcd / rmi4_data->sensor_max_x;
			x = check_scope_x(x,x_lcd);
			y = y * y_lcd_all / rmi4_data->sensor_max_y;
	
			#ifdef TYPE_B_PROTOCOL
			input_mt_slot(rmi4_data->input_dev, 0);
			input_mt_report_slot_state(rmi4_data->input_dev,MT_TOOL_FINGER, 1);
			#endif
			input_report_key(rmi4_data->input_dev,BTN_TOUCH, 1);
			input_report_abs(rmi4_data->input_dev,ABS_MT_POSITION_X, x);
			input_report_abs(rmi4_data->input_dev,ABS_MT_POSITION_Y, y);
			input_sync(rmi4_data->input_dev);
			#ifndef TYPE_B_PROTOCOL
			input_mt_sync(rmi4_data->input_dev);
			#endif
			
			tp_log_debug("%s: Point %d:\n"
							"x = %d\n"
							"y = %d\n",
							__func__, i,x, y);
		}
		input_report_key(rmi4_data->input_dev,BTN_TOUCH, 0);
	}
	return retval;
}
#endif/*REPORT_GESTURE_LOCUS*/

static int synaptics_rmi4_f11_gesture_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char device_data = 0;
	unsigned char f51_custom_data24 = 0;
	unsigned char f11_2d_data39[9] = {0};
	unsigned int reprot_gesture_key_value = 0;
	unsigned long lTimerCur = jiffies;
	static unsigned long lTimerPre = 0;
	static unsigned long lTimerNext = 0;
#if REPORT_GESTURE_LOCUS
	unsigned int reprot_gesture_point_num = 0;
#endif/*REPORT_GESTURE_LOCUS*/
	static int first_int_flag = true;

	
	if(true == first_int_flag) 
	{
		lTimerPre = jiffies;
		lTimerCur = jiffies;
		first_int_flag = false;
		tp_log_debug("%s:Gesture first interrupt!",__func__);
	}

	tp_log_debug("%s:lTimerCur = %ld,lTimerPre + lTimerNext = %ld",__func__,lTimerCur,(lTimerPre + lTimerNext));
	if(time_before(lTimerCur, lTimerPre + lTimerNext)) 
	{
		tp_log_debug("%s:Gesture Timer Interval is less than 1 senond!",__func__);
		return 0;
	}
	else
	{
		lTimerNext = GESTURE_TIMER_INTERVAL;
		lTimerPre = jiffies;
		tp_log_debug("%s:lTimerPre + lTimerNext = %ld",__func__,(lTimerPre + lTimerNext));
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f11_data_base_addr+F11_2D_DATA38_OFFSET,
			&device_data,
			sizeof(device_data));
	if (retval < 0)
		return 0;

	tp_log_vdebug("%s:f11_data_base_addr = %d,device_data = %d",
							__func__, rmi4_data->f11_data_base_addr,device_data);

	switch(device_data)
	{
		case DOUBLE_CLICK_WAKEUP:
			if(!(APP_ENABLE_GESTURE(GESTURE_DOUBLE_CLICK)&rmi4_data->easy_wakeup_gesture))
			{
				tp_log_debug("%s:@@@DOUBLE_CLICK_WAKEUP not enabled!!@@@",__func__);
				return 0;
			}
			tp_log_debug("%s:@@@DOUBLE_CLICK_WAKEUP detected!@@@",__func__);
			reprot_gesture_key_value = rmi4_data->board->wakeup_keys->keys[GESTURE_DOUBLE_CLICK];
			#if REPORT_GESTURE_LOCUS
			reprot_gesture_point_num = 0;
			#endif/*REPORT_GESTURE_LOCUS*/
			break;
		case LINEAR_SLIDE_DETECTED:
			if(!(APP_ENABLE_LINEAR&rmi4_data->easy_wakeup_gesture))
			{
				tp_log_debug("%s:@@@LINEAR_SLIDE not enabled!@@@",__func__);
				return 0;
			}
			tp_log_debug("%s:@@@LINEAR_SLIDE detected!@@@",__func__);
			retval = synaptics_rmi4_i2c_read(rmi4_data,
					rmi4_data->f51_data_base_addr+F51_CUSTOM_DATA24_OFFSET,
					&f51_custom_data24,
					sizeof(f51_custom_data24));
			if (retval < 0)
			{
				tp_log_err("%s:LINEAR_SLIDE read error!retval = %d",__func__,retval);
				return 0;
			}
			switch(f51_custom_data24)
			{
				case LINEAR_SLIDE_LEFT_TO_RIGHT:
					if(APP_ENABLE_GESTURE(GESTURE_SLIDE_L2R)&rmi4_data->easy_wakeup_gesture)
					{
						tp_log_debug("%s:@@@LINEAR_SLIDE_LEFT_TO_RIGHT detected!@@@",__func__);
						reprot_gesture_key_value = rmi4_data->board->wakeup_keys->keys[GESTURE_SLIDE_L2R];
						#if REPORT_GESTURE_LOCUS
						reprot_gesture_point_num = F51_LINEAR_LOCUS_NUM;
						#endif/*REPORT_GESTURE_LOCUS*/
					}
					break;
				case LINEAR_SLIDE_RIGHT_TO_LEFT:
					if(APP_ENABLE_GESTURE(GESTURE_SLIDE_R2L)&rmi4_data->easy_wakeup_gesture)
					{
						tp_log_debug("%s:@@@LINEAR_SLIDE_RIGHT_TO_LEFT detected!@@@",__func__);
						reprot_gesture_key_value = rmi4_data->board->wakeup_keys->keys[GESTURE_SLIDE_R2L];
						#if REPORT_GESTURE_LOCUS
						reprot_gesture_point_num = F51_LINEAR_LOCUS_NUM;
						#endif/*REPORT_GESTURE_LOCUS*/
					}
					break;
				case LINEAR_SLIDE_TOP_TO_BOTTOM:
					if(APP_ENABLE_GESTURE(GESTURE_SLIDE_T2B)&rmi4_data->easy_wakeup_gesture)
					{
						tp_log_debug("%s:@@@LINEAR_SLIDE_TOP_TO_BOTTOM detected!@@@",__func__);
						reprot_gesture_key_value = rmi4_data->board->wakeup_keys->keys[GESTURE_SLIDE_T2B];
						#if REPORT_GESTURE_LOCUS
						reprot_gesture_point_num = F51_LINEAR_LOCUS_NUM;
						#endif/*REPORT_GESTURE_LOCUS*/
					}
					break;
				case LINEAR_SLIDE_BOTTOM_TO_TOP:
					if(APP_ENABLE_GESTURE(GESTURE_SLIDE_B2T)&rmi4_data->easy_wakeup_gesture)
					{
						tp_log_debug("%s:@@@LINEAR_SLIDE_BOTTOM_TO_TOP detected!@@@",__func__);
						reprot_gesture_key_value = rmi4_data->board->wakeup_keys->keys[GESTURE_SLIDE_B2T];
						#if REPORT_GESTURE_LOCUS
						reprot_gesture_point_num = F51_LINEAR_LOCUS_NUM;
						#endif/*REPORT_GESTURE_LOCUS*/
					}
					break;
				case LINEAR_SLIDE_TOP_TO_BOTTOM2:
					if(APP_ENABLE_GESTURE(GESTURE_SLIDE_T2B2)&rmi4_data->easy_wakeup_gesture)
					{
						tp_log_debug("%s:@@@LINEAR_SLIDE_TOP_TO_BOTTOM2 detected!@@@",__func__);
						reprot_gesture_key_value = rmi4_data->board->wakeup_keys->keys[GESTURE_SLIDE_T2B2];
						#if REPORT_GESTURE_LOCUS
						reprot_gesture_point_num = F51_LINEAR2_LOCUS_NUM;
						#endif/*REPORT_GESTURE_LOCUS*/
					}
					break;
				default:
					tp_log_debug("%s:@@@unknow LINEAR!f51_custom_data24 = 0x%x@@@",__func__,f51_custom_data24);
					return 0;
			}
			break;
		case CIRCLE_SLIDE_DETECTED:
			if(!(APP_ENABLE_GESTURE(GESTURE_CIRCLE_SLIDE)&rmi4_data->easy_wakeup_gesture))
			{
				tp_log_debug("%s:@@@CIRCLE_SLIDE not enabled!@@@",__func__);
				return 0;
			}
			tp_log_debug("%s:@@@CIRCLE_SLIDE detected!@@@",__func__);
			reprot_gesture_key_value = rmi4_data->board->wakeup_keys->keys[GESTURE_CIRCLE_SLIDE];
			#if REPORT_GESTURE_LOCUS
			reprot_gesture_point_num = F51_LETTER_LOCUS_NUM;
			#endif/*REPORT_GESTURE_LOCUS*/
			break;
		case SPECIFIC_LETTER_DETECTED:
			if(!(APP_ENABLE_LETTERS&rmi4_data->easy_wakeup_gesture))
			{
				tp_log_debug("%s:@@@SPECIFIC_LETTER not enabled!@@@",__func__);
				return 0;
			}
			tp_log_debug("%s:@@@SPECIFIC_LETTER detected!@@@",__func__);
			retval = synaptics_rmi4_i2c_read(rmi4_data,
					rmi4_data->f11_data_base_addr+F11_2D_DATA39_OFFSET,
					&f11_2d_data39[0],
					sizeof(f11_2d_data39));
			if (retval < 0)
			{
				tp_log_err("%s:SPECIFIC_LETTER read error!retval = %d",__func__,retval);
				return 0;
			}

			tp_log_vdebug("%s:###read### f11_2d_data39[6] = 0x%x  ",__func__,f11_2d_data39[6]);

			switch(f11_2d_data39[6])
			{
				case SPECIFIC_LETTER_c:
					if(APP_ENABLE_GESTURE(GESTURE_LETTER_c)&rmi4_data->easy_wakeup_gesture)
					{
						tp_log_debug("%s:@@@SPECIFIC_LETTER_c detected!@@@",__func__);
						reprot_gesture_key_value = rmi4_data->board->wakeup_keys->keys[GESTURE_LETTER_c];
						#if REPORT_GESTURE_LOCUS
						reprot_gesture_point_num = F51_LETTER_LOCUS_NUM;
						#endif/*REPORT_GESTURE_LOCUS*/
					}
					break;
				case SPECIFIC_LETTER_e:
					if(APP_ENABLE_GESTURE(GESTURE_LETTER_e)&rmi4_data->easy_wakeup_gesture)
					{
						tp_log_debug("%s:@@@SPECIFIC_LETTER_e detected!@@@",__func__);
						reprot_gesture_key_value = rmi4_data->board->wakeup_keys->keys[GESTURE_LETTER_e];
						#if REPORT_GESTURE_LOCUS
						reprot_gesture_point_num = F51_LETTER_LOCUS_NUM;
						#endif/*REPORT_GESTURE_LOCUS*/
					}
					break;
				case SPECIFIC_LETTER_m:
					if(APP_ENABLE_GESTURE(GESTURE_LETTER_m)&rmi4_data->easy_wakeup_gesture)
					{
						tp_log_debug("%s:@@@SPECIFIC_LETTER_m detected!@@@",__func__);
						reprot_gesture_key_value = rmi4_data->board->wakeup_keys->keys[GESTURE_LETTER_m];
						#if REPORT_GESTURE_LOCUS
						reprot_gesture_point_num = F51_LETTER_LOCUS_NUM;
						#endif/*REPORT_GESTURE_LOCUS*/
					}
					break;
				case SPECIFIC_LETTER_w:
					if(APP_ENABLE_GESTURE(GESTURE_LETTER_w)&rmi4_data->easy_wakeup_gesture)
					{
						tp_log_debug("%s:@@@SPECIFIC_LETTER_w detected!@@@",__func__);
						reprot_gesture_key_value = rmi4_data->board->wakeup_keys->keys[GESTURE_LETTER_w];
						#if REPORT_GESTURE_LOCUS
						reprot_gesture_point_num = F51_LETTER_LOCUS_NUM;
						#endif/*REPORT_GESTURE_LOCUS*/
					}
					break;
				default:
					tp_log_debug("%s:@@@unknow letter!f11_2d_data39[6] = 0x%x@@@",__func__,f11_2d_data39[6]);
					return 0;
			}
			break;
		default:
			tp_log_debug("%s:@@@unknow gesture detected!",__func__);
			return 0;
	}
	
	if(0 != reprot_gesture_key_value)
	{
/*		input_report_key(rmi4_data->input_dev,KEY_POWER, 1);
		input_sync(rmi4_data->input_dev);
		input_report_key(rmi4_data->input_dev,KEY_POWER, 0);
		input_sync(rmi4_data->input_dev);*/
		
		tp_log_debug("%s:reprot_gesture_key_value = %d",__func__, reprot_gesture_key_value);
		input_report_key(rmi4_data->input_dev,reprot_gesture_key_value, 1);
		input_sync(rmi4_data->input_dev);
		input_report_key(rmi4_data->input_dev,reprot_gesture_key_value, 0);
		input_sync(rmi4_data->input_dev);

		#if REPORT_GESTURE_LOCUS
		retval = easy_wakeup_gesture_report_locus(rmi4_data,reprot_gesture_point_num);
		if (retval < 0)
		{
			tp_log_err("%s: report locus error!retval = %d",__func__,retval);
			return 0;
		}
		#endif/*REPORT_GESTURE_LOCUS*/
	}
	return 0;
}

static int synaptics_rmi4_f11_palm_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char device_data = 0;
	unsigned int reprot_gesture_key_value = 0;
	static int palm_is_covered = false;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f11_data_base_addr+F11_2D_DATA28_OFFSET,
			&device_data,
			sizeof(device_data));
	if (retval < 0)
		return 0;

	tp_log_vdebug("%s:f11_data_base_addr = %d,device_data = %d",
							__func__, rmi4_data->f11_data_base_addr,device_data);
	/*
	 * Palm cover sleep gesture
	 * 0:for no palm;
	 * 1:for has palm cover;
	 */
	if (device_data)
	{
		if(palm_is_covered == true)
		{
			tp_log_vdebug("%s:@@@GESTURE_PALM_COVERED do not report again!@@@",__func__);
			return 0;
		}
		
		if (APP_ENABLE_GESTURE(GESTURE_PALM_COVERED)&rmi4_data->easy_wakeup_gesture)
		{
			tp_log_debug("%s:@@@GESTURE_PALM_COVERED detected!@@@",__func__);
			reprot_gesture_key_value = rmi4_data->board->wakeup_keys->keys[GESTURE_PALM_COVERED];
			palm_is_covered = true;
			goto report_key;
		}
	}
	else
	{
		tp_log_vdebug("%s:@@@GESTURE_PALM_COVERED not detected!@@@",__func__);
		palm_is_covered = false;
		return 0;
	}

report_key:
/*	input_report_key(rmi4_data->input_dev,KEY_POWER, 1);
	input_sync(rmi4_data->input_dev);
	input_report_key(rmi4_data->input_dev,KEY_POWER, 0);
	input_sync(rmi4_data->input_dev);*/

	tp_log_debug("%s:reprot_gesture_key_value = %d",__func__, reprot_gesture_key_value);
	input_report_key(rmi4_data->input_dev,reprot_gesture_key_value, 1);
	input_sync(rmi4_data->input_dev);
	input_report_key(rmi4_data->input_dev,reprot_gesture_key_value, 0);
	input_sync(rmi4_data->input_dev);
	return 1;
}
#endif /*USE_WAKEUP_GESTURE*/
#endif /*CONFIG_HUAWEI_KERNEL*/

 /**
 * synaptics_rmi4_f11_abs_report()
 *
 * Called by synaptics_rmi4_report_touch() when valid Function $11
 * finger data has been detected.
 *
 * This function reads the Function $11 data registers, determines the
 * status of each finger supported by the Function, processes any
 * necessary coordinate manipulation, reports the finger data to
 * the input subsystem, and returns the number of fingers detected.
 */
static int synaptics_rmi4_f11_abs_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char touch_count = 0; /* number of touch points */
	unsigned char reg_index;
	unsigned char finger;
	unsigned char fingers_supported;
	unsigned char num_of_finger_status_regs;
	unsigned char finger_shift;
	unsigned char finger_status;
	unsigned char data_reg_blk_size;
	unsigned char finger_status_reg[3];
	unsigned char data[F11_STD_DATA_LEN];
	unsigned short data_addr;
	unsigned short data_offset;
	int x;
	int y;
	int z;
	int wx;
	int wy;
	int temp;
#ifdef CONFIG_HUAWEI_KERNEL
	int x_lcd,y_lcd_all;

	x_lcd = rmi4_data->board->lcd_x;
	y_lcd_all = rmi4_data->board->lcd_all;
#endif /*CONFIG_HUAWEI_KERNEL*/

	tp_log_vdebug("%s: in \n",__func__);
#if USE_WAKEUP_GESTURE
	
	if(false != rmi4_data->easy_wakeup_gesture)
	{
		if(true == rmi4_data->sleep_gesture_flag)//work only when suspended
		{
			tp_log_vdebug("%s:wake up gesture mode  in!sleep_gesture_flag = %d",__func__, rmi4_data->sleep_gesture_flag);
			return synaptics_rmi4_f11_gesture_report(rmi4_data,fhandler);
		}
		else if(true == rmi4_data->palm_enabled)//work only when resumed
		{
			tp_log_vdebug("%s:palm sleep gesture mode  in!",__func__);
			retval = synaptics_rmi4_f11_palm_report(rmi4_data,fhandler);
			if (1 == retval)
				return 0;
		}
		else
		{
			tp_log_vdebug("%s:easy wake up gesture not enabled!",__func__);
		}

	}
#endif /*USE_WAKEUP_GESTURE*/
	/*
	 * The number of finger status registers is determined by the
	 * maximum number of fingers supported - 2 bits per finger. So
	 * the number of finger status registers to read is:
	 * register_count = ceil(max_num_of_fingers / 4)
	 */
	fingers_supported = fhandler->num_of_data_points;
	num_of_finger_status_regs = (fingers_supported + 3) / 4;
	data_addr = fhandler->full_addr.data_base;
	data_reg_blk_size = fhandler->size_of_data_register_block;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_addr,
			finger_status_reg,
			num_of_finger_status_regs);
	if (retval < 0)
		return 0;
	tp_log_vdebug("%s(line%d): finger_status_reg = %d",__func__,__LINE__,finger_status_reg[0]);

	for (finger = 0; finger < fingers_supported; finger++) {
		reg_index = finger / 4;
		finger_shift = (finger % 4) * 2;
		finger_status = (finger_status_reg[reg_index] >> finger_shift)
				& MASK_2BIT;

		/*
		 * Each 2-bit finger status field represents the following:
		 * 00 = finger not present
		 * 01 = finger present and data accurate
		 * 10 = finger present but data may be inaccurate
		 * 11 = reserved
		 */
#ifdef TYPE_B_PROTOCOL
		input_mt_slot(rmi4_data->input_dev, finger);
		input_mt_report_slot_state(rmi4_data->input_dev,
				MT_TOOL_FINGER, finger_status);
#endif

		if (finger_status) {
			data_offset = data_addr +
					num_of_finger_status_regs +
					(finger * data_reg_blk_size);
			tp_log_vdebug("%s(line%d): retval = %d",__func__,__LINE__,retval);
			retval = synaptics_rmi4_i2c_read(rmi4_data,
					data_offset,
					data,
					data_reg_blk_size);
			if (retval < 0)
				return 0;

			x = (data[0] << 4) | (data[2] & MASK_4BIT);
			y = (data[1] << 4) | ((data[2] >> 4) & MASK_4BIT);
			wx = (data[3] & MASK_4BIT);
			wy = (data[3] >> 4) & MASK_4BIT;
			z = data[4];

			if (rmi4_data->board->swap_axes) {
				temp = x;
				x = y;
				y = temp;
				temp = wx;
				wx = wy;
				wy = temp;
			}

			if (rmi4_data->board->x_flip)
				x = rmi4_data->sensor_max_x - x;
			if (rmi4_data->board->y_flip)
				y = rmi4_data->sensor_max_y - y;

#ifdef CONFIG_HUAWEI_KERNEL
			tp_log_vdebug("%s: x_lcd = %d,y_lcd_all = %d  ",__func__, x_lcd,y_lcd_all);
			tp_log_vdebug("%s: max_x = %d,max_y = %d  ",__func__, rmi4_data->sensor_max_x,rmi4_data->sensor_max_y);
			x = x * x_lcd / rmi4_data->sensor_max_x;
			x = check_scope_x(x,x_lcd);
			y = y * y_lcd_all / rmi4_data->sensor_max_y;
#endif /*CONFIG_HUAWEI_KERNEL*/
			input_report_key(rmi4_data->input_dev,
					BTN_TOUCH, 1);
#if USE_BTN_TOOL_FINGER//huawei 11-25
			input_report_key(rmi4_data->input_dev,
					BTN_TOOL_FINGER, 1);
#endif//huawei 11-25
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_X, x);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_Y, y);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_PRESSURE, z);
#ifdef REPORT_2D_W
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MAJOR, max(wx, wy));
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MINOR, min(wx, wy));
#endif
#ifndef TYPE_B_PROTOCOL
			input_mt_sync(rmi4_data->input_dev);
#endif

			tp_log_vdebug("%s: Finger %d:\n"
					"status = 0x%02x\n"
					"x = %d\n"
					"y = %d\n"
					"z = %d\n"
					"wx = %d\n"
					"wy = %d\n",
					__func__, finger,
					finger_status,
					x, y,z, wx, wy);
			dev_dbg(&rmi4_data->i2c_client->dev,
					"%s: Finger %d:\n"
					"status = 0x%02x\n"
					"x = %d\n"
					"y = %d\n"
					"z = %d\n"
					"wx = %d\n"
					"wy = %d\n",
					__func__, finger,
					finger_status,
					x, y,z, wx, wy);

			touch_count++;
		}
	}

	if (touch_count == 0) {
		input_report_key(rmi4_data->input_dev,
				BTN_TOUCH, 0);
#if USE_BTN_TOOL_FINGER//huawei 11-25
		input_report_key(rmi4_data->input_dev,
				BTN_TOOL_FINGER, 0);
#endif//huawei 11-25
#ifndef TYPE_B_PROTOCOL
		tp_log_vdebug("%s:NO 2-6-38",__func__);
		input_mt_sync(rmi4_data->input_dev);
#endif
	}

	input_sync(rmi4_data->input_dev);

	tp_log_vdebug("%s: out ",__func__);
	return touch_count;
}

 /**
 * synaptics_rmi4_f12_abs_report()
 *
 * Called by synaptics_rmi4_report_touch() when valid Function $12
 * finger data has been detected.
 *
 * This function reads the Function $12 data registers, determines the
 * status of each finger supported by the Function, processes any
 * necessary coordinate manipulation, reports the finger data to
 * the input subsystem, and returns the number of fingers detected.
 */
static int synaptics_rmi4_f12_abs_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char touch_count = 0; /* number of touch points */
	unsigned char finger;
	unsigned char fingers_to_process;
	unsigned char finger_status;
	unsigned char size_of_2d_data;
	unsigned short data_addr;
	int x;
	int y;
	int wx;
	int wy;
	int temp;
	struct synaptics_rmi4_f12_extra_data *extra_data;
	struct synaptics_rmi4_f12_finger_data *data;
	struct synaptics_rmi4_f12_finger_data *finger_data;
#ifdef F12_DATA_15_WORKAROUND
	static unsigned char fingers_already_present;
#endif

	fingers_to_process = fhandler->num_of_data_points;
	data_addr = fhandler->full_addr.data_base;
	extra_data = (struct synaptics_rmi4_f12_extra_data *)fhandler->extra;
	size_of_2d_data = sizeof(struct synaptics_rmi4_f12_finger_data);


	/* Determine the total number of fingers to process */
	if (extra_data->data15_size) {
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				data_addr + extra_data->data15_offset,
				extra_data->data15_data,
				extra_data->data15_size);
		if (retval < 0)
			return 0;

		/* Start checking from the highest bit */
		temp = extra_data->data15_size - 1; /* Highest byte */
		finger = (fingers_to_process - 1) % 8; /* Highest bit */
		do {
			if (extra_data->data15_data[temp] & (1 << finger))
				break;

			if (finger) {
				finger--;
			} else {
				temp--; /* Move to the next lower byte */
				finger = 7;
			}

			fingers_to_process--;
		} while (fingers_to_process);

		dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: Number of fingers to process = %d\n",
			__func__, fingers_to_process);
	}

#ifdef F12_DATA_15_WORKAROUND
	fingers_to_process = max(fingers_to_process, fingers_already_present);
#endif

	if (!fingers_to_process) {
		synaptics_rmi4_free_fingers(rmi4_data);
		return 0;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_addr + extra_data->data1_offset,
			(unsigned char *)fhandler->data,
			fingers_to_process * size_of_2d_data);
	if (retval < 0)
		return 0;

	data = (struct synaptics_rmi4_f12_finger_data *)fhandler->data;

	for (finger = 0; finger < fingers_to_process; finger++) {
		finger_data = data + finger;
		finger_status = finger_data->object_type_and_status & MASK_1BIT;

#ifdef TYPE_B_PROTOCOL
		input_mt_slot(rmi4_data->input_dev, finger);
		input_mt_report_slot_state(rmi4_data->input_dev,
				MT_TOOL_FINGER, finger_status);
#endif

		if (finger_status) {
#ifdef F12_DATA_15_WORKAROUND
			fingers_already_present = finger + 1;
#endif

			x = (finger_data->x_msb << 8) | (finger_data->x_lsb);
			y = (finger_data->y_msb << 8) | (finger_data->y_lsb);
#ifdef REPORT_2D_W
			wx = finger_data->wx;
			wy = finger_data->wy;
#endif

			if (rmi4_data->board->swap_axes) {
				temp = x;
				x = y;
				y = temp;
				temp = wx;
				wx = wy;
				wy = temp;
			}

			if (rmi4_data->board->x_flip)
				x = rmi4_data->sensor_max_x - x;
			if (rmi4_data->board->y_flip)
				y = rmi4_data->sensor_max_y - y;

			input_report_key(rmi4_data->input_dev,
					BTN_TOUCH, 1);
#if USE_BTN_TOOL_FINGER//huawei 11-25
			input_report_key(rmi4_data->input_dev,
					BTN_TOOL_FINGER, 1);
#endif//huawei 11-25
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_X, x);
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_POSITION_Y, y);
#ifdef REPORT_2D_W
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MAJOR, max(wx, wy));
			input_report_abs(rmi4_data->input_dev,
					ABS_MT_TOUCH_MINOR, min(wx, wy));
#endif
#ifndef TYPE_B_PROTOCOL
			input_mt_sync(rmi4_data->input_dev);
#endif

			dev_dbg(&rmi4_data->i2c_client->dev,
					"%s: Finger %d:\n"
					"status = 0x%02x\n"
					"x = %d\n"
					"y = %d\n"
					"wx = %d\n"
					"wy = %d\n",
					__func__, finger,
					finger_status,
					x, y, wx, wy);

			touch_count++;
		}
	}

	if (touch_count == 0) {
		input_report_key(rmi4_data->input_dev,
				BTN_TOUCH, 0);
#if USE_BTN_TOOL_FINGER//huawei 11-25
		input_report_key(rmi4_data->input_dev,
				BTN_TOOL_FINGER, 0);
#endif//huawei 11-25
#ifndef TYPE_B_PROTOCOL
		input_mt_sync(rmi4_data->input_dev);
#endif
	}

	input_sync(rmi4_data->input_dev);

	return touch_count;
}

static void synaptics_rmi4_f1a_report(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char touch_count = 0;
	unsigned char button;
	unsigned char index;
	unsigned char shift;
	unsigned char status;
	unsigned char *data;
	unsigned short data_addr = fhandler->full_addr.data_base;
	struct synaptics_rmi4_f1a_handle *f1a = fhandler->data;
	static unsigned char do_once = 1;
	static bool current_status[MAX_NUMBER_OF_BUTTONS];
#ifdef NO_0D_WHILE_2D
	static bool before_2d_status[MAX_NUMBER_OF_BUTTONS];
	static bool while_2d_status[MAX_NUMBER_OF_BUTTONS];
#endif

	if (do_once) {
		memset(current_status, 0, sizeof(current_status));
#ifdef NO_0D_WHILE_2D
		memset(before_2d_status, 0, sizeof(before_2d_status));
		memset(while_2d_status, 0, sizeof(while_2d_status));
#endif
		do_once = 0;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			data_addr,
			f1a->button_data_buffer,
			f1a->button_bitmask_size);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to read button data registers\n",
				__func__);
		return;
	}

	data = f1a->button_data_buffer;

	for (button = 0; button < f1a->valid_button_count; button++) {
		index = button / 8;
		shift = button % 8;
		status = ((data[index] >> shift) & MASK_1BIT);

		if (current_status[button] == status)
			continue;
		else
			current_status[button] = status;

		dev_dbg(&rmi4_data->i2c_client->dev,
				"%s: Button %d (code %d) ->%d\n",
				__func__, button,
				f1a->button_map[button],
				status);
#ifdef NO_0D_WHILE_2D
		if (rmi4_data->fingers_on_2d == false) {
			if (status == 1) {
				before_2d_status[button] = 1;
			} else {
				if (while_2d_status[button] == 1) {
					while_2d_status[button] = 0;
					continue;
				} else {
					before_2d_status[button] = 0;
				}
			}
			touch_count++;
			input_report_key(rmi4_data->input_dev,
					f1a->button_map[button],
					status);
		} else {
			if (before_2d_status[button] == 1) {
				before_2d_status[button] = 0;
				touch_count++;
				input_report_key(rmi4_data->input_dev,
						f1a->button_map[button],
						status);
			} else {
				if (status == 1)
					while_2d_status[button] = 1;
				else
					while_2d_status[button] = 0;
			}
		}
#else
		touch_count++;
		input_report_key(rmi4_data->input_dev,
				f1a->button_map[button],
				status);
#endif
	}

	if (touch_count)
		input_sync(rmi4_data->input_dev);

	return;
}

 /**
 * synaptics_rmi4_report_touch()
 *
 * Called by synaptics_rmi4_sensor_report().
 *
 * This function calls the appropriate finger data reporting function
 * based on the function handler it receives and returns the number of
 * fingers detected.
 */
static void synaptics_rmi4_report_touch(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	unsigned char touch_count_2d;

	dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: Function %02x reporting\n",
			__func__, fhandler->fn_number);

	switch (fhandler->fn_number) {
	case SYNAPTICS_RMI4_F11:
		touch_count_2d = synaptics_rmi4_f11_abs_report(rmi4_data,
				fhandler);

		if (touch_count_2d)
			rmi4_data->fingers_on_2d = true;
		else
			rmi4_data->fingers_on_2d = false;
		break;
	case SYNAPTICS_RMI4_F12:
		touch_count_2d = synaptics_rmi4_f12_abs_report(rmi4_data,
				fhandler);

		if (touch_count_2d)
			rmi4_data->fingers_on_2d = true;
		else
			rmi4_data->fingers_on_2d = false;
		break;
	case SYNAPTICS_RMI4_F1A:
		synaptics_rmi4_f1a_report(rmi4_data, fhandler);
		break;
	default:
		break;
	}

	return;
}

 /**
 * synaptics_rmi4_sensor_report()
 *
 * Called by synaptics_rmi4_irq().
 *
 * This function determines the interrupt source(s) from the sensor
 * and calls synaptics_rmi4_report_touch() with the appropriate
 * function handler for each function with valid data inputs.
 */
#if USE_IRQ_THREAD //huawei 11-25
static void synaptics_rmi4_sensor_report(struct synaptics_rmi4_data *rmi4_data)
#else //huawei 11-25
static void synaptics_rmi4_sensor_report(struct work_struct *work)
#endif //huawei 11-25
{
	int retval;
	unsigned char data[MAX_INTR_REGISTERS + 1];
	unsigned char *intr = &data[1];
	struct synaptics_rmi4_f01_device_status status;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_exp_fhandler *exp_fhandler;
	struct synaptics_rmi4_device_info *rmi;
#if USE_IRQ_THREAD //huawei 11-25
#else //huawei 11-25
	struct synaptics_rmi4_data *rmi4_data = NULL;
	rmi4_data = container_of(work,struct synaptics_rmi4_data, work);
	tp_log_vdebug("%s:### in! ",__func__);
#endif //huawei 11-25

	rmi = &(rmi4_data->rmi4_mod_info);

	/*
	 * Get interrupt status information from F01 Data1 register to
	 * determine the source(s) that are flagging the interrupt.
	 */
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr,
			data,
			rmi4_data->num_of_intr_regs + 1);
	tp_log_vdebug("%s: data[1] = %d ,retval = %d \n",__func__,data[1],retval);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to read interrupt status\n",
				__func__);
		return;
	}

	tp_log_vdebug("%s: data[1] = %d ,retval = %d",__func__,data[1],retval);
	status.data[0] = data[0];
	if (status.unconfigured && !status.flash_prog) {
		pr_notice("%s: spontaneous reset detected\n", __func__);
		retval = synaptics_rmi4_reinit_device(rmi4_data);
		if (retval < 0) {
			dev_err(&rmi4_data->i2c_client->dev,
					"%s: Failed to reinit device\n",
					__func__);
		}
		return;
	}

	/*
	 * Traverse the function handler list and service the source(s)
	 * of the interrupt accordingly.
	 */
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->num_of_data_sources) {
				if (fhandler->intr_mask &
						intr[fhandler->intr_reg_num]) {
					synaptics_rmi4_report_touch(rmi4_data,
							fhandler);
				}
			}
		}
	}

	mutex_lock(&exp_data.mutex);
	if (!list_empty(&exp_data.list)) {
		list_for_each_entry(exp_fhandler, &exp_data.list, link) {
			if (!exp_fhandler->insert &&
					!exp_fhandler->remove &&
					(exp_fhandler->exp_fn->attn != NULL))
				exp_fhandler->exp_fn->attn(rmi4_data, intr[0]);
		}
	}
	mutex_unlock(&exp_data.mutex);

#if USE_IRQ_THREAD //huawei 11-25
#else //huawei 11-25
	if (rmi4_data->irq_enabled)
	{
		tp_log_vdebug("%s:### irq_enabled! ",__func__);
		enable_irq(rmi4_data->irq);
	}
#endif //huawei 11-25
	
	return;
}

 /**
 * synaptics_rmi4_irq()
 *
 * Called by the kernel when an interrupt occurs (when the sensor
 * asserts the attention irq).
 *
 * This function is the ISR thread and handles the acquisition
 * and the reporting of finger data when the presence of fingers
 * is detected.
 */
static irqreturn_t synaptics_rmi4_irq(int irq, void *data)
{
	struct synaptics_rmi4_data *rmi4_data = data;

	tp_log_vdebug("%s: in!",__func__);
	tp_log_vdebug("%s(line%d): touch_stopped=%d\n",__func__,__LINE__,rmi4_data->touch_stopped);
	if (!rmi4_data->touch_stopped)
#if USE_IRQ_THREAD //huawei 11-25
		synaptics_rmi4_sensor_report(rmi4_data);
#else //huawei 11-25
		disable_irq_nosync(irq);
		queue_work(synaptics_wq, &rmi4_data->work);
#endif //huawei 11-25
	

	return IRQ_HANDLED;
}

 /**
 * synaptics_rmi4_irq_enable()
 *
 * Called by synaptics_rmi4_probe() and the power management functions
 * in this driver and also exported to other expansion Function modules
 * such as rmi_dev.
 *
 * This function handles the enabling and disabling of the attention
 * irq including the setting up of the ISR thread.
 */
static int synaptics_rmi4_irq_enable(struct synaptics_rmi4_data *rmi4_data,
		bool enable)
{
	int retval = 0;
	unsigned char intr_status[MAX_INTR_REGISTERS];
	//const struct synaptics_dsx_platform_data *platform_data =
	//		rmi4_data->i2c_client->dev.platform_data;

	const struct synaptics_dsx_platform_data *platform_data =
			rmi4_data->board;
	tp_log_debug("%s: enable = %d ",__func__,enable);
	tp_log_debug("%s: irq_enabled = %d \n",__func__,rmi4_data->irq_enabled);
	if (enable) {
		if (rmi4_data->irq_enabled)
			return retval;

		/* Clear interrupts first */
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				rmi4_data->f01_data_base_addr + 1,
				intr_status,
				rmi4_data->num_of_intr_regs);
		tp_log_debug("%s: intr_status[0] = %d ,retval = %d \n",__func__,intr_status[0],retval);
		if (retval < 0)
			return retval;
		tp_log_debug("%s:before gpio17= %d\n",__func__,gpio_get_value(17));

		retval = request_threaded_irq(rmi4_data->irq, NULL,
				synaptics_rmi4_irq, platform_data->irq_flags,
				DRIVER_NAME, rmi4_data);
		tp_log_debug("%s: request_threaded_irq retval= %d ",__func__,retval);
		if (retval < 0) {
			dev_err(&rmi4_data->i2c_client->dev,
					"%s: Failed to create irq thread\n",
					__func__);
			return retval;
		}

		tp_log_debug("%s:after gpio17= %d\n",__func__,gpio_get_value(17));

		/* Clear interrupts first */
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				rmi4_data->f01_data_base_addr + 1,
				intr_status,
				rmi4_data->num_of_intr_regs);
		tp_log_debug("%s: intr_status[0] = %d ,retval = %d \n",__func__,intr_status[0],retval);
		if (retval < 0)
			return retval;

		rmi4_data->irq_enabled = true;
	} else {
		tp_log_debug("%s:before gpio17= %d\n",__func__,gpio_get_value(17));

		if (rmi4_data->irq_enabled) {
			disable_irq(rmi4_data->irq);
			free_irq(rmi4_data->irq, rmi4_data);
			rmi4_data->irq_enabled = false;
			/* Clear interrupts first */
			retval = synaptics_rmi4_i2c_read(rmi4_data,
					rmi4_data->f01_data_base_addr + 1,
					intr_status,
					rmi4_data->num_of_intr_regs);
			tp_log_debug("%s: intr_status[0] = %d ,retval = %d \n",__func__,intr_status[0],retval);
			if (retval < 0)
				return retval;
		}
		tp_log_debug("%s:after gpio17= %d\n",__func__,gpio_get_value(17));

	}

	return retval;
}

static void synaptics_rmi4_set_intr_mask(struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	unsigned char ii;
	unsigned char intr_offset;

	fhandler->intr_reg_num = (intr_count + 7) / 8;
	if (fhandler->intr_reg_num != 0)
		fhandler->intr_reg_num -= 1;

	/* Set an enable bit for each data source */
	intr_offset = intr_count % 8;
	fhandler->intr_mask = 0;
	for (ii = intr_offset;
			ii < ((fd->intr_src_count & MASK_3BIT) +
			intr_offset);
			ii++)
		fhandler->intr_mask |= 1 << ii;

	return;
}

static int synaptics_rmi4_f01_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;
	fhandler->data = NULL;
	fhandler->extra = NULL;

	synaptics_rmi4_set_intr_mask(fhandler, fd, intr_count);

	rmi4_data->f01_query_base_addr = fd->query_base_addr;
	rmi4_data->f01_ctrl_base_addr = fd->ctrl_base_addr;
	rmi4_data->f01_data_base_addr = fd->data_base_addr;
	rmi4_data->f01_cmd_base_addr = fd->cmd_base_addr;

	return 0;
}

 /**
 * synaptics_rmi4_f11_init()
 *
 * Called by synaptics_rmi4_query_device().
 *
 * This funtion parses information from the Function 11 registers
 * and determines the number of fingers supported, x and y data ranges,
 * offset to the associated interrupt status register, interrupt bit
 * mask, and gathers finger data acquisition capabilities from the query
 * registers.
 */
static int synaptics_rmi4_f11_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char abs_data_size;
	unsigned char abs_data_blk_size;
	unsigned char query[F11_STD_QUERY_LEN];
	unsigned char control[F11_STD_CTRL_LEN];

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			query,
			sizeof(query));
	if (retval < 0)
		return retval;

	/* Maximum number of fingers supported */
	if ((query[1] & MASK_3BIT) <= 4)
		fhandler->num_of_data_points = (query[1] & MASK_3BIT) + 1;
	else if ((query[1] & MASK_3BIT) == 5)
		fhandler->num_of_data_points = 10;

	rmi4_data->num_of_fingers = fhandler->num_of_data_points;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base,
			control,
			sizeof(control));
	if (retval < 0)
		return retval;

	/* Maximum x and y */
	rmi4_data->sensor_max_x = ((control[6] & MASK_8BIT) << 0) |
			((control[7] & MASK_4BIT) << 8);
	rmi4_data->sensor_max_y = ((control[8] & MASK_8BIT) << 0) |
			((control[9] & MASK_4BIT) << 8);
	dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: Function %02x max x = %d max y = %d\n",
			__func__, fhandler->fn_number,
			rmi4_data->sensor_max_x,
			rmi4_data->sensor_max_y);

	rmi4_data->max_touch_width = MAX_F11_TOUCH_WIDTH;

	synaptics_rmi4_set_intr_mask(fhandler, fd, intr_count);

	abs_data_size = query[5] & MASK_2BIT;
	abs_data_blk_size = 3 + (2 * (abs_data_size == 0 ? 1 : 0));
	fhandler->size_of_data_register_block = abs_data_blk_size;
	fhandler->data = NULL;
	fhandler->extra = NULL;

	rmi4_data->f11_query_base_addr = fd->query_base_addr;
	rmi4_data->f11_ctrl_base_addr = fd->ctrl_base_addr;
	rmi4_data->f11_data_base_addr = fd->data_base_addr;
	rmi4_data->f11_cmd_base_addr = fd->cmd_base_addr;
	return retval;
}

static int synaptics_rmi4_f12_set_enables(struct synaptics_rmi4_data *rmi4_data,
		unsigned short ctrl28)
{
	int retval;
	static unsigned short ctrl_28_address;

	if (ctrl28)
		ctrl_28_address = ctrl28;

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			ctrl_28_address,
			&rmi4_data->report_enable,
			sizeof(rmi4_data->report_enable));
	if (retval < 0)
		return retval;

	return retval;
}

 /**
 * synaptics_rmi4_f12_init()
 *
 * Called by synaptics_rmi4_query_device().
 *
 * This funtion parses information from the Function 12 registers and
 * determines the number of fingers supported, offset to the data1
 * register, x and y data ranges, offset to the associated interrupt
 * status register, interrupt bit mask, and allocates memory resources
 * for finger data acquisition.
 */
static int synaptics_rmi4_f12_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;
	unsigned char size_of_2d_data;
	unsigned char size_of_query8;
	unsigned char ctrl_8_offset;
	unsigned char ctrl_23_offset;
	unsigned char ctrl_28_offset;
	unsigned char num_of_fingers;
	struct synaptics_rmi4_f12_extra_data *extra_data;
	struct synaptics_rmi4_f12_query_5 query_5;
	struct synaptics_rmi4_f12_query_8 query_8;
	struct synaptics_rmi4_f12_ctrl_8 ctrl_8;
	struct synaptics_rmi4_f12_ctrl_23 ctrl_23;

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;
	fhandler->extra = kmalloc(sizeof(*extra_data), GFP_KERNEL);
	extra_data = (struct synaptics_rmi4_f12_extra_data *)fhandler->extra;
	size_of_2d_data = sizeof(struct synaptics_rmi4_f12_finger_data);

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base + 5,
			query_5.data,
			sizeof(query_5.data));
	if (retval < 0)
		return retval;

	ctrl_8_offset = query_5.ctrl0_is_present +
			query_5.ctrl1_is_present +
			query_5.ctrl2_is_present +
			query_5.ctrl3_is_present +
			query_5.ctrl4_is_present +
			query_5.ctrl5_is_present +
			query_5.ctrl6_is_present +
			query_5.ctrl7_is_present;

	ctrl_23_offset = ctrl_8_offset +
			query_5.ctrl8_is_present +
			query_5.ctrl9_is_present +
			query_5.ctrl10_is_present +
			query_5.ctrl11_is_present +
			query_5.ctrl12_is_present +
			query_5.ctrl13_is_present +
			query_5.ctrl14_is_present +
			query_5.ctrl15_is_present +
			query_5.ctrl16_is_present +
			query_5.ctrl17_is_present +
			query_5.ctrl18_is_present +
			query_5.ctrl19_is_present +
			query_5.ctrl20_is_present +
			query_5.ctrl21_is_present +
			query_5.ctrl22_is_present;

	ctrl_28_offset = ctrl_23_offset +
			query_5.ctrl23_is_present +
			query_5.ctrl24_is_present +
			query_5.ctrl25_is_present +
			query_5.ctrl26_is_present +
			query_5.ctrl27_is_present;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base + ctrl_23_offset,
			ctrl_23.data,
			sizeof(ctrl_23.data));
	if (retval < 0)
		return retval;

	/* Maximum number of fingers supported */
	fhandler->num_of_data_points = min(ctrl_23.max_reported_objects,
			(unsigned char)F12_FINGERS_TO_SUPPORT);

	num_of_fingers = fhandler->num_of_data_points;
	rmi4_data->num_of_fingers = num_of_fingers;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base + 7,
			&size_of_query8,
			sizeof(size_of_query8));
	if (retval < 0)
		return retval;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base + 8,
			query_8.data,
			size_of_query8);
	if (retval < 0)
		return retval;

	/* Determine the presence of the Data0 register */
	extra_data->data1_offset = query_8.data0_is_present;

	if ((size_of_query8 >= 3) && (query_8.data15_is_present)) {
		extra_data->data15_offset = query_8.data0_is_present +
				query_8.data1_is_present +
				query_8.data2_is_present +
				query_8.data3_is_present +
				query_8.data4_is_present +
				query_8.data5_is_present +
				query_8.data6_is_present +
				query_8.data7_is_present +
				query_8.data8_is_present +
				query_8.data9_is_present +
				query_8.data10_is_present +
				query_8.data11_is_present +
				query_8.data12_is_present +
				query_8.data13_is_present +
				query_8.data14_is_present;
		extra_data->data15_size = (num_of_fingers + 7) / 8;
	} else {
		extra_data->data15_size = 0;
	}

	rmi4_data->report_enable = RPT_DEFAULT;
#ifdef REPORT_2D_Z
	rmi4_data->report_enable |= RPT_Z;
#endif
#ifdef REPORT_2D_W
	rmi4_data->report_enable |= (RPT_WX | RPT_WY);
#endif

	retval = synaptics_rmi4_f12_set_enables(rmi4_data,
			fhandler->full_addr.ctrl_base + ctrl_28_offset);
	if (retval < 0)
		return retval;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.ctrl_base + ctrl_8_offset,
			ctrl_8.data,
			sizeof(ctrl_8.data));
	if (retval < 0)
		return retval;

	/* Maximum x and y */
	rmi4_data->sensor_max_x =
			((unsigned short)ctrl_8.max_x_coord_lsb << 0) |
			((unsigned short)ctrl_8.max_x_coord_msb << 8);
	rmi4_data->sensor_max_y =
			((unsigned short)ctrl_8.max_y_coord_lsb << 0) |
			((unsigned short)ctrl_8.max_y_coord_msb << 8);
	dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: Function %02x max x = %d max y = %d\n",
			__func__, fhandler->fn_number,
			rmi4_data->sensor_max_x,
			rmi4_data->sensor_max_y);

	rmi4_data->num_of_rx = ctrl_8.num_of_rx;
	rmi4_data->num_of_tx = ctrl_8.num_of_tx;
	rmi4_data->max_touch_width = max(rmi4_data->num_of_rx,
			rmi4_data->num_of_tx);

	synaptics_rmi4_set_intr_mask(fhandler, fd, intr_count);

	/* Allocate memory for finger data storage space */
	fhandler->data_size = num_of_fingers * size_of_2d_data;
	fhandler->data = kmalloc(fhandler->data_size, GFP_KERNEL);

	return retval;
}

static int synaptics_rmi4_f1a_alloc_mem(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	struct synaptics_rmi4_f1a_handle *f1a;

	f1a = kzalloc(sizeof(*f1a), GFP_KERNEL);
	if (!f1a) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for function handle\n",
				__func__);
		return -ENOMEM;
	}

	fhandler->data = (void *)f1a;
	fhandler->extra = NULL;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			fhandler->full_addr.query_base,
			f1a->button_query.data,
			sizeof(f1a->button_query.data));
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to read query registers\n",
				__func__);
		return retval;
	}

	f1a->max_count = f1a->button_query.max_button_count + 1;

	f1a->button_control.txrx_map = kzalloc(f1a->max_count * 2, GFP_KERNEL);
	if (!f1a->button_control.txrx_map) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for tx rx mapping\n",
				__func__);
		return -ENOMEM;
	}

	f1a->button_bitmask_size = (f1a->max_count + 7) / 8;

	f1a->button_data_buffer = kcalloc(f1a->button_bitmask_size,
			sizeof(*(f1a->button_data_buffer)), GFP_KERNEL);
	if (!f1a->button_data_buffer) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for data buffer\n",
				__func__);
		return -ENOMEM;
	}

	f1a->button_map = kcalloc(f1a->max_count,
			sizeof(*(f1a->button_map)), GFP_KERNEL);
	if (!f1a->button_map) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to alloc mem for button map\n",
				__func__);
		return -ENOMEM;
	}

	return 0;
}

static int synaptics_rmi4_f1a_button_map(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler)
{
	int retval;
	unsigned char ii;
	unsigned char mapping_offset = 0;
	struct synaptics_rmi4_f1a_handle *f1a = fhandler->data;
	const struct synaptics_dsx_platform_data *pdata = rmi4_data->board;

	mapping_offset = f1a->button_query.has_general_control +
			f1a->button_query.has_interrupt_enable +
			f1a->button_query.has_multibutton_select;

	if (f1a->button_query.has_tx_rx_map) {
		retval = synaptics_rmi4_i2c_read(rmi4_data,
				fhandler->full_addr.ctrl_base + mapping_offset,
				f1a->button_control.txrx_map,
				sizeof(f1a->button_control.txrx_map));
		if (retval < 0) {
			dev_err(&rmi4_data->i2c_client->dev,
					"%s: Failed to read tx rx mapping\n",
					__func__);
			return retval;
		}

		rmi4_data->button_txrx_mapping = f1a->button_control.txrx_map;
	}

	if (!pdata->cap_button_map) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: cap_button_map is NULL in board file\n",
				__func__);
		//return -ENODEV;
	} else if (!pdata->cap_button_map->map) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Button map is missing in board file\n",
				__func__);
		//return -ENODEV;
	} else {
		if (pdata->cap_button_map->nbuttons != f1a->max_count) {
			f1a->valid_button_count = min(f1a->max_count,
					pdata->cap_button_map->nbuttons);
		} else {
			f1a->valid_button_count = f1a->max_count;
		}

		for (ii = 0; ii < f1a->valid_button_count; ii++)
			f1a->button_map[ii] = pdata->cap_button_map->map[ii];
	}

	return 0;
}

static void synaptics_rmi4_f1a_kfree(struct synaptics_rmi4_fn *fhandler)
{
	struct synaptics_rmi4_f1a_handle *f1a = fhandler->data;

	if (f1a) {
		kfree(f1a->button_control.txrx_map);
		kfree(f1a->button_data_buffer);
		kfree(f1a->button_map);
		kfree(f1a);
		fhandler->data = NULL;
	}

	return;
}

static int synaptics_rmi4_f1a_init(struct synaptics_rmi4_data *rmi4_data,
		struct synaptics_rmi4_fn *fhandler,
		struct synaptics_rmi4_fn_desc *fd,
		unsigned int intr_count)
{
	int retval;

	fhandler->fn_number = fd->fn_number;
	fhandler->num_of_data_sources = fd->intr_src_count;

	synaptics_rmi4_set_intr_mask(fhandler, fd, intr_count);

	retval = synaptics_rmi4_f1a_alloc_mem(rmi4_data, fhandler);
	if (retval < 0)
		goto error_exit;

	retval = synaptics_rmi4_f1a_button_map(rmi4_data, fhandler);
	if (retval < 0)
		goto error_exit;

	rmi4_data->button_0d_enabled = 1;

	return 0;

error_exit:
	synaptics_rmi4_f1a_kfree(fhandler);

	return retval;
}

static void synaptics_rmi4_empty_fn_list(struct synaptics_rmi4_data *rmi4_data)
{
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_fn *fhandler_temp;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry_safe(fhandler,
				fhandler_temp,
				&rmi->support_fn_list,
				link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A) {
				synaptics_rmi4_f1a_kfree(fhandler);
			} else {
				kfree(fhandler->extra);
				kfree(fhandler->data);
			}
			list_del(&fhandler->link);
			kfree(fhandler);
		}
	}
	INIT_LIST_HEAD(&rmi->support_fn_list);

	return;
}

static int synaptics_rmi4_check_status(struct synaptics_rmi4_data *rmi4_data,
		bool *was_in_bl_mode)
{
	int retval;
	int timeout = CHECK_STATUS_TIMEOUT_MS;
	unsigned char command = 0x01;
	unsigned char intr_status;
	struct synaptics_rmi4_f01_device_status status;

	/* Do a device reset first */
	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_cmd_base_addr,
			&command,
			sizeof(command));
	if (retval < 0)
		return retval;

	msleep(rmi4_data->board->reset_delay_ms);

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr,
			status.data,
			sizeof(status.data));
	if (retval < 0)
		return retval;

	while (status.status_code == STATUS_CRC_IN_PROGRESS) {
		if (timeout > 0)
			msleep(20);
		else
			return -1;

		retval = synaptics_rmi4_i2c_read(rmi4_data,
				rmi4_data->f01_data_base_addr,
				status.data,
				sizeof(status.data));
		if (retval < 0)
			return retval;

		timeout -= 20;
	}

	if (timeout != CHECK_STATUS_TIMEOUT_MS)
		*was_in_bl_mode = true;

	if (status.flash_prog == 1) {
		rmi4_data->flash_prog_mode = true;
		pr_notice("%s: In flash prog mode, status = 0x%02x\n",
				__func__,
				status.status_code);
	} else {
		rmi4_data->flash_prog_mode = false;
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr + 1,
			&intr_status,
			sizeof(intr_status));
	tp_log_debug("%s: intr_status = %d ,retval = %d \n",__func__,intr_status,retval);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to read interrupt status\n",
				__func__);
		return retval;
	}

	return 0;
}

static void synaptics_rmi4_set_configured(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char device_ctrl;

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to set configured\n",
				__func__);
		return;
	}

	rmi4_data->no_sleep_setting = device_ctrl & NO_SLEEP_ON;
	device_ctrl |= CONFIGURED;

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to set configured\n",
				__func__);
	}

	return;
}

static int synaptics_rmi4_alloc_fh(struct synaptics_rmi4_fn **fhandler,
		struct synaptics_rmi4_fn_desc *rmi_fd, int page_number)
{
	*fhandler = kmalloc(sizeof(**fhandler), GFP_KERNEL);
	if (!(*fhandler))
		return -ENOMEM;

	(*fhandler)->full_addr.data_base =
			(rmi_fd->data_base_addr |
			(page_number << 8));
	(*fhandler)->full_addr.ctrl_base =
			(rmi_fd->ctrl_base_addr |
			(page_number << 8));
	(*fhandler)->full_addr.cmd_base =
			(rmi_fd->cmd_base_addr |
			(page_number << 8));
	(*fhandler)->full_addr.query_base =
			(rmi_fd->query_base_addr |
			(page_number << 8));

	return 0;
}

 static int synaptics_rmi4_f51_init(struct synaptics_rmi4_data *rmi4_data,
		 struct synaptics_rmi4_fn_desc *fd,
		 unsigned char page)
 {
	 rmi4_data->f51_query_base_addr = fd->query_base_addr | (page << 8);
	 rmi4_data->f51_ctrl_base_addr = fd->ctrl_base_addr | (page << 8);
	 rmi4_data->f51_data_base_addr = fd->data_base_addr | (page << 8);
	 rmi4_data->f51_cmd_base_addr = fd->cmd_base_addr | (page << 8);
	 tp_log_debug("%s(line%d): rmi4_data->f51_query_base_addr = 0x%x",__func__,__LINE__,rmi4_data->f51_query_base_addr);
	 tp_log_debug("%s(line%d): rmi4_data->f51_ctrl_base_addr = 0x%x",__func__,__LINE__,rmi4_data->f51_ctrl_base_addr);
	 tp_log_debug("%s(line%d): rmi4_data->f51_data_base_addr = 0x%x",__func__,__LINE__,rmi4_data->f51_data_base_addr);
	 tp_log_debug("%s(line%d): rmi4_data->f51_cmd_base_addr = 0x%x",__func__,__LINE__,rmi4_data->f51_cmd_base_addr);
 
	 return 0;
 }
 /**
 * synaptics_rmi4_query_device()
 *
 * Called by synaptics_rmi4_probe().
 *
 * This funtion scans the page description table, records the offsets
 * to the register types of Function $01, sets up the function handlers
 * for Function $11 and Function $12, determines the number of interrupt
 * sources from the sensor, adds valid Functions with data inputs to the
 * Function linked list, parses information from the query registers of
 * Function $01, and enables the interrupt sources from the valid Functions
 * with data inputs.
 */
static int synaptics_rmi4_query_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char ii;
	unsigned char page_number;
	unsigned char intr_count;
	unsigned char f01_query[F01_STD_QUERY_LEN];
	unsigned short pdt_entry_addr;
	unsigned short intr_addr;
	bool was_in_bl_mode;
	struct synaptics_rmi4_fn_desc rmi_fd;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

rescan_pdt:
	was_in_bl_mode = false;
	intr_count = 0;
	INIT_LIST_HEAD(&rmi->support_fn_list);

	/* Scan the page description tables of the pages to service */
	for (page_number = 0; page_number < PAGES_TO_SERVICE; page_number++) {
		for (pdt_entry_addr = PDT_START; pdt_entry_addr > PDT_END;
				pdt_entry_addr -= PDT_ENTRY_SIZE) {
			pdt_entry_addr |= (page_number << 8);

			tp_log_debug("%s(line%d): pdt_entry_addr = 0x%x",__func__,__LINE__,pdt_entry_addr);
			tp_log_debug("%s(line%d): page_number = 0x%x",__func__,__LINE__,page_number);
			retval = synaptics_rmi4_i2c_read(rmi4_data,
					pdt_entry_addr,
					(unsigned char *)&rmi_fd,
					sizeof(rmi_fd));
			tp_log_debug("%s(line%d): rmi_fd.fn_number = 0x%x",__func__,__LINE__,rmi_fd.fn_number);
			if (retval < 0)
				return retval;

			fhandler = NULL;

			if (rmi_fd.fn_number == 0) {
				dev_dbg(&rmi4_data->i2c_client->dev,
						"%s: Reached end of PDT\n",
						__func__);
				break;
			}

			dev_dbg(&rmi4_data->i2c_client->dev,
					"%s: F%02x found (page %d)\n",
					__func__, rmi_fd.fn_number,
					page_number);

			switch (rmi_fd.fn_number) {
			case SYNAPTICS_RMI4_F01:
				tp_log_debug("%s(line%d): SYNAPTICS_RMI4_F01",__func__,__LINE__);
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					dev_err(&rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				retval = synaptics_rmi4_f01_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0)
					return retval;

				retval = synaptics_rmi4_check_status(rmi4_data,
						&was_in_bl_mode);
				if (retval < 0) {
					dev_err(&rmi4_data->i2c_client->dev,
							"%s: Failed to check status\n",
							__func__);
					return retval;
				}

				if (was_in_bl_mode) {
					kfree(fhandler);
					fhandler = NULL;
					goto rescan_pdt;
				}

				if (rmi4_data->flash_prog_mode)
					goto flash_prog_mode;

				break;
			case SYNAPTICS_RMI4_F11:
				tp_log_debug("%s(line%d): SYNAPTICS_RMI4_F11",__func__,__LINE__);
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					dev_err(&rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				retval = synaptics_rmi4_f11_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0)
					return retval;
				break;
			case SYNAPTICS_RMI4_F12:
				tp_log_debug("%s(line%d): SYNAPTICS_RMI4_F12",__func__,__LINE__);
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					dev_err(&rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				retval = synaptics_rmi4_f12_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0)
					return retval;
				break;
			case SYNAPTICS_RMI4_F1A:
				tp_log_debug("%s(line%d): SYNAPTICS_RMI4_F1A",__func__,__LINE__);
				if (rmi_fd.intr_src_count == 0)
					break;

				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					dev_err(&rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}

				retval = synaptics_rmi4_f1a_init(rmi4_data,
						fhandler, &rmi_fd, intr_count);
				if (retval < 0) {
#ifdef IGNORE_FN_INIT_FAILURE
					kfree(fhandler);
					fhandler = NULL;
#else
					return retval;
#endif
				}
				break;
			case SYNAPTICS_RMI4_F51:
				tp_log_debug("%s(line%d): SYNAPTICS_RMI4_F51",__func__,__LINE__);
				tp_log_debug("%s(line%d): rmi_fd.intr_src_count =0x%x",__func__,__LINE__,rmi_fd.intr_src_count);
			
				retval = synaptics_rmi4_alloc_fh(&fhandler,
						&rmi_fd, page_number);
				if (retval < 0) {
					dev_err(&rmi4_data->i2c_client->dev,
							"%s: Failed to alloc for F%d\n",
							__func__,
							rmi_fd.fn_number);
					return retval;
				}
			
				retval = synaptics_rmi4_f51_init(rmi4_data,
						&rmi_fd, page_number);
				if (retval < 0)
					return retval;
				break;
			}

			/* Accumulate the interrupt count */
			intr_count += (rmi_fd.intr_src_count & MASK_3BIT);

			if (fhandler && rmi_fd.intr_src_count) {
				list_add_tail(&fhandler->link,
						&rmi->support_fn_list);
			}
		}
	}

flash_prog_mode:
	rmi4_data->num_of_intr_regs = (intr_count + 7) / 8;
	dev_dbg(&rmi4_data->i2c_client->dev,
			"%s: Number of interrupt registers = %d\n",
			__func__, rmi4_data->num_of_intr_regs);

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_query_base_addr,
			f01_query,
			sizeof(f01_query));
	if (retval < 0)
		return retval;

#ifdef CONFIG_HUAWEI_HW_DEV_DCT
	/* detect current device successful, set the flag as present */
	set_hw_dev_flag(DEV_I2C_TOUCH_PANEL);
#endif

	/* RMI Version 4.0 currently supported */
	rmi->version_major = 4;
	rmi->version_minor = 0;

	rmi->manufacturer_id = f01_query[0];
	rmi->product_props = f01_query[1];
	rmi->product_info[0] = f01_query[2] & MASK_7BIT;
	rmi->product_info[1] = f01_query[3] & MASK_7BIT;
	rmi->date_code[0] = f01_query[4] & MASK_5BIT;
	rmi->date_code[1] = f01_query[5] & MASK_4BIT;
	rmi->date_code[2] = f01_query[6] & MASK_5BIT;
	rmi->tester_id = ((f01_query[7] & MASK_7BIT) << 8) |
			(f01_query[8] & MASK_7BIT);
	rmi->serial_number = ((f01_query[9] & MASK_7BIT) << 8) |
			(f01_query[10] & MASK_7BIT);
	memcpy(rmi->product_id_string, &f01_query[11], 10);

	if (rmi->manufacturer_id != 1) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Non-Synaptics device found, manufacturer ID = %d\n",
				__func__, rmi->manufacturer_id);
	}

	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_query_base_addr + F01_BUID_ID_OFFSET,
			rmi->build_id,
			sizeof(rmi->build_id));
	if (retval < 0)
		return retval;

	rmi4_data->firmware_id = (unsigned int)rmi->build_id[0] +
			(unsigned int)rmi->build_id[1] * 0x100 +
			(unsigned int)rmi->build_id[2] * 0x10000;

	memset(rmi4_data->intr_mask, 0x00, sizeof(rmi4_data->intr_mask));

	/*
	 * Map out the interrupt bit masks for the interrupt sources
	 * from the registered function handlers.
	 */
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->num_of_data_sources) {
				rmi4_data->intr_mask[fhandler->intr_reg_num] |=
						fhandler->intr_mask;
			}
		}
	}

	/* Enable the interrupt sources */
	for (ii = 0; ii < rmi4_data->num_of_intr_regs; ii++) {
		if (rmi4_data->intr_mask[ii] != 0x00) {
			dev_dbg(&rmi4_data->i2c_client->dev,
					"%s: Interrupt enable mask %d = 0x%02x\n",
					__func__, ii, rmi4_data->intr_mask[ii]);
			intr_addr = rmi4_data->f01_ctrl_base_addr + 1 + ii;
			retval = synaptics_rmi4_i2c_write(rmi4_data,
					intr_addr,
					&(rmi4_data->intr_mask[ii]),
					sizeof(rmi4_data->intr_mask[ii]));
			if (retval < 0)
				return retval;
		}
	}

	synaptics_rmi4_set_configured(rmi4_data);

	return 0;
}

static void synaptics_rmi4_set_params(struct synaptics_rmi4_data *rmi4_data)
{
	unsigned char ii;
	struct synaptics_rmi4_f1a_handle *f1a;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_device_info *rmi;

	rmi = &(rmi4_data->rmi4_mod_info);

	tp_log_debug("%s: sensor_max_x = %d,sensor_max_y = %d	",__func__, rmi4_data->sensor_max_x,rmi4_data->sensor_max_y);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_POSITION_X, 0,
#ifndef CONFIG_HUAWEI_KERNEL
			rmi4_data->sensor_max_x, 0, 0);
#else /*CONFIG_HUAWEI_KERNEL*/
			rmi4_data->board->lcd_x-1, 0, 0);
#endif /*CONFIG_HUAWEI_KERNEL*/
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_POSITION_Y, 0,
#ifndef CONFIG_HUAWEI_KERNEL
			rmi4_data->sensor_max_y, 0, 0);
#else /*CONFIG_HUAWEI_KERNEL*/
			rmi4_data->board->lcd_y-1, 0, 0);
#endif /*CONFIG_HUAWEI_KERNEL*/
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_PRESSURE, 0, 255, 0, 0);
#ifdef REPORT_2D_W
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_TOUCH_MAJOR, 0,
			rmi4_data->max_touch_width, 0, 0);
	input_set_abs_params(rmi4_data->input_dev,
			ABS_MT_TOUCH_MINOR, 0,
			rmi4_data->max_touch_width, 0, 0);
#endif

#ifdef TYPE_B_PROTOCOL
	input_mt_init_slots(rmi4_data->input_dev,
			rmi4_data->num_of_fingers);
#endif

	f1a = NULL;
	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F1A)
				f1a = fhandler->data;
		}
	}

	if (f1a) {
		for (ii = 0; ii < f1a->valid_button_count; ii++) {
			set_bit(f1a->button_map[ii],
					rmi4_data->input_dev->keybit);
			input_set_capability(rmi4_data->input_dev,
					EV_KEY, f1a->button_map[ii]);
		}
	}

	return;
}

static int synaptics_rmi4_set_input_dev(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	int temp;

	rmi4_data->input_dev = input_allocate_device();
	if (rmi4_data->input_dev == NULL) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to allocate input device\n",
				__func__);
		retval = -ENOMEM;
		goto err_input_device;
	}

	retval = synaptics_rmi4_query_device(rmi4_data);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to query device\n",
				__func__);
		goto err_query_device;
	}

	rmi4_data->input_dev->name = DRIVER_NAME;
	rmi4_data->input_dev->phys = INPUT_PHYS_NAME;
	rmi4_data->input_dev->id.product = SYNAPTICS_DSX_DRIVER_PRODUCT;
	rmi4_data->input_dev->id.version = SYNAPTICS_DSX_DRIVER_VERSION;
	rmi4_data->input_dev->id.bustype = BUS_I2C;
	rmi4_data->input_dev->dev.parent = &rmi4_data->i2c_client->dev;
	input_set_drvdata(rmi4_data->input_dev, rmi4_data);

	set_bit(EV_SYN, rmi4_data->input_dev->evbit);
	set_bit(EV_KEY, rmi4_data->input_dev->evbit);
	set_bit(EV_ABS, rmi4_data->input_dev->evbit);
	set_bit(BTN_TOUCH, rmi4_data->input_dev->keybit);
#if USE_BTN_TOOL_FINGER//huawei 11-25
	set_bit(BTN_TOOL_FINGER, rmi4_data->input_dev->keybit);
#endif//huawei 11-25
#ifdef INPUT_PROP_DIRECT
	set_bit(INPUT_PROP_DIRECT, rmi4_data->input_dev->propbit);
#endif
#if USE_WAKEUP_GESTURE
	retval = init_easy_wakeup_key_value(rmi4_data);
	if (retval < 0) {
		tp_log_err( "%s: Error, init easy wakeup key value fail!\n",
			__func__);
		rmi4_data->board->easy_wakeup_supported_gestures = 0;
	}
#endif /*USE_WAKEUP_GESTURE*/

	if (rmi4_data->board->swap_axes) {
		temp = rmi4_data->sensor_max_x;
		rmi4_data->sensor_max_x = rmi4_data->sensor_max_y;
		rmi4_data->sensor_max_y = temp;
	}

	synaptics_rmi4_set_params(rmi4_data);

	retval = input_register_device(rmi4_data->input_dev);
	if (retval) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to register input device\n",
				__func__);
		goto err_register_input;
	}

	return 0;

err_register_input:
err_query_device:
	synaptics_rmi4_empty_fn_list(rmi4_data);
	input_free_device(rmi4_data->input_dev);

err_input_device:
	return retval;
}

static int synaptics_rmi4_set_gpio(struct synaptics_dsx_platform_data *platform_data)
{
	int retval;
#ifndef CONFIG_HUAWEI_KERNEL
	int power_on;
#else /*CONFIG_HUAWEI_KERNEL*/
	int irq_gpio;
	int rst_gpio;
#endif /*CONFIG_HUAWEI_KERNEL*/
	int reset_on;

#ifndef CONFIG_HUAWEI_KERNEL
	power_on = platform_data->power_on_state;
#endif /*CONFIG_HUAWEI_KERNEL*/
	reset_on = platform_data->reset_on_state;
	tp_log_debug("%s: reset_on_state = %d ",__func__, reset_on);

	if (platform_data->irq_gpio >= 0) {
#ifndef CONFIG_HUAWEI_KERNEL
		retval = synaptics_gpio_setup(
				platform_data->irq_gpio,
				true, 0, 0);
		if (retval < 0) {
			pr_err("%s: Failed to configure attention GPIO\n",
					__func__);
			goto err_gpio_irq;
		}
#else /*CONFIG_HUAWEI_KERNEL*/
		irq_gpio = platform_data->irq_gpio;
		retval = gpio_request(irq_gpio, "dsx_gpio_irq");
		if (retval)
		{
			tp_log_err("%s: Failed to get tp gpio %d (code: %d)",__func__, irq_gpio, retval);
			goto err_gpio_irq;
		}

		retval = gpio_tlmm_config(GPIO_CFG(irq_gpio,0,GPIO_CFG_INPUT,
									GPIO_CFG_PULL_UP,GPIO_CFG_8MA),GPIO_CFG_ENABLE);
		if(retval < 0)
		{
			tp_log_err("%s: Fail set gpio as pull up=%d\n",__func__, irq_gpio);
			goto err_gpio_power;
		}
		retval = gpio_direction_input(irq_gpio);
#endif /*CONFIG_HUAWEI_KERNEL*/
	}

#ifndef CONFIG_HUAWEI_KERNEL
	if (platform_data->power_gpio >= 0) {
		retval = synaptics_gpio_setup(
				platform_data->power_gpio,
				true, 1, !power_on);
		if (retval < 0) {
			pr_err("%s: Failed to configure power GPIO\n",
					__func__);
			goto err_gpio_power;
		}
	}
#endif /*CONFIG_HUAWEI_KERNEL*/

	if (platform_data->reset_gpio >= 0) {
#ifndef CONFIG_HUAWEI_KERNEL
		retval = synaptics_gpio_setup(
				platform_data->reset_gpio,
				true, 1, !reset_on);
		if (retval < 0) {
			pr_err("%s: Failed to configure reset GPIO\n",
					__func__);
			goto err_gpio_reset;
		}
#else /*CONFIG_HUAWEI_KERNEL*/
		rst_gpio = platform_data->reset_gpio;

		retval = gpio_request(rst_gpio, "dsx_gpio_rst");
		if (retval)
		{
			tp_log_err("%s:touch rst gpio request failed\n", __func__);
			goto err_gpio_power;
		}
		
		retval = gpio_tlmm_config(GPIO_CFG(rst_gpio,0, GPIO_CFG_OUTPUT, 
								GPIO_CFG_PULL_UP,GPIO_CFG_8MA), GPIO_CFG_ENABLE);
		if (retval)
		{
			tp_log_err("%s:touch int gpio config failed\n", __func__);
			goto err_gpio_reset;
		}
		
		retval = gpio_direction_output(rst_gpio, !reset_on);//pull up
		mdelay(5);
		retval = gpio_direction_output(rst_gpio, reset_on);//pull down
		mdelay(platform_data->reset_active_ms); 																																																																																																																																																										 
		retval = gpio_direction_output(rst_gpio, !reset_on);//pull up
		mdelay(platform_data->reset_delay_ms);
#endif /*CONFIG_HUAWEI_KERNEL*/
	}

#ifndef CONFIG_HUAWEI_KERNEL
	if (platform_data->power_gpio >= 0) {
		gpio_set_value(platform_data->power_gpio, power_on);
		msleep(platform_data->power_delay_ms);
	}

	if (platform_data->reset_gpio >= 0) {
		gpio_set_value(platform_data->reset_gpio, reset_on);
		msleep(platform_data->reset_active_ms);
		gpio_set_value(platform_data->reset_gpio, !reset_on);
		msleep(platform_data->reset_delay_ms);
	}
#endif /*CONFIG_HUAWEI_KERNEL*/

	return 0;

err_gpio_reset:
#ifndef CONFIG_HUAWEI_KERNEL
	if (platform_data->power_gpio >= 0) {
		synaptics_gpio_setup(
				platform_data->power_gpio,
				false, 0, 0);
	}
#else /*CONFIG_HUAWEI_KERNEL*/
	tp_log_info("%s: reset_false ",__func__);
	synaptics_gpio_setup(
			platform_data->reset_gpio,
			false, 0, 0);
#endif /*CONFIG_HUAWEI_KERNEL*/

err_gpio_power:
	synaptics_gpio_setup(
			platform_data->irq_gpio,
			false, 0, 0);

err_gpio_irq:
	return retval;
}

static int synaptics_rmi4_free_fingers(struct synaptics_rmi4_data *rmi4_data)
{
	unsigned char ii;

	tp_log_debug("%s: in \n",__func__);
#ifdef TYPE_B_PROTOCOL
	for (ii = 0; ii < rmi4_data->num_of_fingers; ii++) {
		input_mt_slot(rmi4_data->input_dev, ii);
		input_mt_report_slot_state(rmi4_data->input_dev,
				MT_TOOL_FINGER, 0);
	}
#endif
	input_report_key(rmi4_data->input_dev,
			BTN_TOUCH, 0);
#if USE_BTN_TOOL_FINGER//huawei 11-25
	input_report_key(rmi4_data->input_dev,
			BTN_TOOL_FINGER, 0);
#endif//huawei 11-25
#ifndef TYPE_B_PROTOCOL
	input_mt_sync(rmi4_data->input_dev);
#endif
	input_sync(rmi4_data->input_dev);

	rmi4_data->fingers_on_2d = false;

	tp_log_debug("%s: out ",__func__);
	return 0;
}

static int synaptics_rmi4_reinit_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char ii;
	unsigned short intr_addr;
	struct synaptics_rmi4_fn *fhandler;
	struct synaptics_rmi4_exp_fhandler *exp_fhandler;
	struct synaptics_rmi4_device_info *rmi;
	unsigned char intr_status[MAX_INTR_REGISTERS];
#ifndef CONFIG_OF
	if (*platform_data->regulator_name != 0x00) {
		rmi4_data->regulator = regulator_get(&client->dev,
				platform_data->regulator_name);
	}
#else
	const struct synaptics_dsx_platform_data *platform_data =
#ifndef CONFIG_HUAWEI_KERNEL
			rmi4_data->i2c_client->dev.platform_data;
#else /*CONFIG_HUAWEI_KERNEL*/
			rmi4_data->board;
#endif /*CONFIG_HUAWEI_KERNEL*/
	if (platform_data->reg_en) {
		rmi4_data->regulator = regulator_get(&rmi4_data->i2c_client->dev,
				"vdd");
	}
#endif

	rmi = &(rmi4_data->rmi4_mod_info);

	mutex_lock(&(rmi4_data->rmi4_reset_mutex));

	tp_log_debug("%s(%d):before gpio17= %d\n",__func__,__LINE__,gpio_get_value(17));
	
	synaptics_rmi4_free_fingers(rmi4_data);
	
	tp_log_debug("%s(%d):after gpio17= %d\n",__func__,__LINE__,gpio_get_value(17));
	
	/* Clear interrupts first */
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_data_base_addr + 1,
			intr_status,
			rmi4_data->num_of_intr_regs);
	tp_log_debug("%s: intr_status[0] = %d ,retval = %d \n",__func__,intr_status[0],retval);
	if (retval < 0){
		goto exit;
	}
	
	tp_log_debug("%s(%d):after gpio17= %d\n",__func__,__LINE__,gpio_get_value(17));

	if (!list_empty(&rmi->support_fn_list)) {
		list_for_each_entry(fhandler, &rmi->support_fn_list, link) {
			if (fhandler->fn_number == SYNAPTICS_RMI4_F12) {
				synaptics_rmi4_f12_set_enables(rmi4_data, 0);
				break;
			}
		}
	}

	for (ii = 0; ii < rmi4_data->num_of_intr_regs; ii++) {
		if (rmi4_data->intr_mask[ii] != 0x00) {
			dev_dbg(&rmi4_data->i2c_client->dev,
					"%s: Interrupt enable mask %d = 0x%02x\n",
					__func__, ii, rmi4_data->intr_mask[ii]);
			intr_addr = rmi4_data->f01_ctrl_base_addr + 1 + ii;
			retval = synaptics_rmi4_i2c_write(rmi4_data,
					intr_addr,
					&(rmi4_data->intr_mask[ii]),
					sizeof(rmi4_data->intr_mask[ii]));
			if (retval < 0)
				goto exit;
		}
	}

	mutex_lock(&exp_data.mutex);
	if (!list_empty(&exp_data.list)) {
		list_for_each_entry(exp_fhandler, &exp_data.list, link)
			if (exp_fhandler->exp_fn->reinit != NULL)
				exp_fhandler->exp_fn->reinit(rmi4_data);
	}
	mutex_unlock(&exp_data.mutex);

	synaptics_rmi4_set_configured(rmi4_data);

	retval = 0;

exit:
	mutex_unlock(&(rmi4_data->rmi4_reset_mutex));
	return retval;
}

static int synaptics_rmi4_reset_device(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	int temp;
	unsigned char command = 0x01;
	struct synaptics_rmi4_exp_fhandler *exp_fhandler;

	mutex_lock(&(rmi4_data->rmi4_reset_mutex));

	rmi4_data->touch_stopped = true;

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_cmd_base_addr,
			&command,
			sizeof(command));
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to issue reset command, error = %d\n",
				__func__, retval);
		mutex_unlock(&(rmi4_data->rmi4_reset_mutex));
		return retval;
	}

	msleep(rmi4_data->board->reset_delay_ms);

	synaptics_rmi4_free_fingers(rmi4_data);

	synaptics_rmi4_empty_fn_list(rmi4_data);

	retval = synaptics_rmi4_query_device(rmi4_data);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to query device\n",
				__func__);
		mutex_unlock(&(rmi4_data->rmi4_reset_mutex));
		return retval;
	}

	if (rmi4_data->board->swap_axes) {
		temp = rmi4_data->sensor_max_x;
		rmi4_data->sensor_max_x = rmi4_data->sensor_max_y;
		rmi4_data->sensor_max_y = temp;
	}

	synaptics_rmi4_set_params(rmi4_data);

	mutex_lock(&exp_data.mutex);
	if (!list_empty(&exp_data.list)) {
		list_for_each_entry(exp_fhandler, &exp_data.list, link)
			if (exp_fhandler->exp_fn->reset != NULL)
				exp_fhandler->exp_fn->reset(rmi4_data);
	}
	mutex_unlock(&exp_data.mutex);

	rmi4_data->touch_stopped = false;

	mutex_unlock(&(rmi4_data->rmi4_reset_mutex));

	return 0;
}

/**
* synaptics_rmi4_exp_fn_work()
*
* Called by the kernel at the scheduled time.
*
* This function is a work thread that checks for the insertion and
* removal of other expansion Function modules such as rmi_dev and calls
* their initialization and removal callback functions accordingly.
*/
static void synaptics_rmi4_exp_fn_work(struct work_struct *work)
{
	int retval;
	struct synaptics_rmi4_exp_fhandler *exp_fhandler;
	struct synaptics_rmi4_exp_fhandler *exp_fhandler_temp;
	struct synaptics_rmi4_data *rmi4_data = exp_data.rmi4_data;

	mutex_lock(&exp_data.mutex);
	if (!list_empty(&exp_data.list)) {
		list_for_each_entry_safe(exp_fhandler,
				exp_fhandler_temp,
				&exp_data.list,
				link) {
			if ((exp_fhandler->exp_fn->init != NULL) &&
					exp_fhandler->insert) {
				retval = exp_fhandler->exp_fn->init(rmi4_data);
				if (retval < 0) {
					list_del(&exp_fhandler->link);
					kfree(exp_fhandler);
				} else {
					exp_fhandler->insert = false;
				}
			} else if ((exp_fhandler->exp_fn->remove != NULL) &&
					exp_fhandler->remove) {
				exp_fhandler->exp_fn->remove(rmi4_data);
				list_del(&exp_fhandler->link);
				kfree(exp_fhandler);
			}
		}
	}
	mutex_unlock(&exp_data.mutex);

	return;
}

/**
* synaptics_rmi4_new_function()
*
* Called by other expansion Function modules in their module init and
* module exit functions.
*
* This function is used by other expansion Function modules such as
* rmi_dev to register themselves with the driver by providing their
* initialization and removal callback function pointers so that they
* can be inserted or removed dynamically at module init and exit times,
* respectively.
*/
void synaptics_rmi4_new_function(struct synaptics_rmi4_exp_fn *exp_fn,
		bool insert)
{
	struct synaptics_rmi4_exp_fhandler *exp_fhandler;

	if (!exp_data.initialized) {
		mutex_init(&exp_data.mutex);
		INIT_LIST_HEAD(&exp_data.list);
		exp_data.initialized = true;
	}

	mutex_lock(&exp_data.mutex);
	if (insert) {
		exp_fhandler = kzalloc(sizeof(*exp_fhandler), GFP_KERNEL);
		if (!exp_fhandler) {
			pr_err("%s: Failed to alloc mem for expansion function\n",
					__func__);
			goto exit;
		}
		exp_fhandler->exp_fn = exp_fn;
		exp_fhandler->insert = true;
		exp_fhandler->remove = false;
		list_add_tail(&exp_fhandler->link, &exp_data.list);
	} else if (!list_empty(&exp_data.list)) {
		list_for_each_entry(exp_fhandler, &exp_data.list, link) {
			if (exp_fhandler->exp_fn->fn_type == exp_fn->fn_type) {
				exp_fhandler->insert = false;
				exp_fhandler->remove = true;
				goto exit;
			}
		}
	}

exit:
	mutex_unlock(&exp_data.mutex);

	if (exp_data.queue_work) {
		queue_delayed_work(exp_data.workqueue,
				&exp_data.work,
				msecs_to_jiffies(EXP_FN_WORK_DELAY_MS));
	}

	return;
}
EXPORT_SYMBOL(synaptics_rmi4_new_function);

#ifdef ENABLE_VIRTUAL_KEYS//huawei 11-25

#define VIRTUAL_KEY_ELEMENT_SIZE	5
#define  MAX_NAME_LENGTH 256
#define DSX_MAX_PRBUF_SIZE		PIPE_BUF

static ssize_t virtual_keys_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct syanptics_virtual_keys *vkeys = container_of(attr,
		struct syanptics_virtual_keys, kobj_attr);
	u16 *data = vkeys->data;
	int size = vkeys->size;
	int index;
	int i;

	index = 0;
	for (i = 0; i < size; i += VIRTUAL_KEY_ELEMENT_SIZE)
		index += scnprintf(buf + index, DSX_MAX_PRBUF_SIZE - index,
			"0x01:%d:%d:%d:%d:%d\n",
			data[i], data[i+1], data[i+2], data[i+3], data[i+4]);

	return index;
}

static u16 *create_and_get_u16_array(struct device_node *dev_node,
		const char *name, int *size)
{
	const __be32 *values;
	u16 *val_array;
	int len;
	int sz;
	int rc;
	int i;

	values = of_get_property(dev_node, name, &len);
	if (values == NULL)
		return NULL;

	sz = len / sizeof(u32);
	tp_log_debug("%s: %s size:%d\n", __func__, name, sz);

	val_array = kzalloc(sz * sizeof(u16), GFP_KERNEL);
	if (val_array == NULL) {
		rc = -ENOMEM;
		goto fail;
	}

	for (i = 0; i < sz; i++)
		val_array[i] = (u16)be32_to_cpup(values++);

	*size = sz;

	return val_array;

fail:
	tp_log_debug("%s:fail rc=%d\n", __func__, rc);
	return ERR_PTR(rc);
}

static int setup_virtual_keys(struct device_node *dev_node,
		const char *inp_dev_name, struct syanptics_virtual_keys *vkeys)
{
	char *name;
	u16 *data;
	int size = 0;
	int rc = -EINVAL;

	data = create_and_get_u16_array(dev_node, "synaptics,virtual_keys", &size);
	if (data == NULL)
	{
		/*if virtual keys are not supported  return error*/
		return -ENOMEM;
	}
	else if (IS_ERR(data)) {
		rc = PTR_ERR(data);
		goto fail;
	}

	/* Check for valid virtual keys size */
	if (size % VIRTUAL_KEY_ELEMENT_SIZE) {
		rc = -EINVAL;
		goto fail_free_data;
	}

	name = kzalloc(MAX_NAME_LENGTH, GFP_KERNEL);
	if (name == NULL) {
		rc = -ENOMEM;
		goto fail_free_data;
	}

	snprintf(name, MAX_NAME_LENGTH, "virtualkeys.%s", inp_dev_name);

	vkeys->data = data;
	vkeys->size = size;

	/* TODO: Instantiate in board file and export it */
	if (board_properties_kobj == NULL)
		board_properties_kobj =
			kobject_create_and_add("board_properties", NULL);
	if (board_properties_kobj == NULL) {
		tp_log_err("%s: Cannot get board_properties kobject!\n", __func__);
		rc = -EINVAL;
		goto fail_free_name;
	}

	/* Initialize dynamic SysFs attribute */
	sysfs_attr_init(&vkeys->kobj_attr.attr);
	vkeys->kobj_attr.attr.name = name;
	vkeys->kobj_attr.attr.mode = S_IRUGO;
	vkeys->kobj_attr.show = virtual_keys_show;

	rc = sysfs_create_file(board_properties_kobj, &vkeys->kobj_attr.attr);
	if (rc)
		goto fail_del_kobj;

	return 0;

fail_del_kobj:
	kobject_del(board_properties_kobj);
fail_free_name:
	kfree(name);
	vkeys->kobj_attr.attr.name = NULL;
fail_free_data:
	kfree(data);
	vkeys->data = NULL;
	vkeys->size = 0;
fail:
	return rc;
}

static void free_virtual_keys(struct syanptics_virtual_keys *vkeys)
{
	if (board_properties_kobj)
		sysfs_remove_file(board_properties_kobj,
			&vkeys->kobj_attr.attr);

	kfree(vkeys->data);
	vkeys->data = NULL;
	vkeys->size = 0;

	kfree(vkeys->kobj_attr.attr.name);
	vkeys->kobj_attr.attr.name = NULL;
}
#endif /*ENABLE_VIRTUAL_KEYS*/

static struct syanptics_wakeup_keys *create_and_get_wakeup_keys(
		struct device_node *dev_node)
{
	struct syanptics_wakeup_keys *wakeup_keys;
	u16 *keys;
	int size;
	int rc;

	keys = create_and_get_u16_array(dev_node, "synaptics,easy_wakeup_gesture_keys", &size);
	if (IS_ERR_OR_NULL(keys))
		return (void *)keys;

	tp_log_debug("%s:  size:%d\n", __func__, size);
	/* Check for valid abs size */
	if (size > GESTURE_MAX) {
		rc = -EINVAL;
		goto fail_free_keys;
	}

	wakeup_keys = kzalloc(sizeof(*wakeup_keys), GFP_KERNEL);
	if (wakeup_keys == NULL) {
		rc = -ENOMEM;
		goto fail_free_keys;
	}

	wakeup_keys->keys = keys;
	wakeup_keys->size = size;

	return wakeup_keys;

fail_free_keys:
	kfree(keys);

	tp_log_debug("%s:fail rc=%d\n", __func__, rc);
	return ERR_PTR(rc);
}

static void free_wakeup_keys(struct syanptics_wakeup_keys *wakeup_keys)
{
	/*If call in failure case when frmwrk is free but abs is not it might crash,
	use check even its called in failure case*/
	if (wakeup_keys)
		kfree(wakeup_keys->keys);
	kfree(wakeup_keys);
}
#ifdef CONFIG_OF
static int synaptics_rmi4_parse_dt(struct device *dev, struct synaptics_dsx_platform_data *pdata)
{
	//int retval; //TODO: error checking
	struct device_node *np = dev->of_node;
	struct property *prop;
	u32 tmp = 0;
	int err = 0;

	pdata->x_flip = of_property_read_bool(np, "synaptics,x-flip");
	pdata->y_flip = of_property_read_bool(np, "synaptics,y-flip");
	pdata->swap_axes = of_property_read_bool(np, "synaptics,swap-axes");

#ifndef CONFIG_HUAWEI_KERNEL
	/* regulator info */
	err = of_property_read_string(dev->of_node,"synaptics,vdd", (const char **)&pdata->regulator_vdd);
	if (err) {
		tp_log_err("%s: regulator_name error err=%d\n", __func__, err);
	}

	err = of_property_read_string(dev->of_node,"synaptics,vbus", (const char **)&pdata->regulator_vbus);
	if (err) {
		tp_log_err("%s: regulator_name error err=%d\n", __func__, err);
	}

	pdata->reg_en = of_property_read_bool(np, "synaptics,reg-en");
	/* reset, irq gpio info */
#endif /*CONFIG_HUAWEI_KERNEL*/

	pdata->irq_gpio = of_get_named_gpio_flags(np, "synaptics,irq-gpio", 0, NULL);
#ifndef CONFIG_HUAWEI_KERNEL
	pdata->irq_flags = IRQF_TRIGGER_LOW;
#else /*CONFIG_HUAWEI_KERNEL*/
	pdata->irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
#endif /*CONFIG_HUAWEI_KERNEL*/

#ifndef CONFIG_HUAWEI_KERNEL
	if (of_find_property(np, "synaptics,power-gpio", NULL))
		pdata->power_gpio = of_get_named_gpio_flags(np, "synaptics,power-gpio", 0, NULL);
	else
		pdata->power_gpio = -1;
#endif /*CONFIG_HUAWEI_KERNEL*/

	if (of_find_property(np, "synaptics,reset-gpio", NULL))
		pdata->reset_gpio = of_get_named_gpio_flags(np, "synaptics,reset-gpio", 0, NULL);
	else
		pdata->reset_gpio = -1;

#ifndef CONFIG_HUAWEI_KERNEL
	tmp = 0;
	err = of_property_read_u32(np, "synaptics,power-on-status", &tmp);
	if (!err)
		pdata->power_on_state = tmp;
	else
		pdata->power_on_state = 1;
#endif /*CONFIG_HUAWEI_KERNEL*/
		
#ifdef CONFIG_HUAWEI_KERNEL
	tmp = 0;
	err = of_property_read_u32(np, "synaptics,lcd-x", &tmp);
	if (!err)
		pdata->lcd_x = tmp;
	else
		pdata->lcd_x = LCD_X_DEFAULT;

	tmp = 0;
	err = of_property_read_u32(np, "synaptics,lcd-y", &tmp);
	if (!err)
		pdata->lcd_y = tmp;
	else
		pdata->lcd_y = LCD_Y_DEFAULT;

	tmp = 0;
	err = of_property_read_u32(np, "synaptics,lcd-all", &tmp);
	if (!err)
		pdata->lcd_all = tmp;
	else
		pdata->lcd_all = LCD_ALL_DEFAULT;
	
#ifdef ENABLE_VIRTUAL_KEYS
	err = setup_virtual_keys(np, DRIVER_NAME,&pdata->vkeys);
	if (err) {
		tp_log_err("%s: Cannot setup virtual keys, only TP will work now!err = %d\n",__func__,err);
		//If the virtual keys are not supported the TP should work fine;
	}
#endif

	err = of_property_read_string(np, "synaptics,product_name",&pdata->product_name);
	if (err){
		tp_log_err("Unable to read firmware_name\n");			
		pdata->product_name = "Unknow";
	}
#endif /*CONFIG_HUAWEI_KERNEL*/

	tmp = 0;
	err = of_property_read_u32(np, "synaptics,reset-on-status", &tmp);
	if (!err)
		pdata->reset_on_state = tmp;
	else
		pdata->reset_on_state = 0;

	tmp = 0;
	err = of_property_read_u32(np, "synaptics,power-delay-ms", &tmp);
	if (!err)
		pdata->power_delay_ms = tmp;
	else
		pdata->power_delay_ms = 160;

	tmp = 0;
	err = of_property_read_u32(np, "synaptics,reset-delay-ms", &tmp);
	if  (!err)
		pdata->reset_delay_ms = tmp;
	else
		pdata->reset_delay_ms = 10;

	tmp = 0;
	err = of_property_read_u32(np, "synaptics,reset-active-ms", &tmp);
	if (!err)
		pdata->reset_active_ms = tmp;
	else
		pdata->reset_active_ms = 100;

	prop = of_find_property(np, "synaptics,button-map", NULL);
	if (prop) {
		pdata->cap_button_map->nbuttons = prop->length / sizeof(tmp);
		err = of_property_read_u32_array(np,
			"synaptics,button-map", (unsigned int *)pdata->cap_button_map->map,
			pdata->cap_button_map->nbuttons);
		if (err) {
			tp_log_err("Unable to read key codes\n");			
			pdata->cap_button_map->map = NULL;			
		}
	}

	err = of_property_read_u32(np, "synaptics,gesture_enabled", &tmp);
	if (!err)
		pdata->gesture_enabled = tmp;
	
	err = of_property_read_u32(np, "synaptics,easy_wakeup_supported_gestures", &tmp);
	if (!err)
		pdata->easy_wakeup_supported_gestures = (u32)tmp;

	pdata->wakeup_keys = create_and_get_wakeup_keys(np);
	if (IS_ERR_OR_NULL(pdata->wakeup_keys)) {
		tp_log_err("%s: Wakeup gesture is not configured!\n", __func__);
		pdata->wakeup_keys = NULL;
	}else{
		/*change to debug*/
		tp_log_info("%s: Wakeup gesture is configured for %d guestures\n", 
			__func__,pdata->wakeup_keys->size);
	}

	err = of_property_read_u32(np, "synaptics,glove_enabled", &tmp);
	if (!err)
		pdata->glove_enabled = tmp;
	return 0;
}
#else
static int synaptics_rmi4_parse_dt(struct device *dev, struct synaptics_dsx_platform_data *pdata)
{
	return -ENODEV;
}
#endif

#ifdef CONFIG_HUAWEI_KERNEL
 static int synaptics_rmi4_power_on(struct device *dev)
 {
	 char const *power_pin_vdd;
	 char const *power_pin_vbus;
	 struct regulator *vdd_synaptics;
	 struct regulator *vbus_synaptics;
	 int rc;

		/*get the power name*/
	 rc = of_property_read_string(dev->of_node,"synaptics,vdd", &power_pin_vdd);
	 if (rc) {
		 tp_log_err("%s: OF error vdd rc=%d\n", __func__, rc);
	 }
	 
	 rc = of_property_read_string(dev->of_node,"synaptics,vbus", &power_pin_vbus);
	 if (rc) {
		 tp_log_err("%s: OF error vbus rc=%d\n", __func__, rc);
	 }
		 /* VDD power on */
 
	 vdd_synaptics = regulator_get(dev, power_pin_vdd);
	 if (IS_ERR(vdd_synaptics)) {
		 tp_log_err("%s: vdd_synaptics regulator get fail, rc=%d\n", __func__, rc);
		 return  -EINVAL;
	 }
 
	 rc = regulator_set_voltage(vdd_synaptics,2850000,2850000);
	 if(rc < 0){
		 tp_log_err( "%s: vdd_synaptics regulator set fail, rc=%d\n", __func__, rc);
		 return  -EINVAL;
	 }
 
	 rc = regulator_enable(vdd_synaptics);
	 if (rc < 0) {
		 tp_log_err( "%s: vdd_synaptics regulator enable fail, rc=%d\n", __func__, rc);
		 return -EINVAL;
	 }
 
	 /* VBUS power on */
	 vbus_synaptics = regulator_get(dev, power_pin_vbus);
	 if (IS_ERR(vbus_synaptics)) {
		 tp_log_err( "%s: vbus_synaptics regulator get fail, rc=%d\n", __func__, rc);
		 return -EINVAL;
	 }
 
	 rc = regulator_set_voltage(vbus_synaptics,1800000,1800000);
	 if(rc < 0){
		 tp_log_err( "%s: vbus_synaptics regulator set fail, rc=%d\n", __func__, rc);
		 return -EINVAL;
	 }
  
	 rc = regulator_enable(vbus_synaptics);
	 if (rc < 0) {
		 tp_log_err( "%s: vbus_synaptics regulator enable fail, rc=%d\n", __func__, rc);
		 return -EINVAL;
	 }
	 return 0;
 }
#endif /*CONFIG_HUAWEI_KERNEL*/

#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_FB)
 static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
 {
	 struct fb_event *evdata = data;
	 int *blank = NULL;
	 int ret = 0;
	 static int runtime_count = 0;
	 struct synaptics_rmi4_data *rmi4_data =
			 container_of(self, struct synaptics_rmi4_data,
			 fb_notif);
 
	 struct device *dev = &(rmi4_data->input_dev->dev);
	 
	 if (evdata && evdata->data && event == FB_EVENT_BLANK && rmi4_data) {
		 blank = evdata->data;
		 dev_info(dev, "%s:%d %d\n",__func__,__LINE__, *blank);
		 /*In case of resume we get BLANK_UNBLANK and for suspen BLANK_POWERDOWN*/
		 if (*blank == FB_BLANK_UNBLANK)
		 {
			 tp_log_debug("%s:line%d , rt_counter=%d\n",__func__,__LINE__, rmi4_data->i2c_client->dev.power.usage_count.counter);
			 if (0 == runtime_count)
			 {
				 ret = pm_runtime_get_sync(&rmi4_data->i2c_client->dev);
				 if (ret < 0)
				 {
					 tp_log_debug("%s:line%d , runtime_count=%d,ret=%d\n",__func__,__LINE__, runtime_count,ret);
				 }
				 else
				 {
					 runtime_count = 1;//in case that fb_callback more than once
					 tp_log_debug("%s:line%d , runtime_count=%d,ret=%d\n",__func__,__LINE__, runtime_count,ret);
				 }
			 }
			 else
			 {
				 tp_log_debug("%s:line%d , runtime_count=%d,TP already resumed!\n",__func__,__LINE__, runtime_count);
			 }
		 }
		 else if (*blank == FB_BLANK_POWERDOWN)
		 {
			tp_log_debug("%s:line%d , rt_counter=%d\n",__func__,__LINE__, rmi4_data->i2c_client->dev.power.usage_count.counter);
			if (1 == runtime_count)
			{
				 ret = pm_runtime_put(&rmi4_data->i2c_client->dev);
				 if (ret < 0)
				 {
					 tp_log_debug("%s:line%d , runtime_count=%d,ret=%d\n",__func__,__LINE__, runtime_count,ret);
				 }
				 else
				 {
					 runtime_count = 0;//in case that fb_callback more than once
					 tp_log_debug("%s:line%d , runtime_count=%d,ret=%d\n",__func__,__LINE__, runtime_count,ret);
				 }
			 }
			 else
			 {
				 tp_log_debug("%s:line%d , runtime_count=%d,TP already suspended!\n",__func__,__LINE__, runtime_count);
			 }
		 }
	 }
	 return 0;
 }
 
 static void configure_sleep(struct synaptics_rmi4_data *rmi4_data)
 {
	 int retval = 0;
		struct device *dev = &(rmi4_data->input_dev->dev);
		
	 rmi4_data->fb_notif.notifier_call = fb_notifier_callback;
 
	 /*register the callback to the FB*/
	 retval = fb_register_client(&rmi4_data->fb_notif);
	 if (retval)
		 dev_info(dev,
			 "Unable to register fb_notifier: %d\n", retval);
	 return;
 }
#endif

 /**
 * synaptics_rmi4_probe()
 *
 * Called by the kernel when an association with an I2C device of the
 * same name is made (after doing i2c_add_driver).
 *
 * This funtion allocates and initializes the resources for the driver
 * as an input driver, turns on the power to the sensor, queries the
 * sensor for its supported Functions and characteristics, registers
 * the driver to the input subsystem, sets up the interrupt, handles
 * the registration of the early_suspend and late_resume functions,
 * and creates a work queue for detection of other expansion Function
 * modules.
 */

static int __devinit synaptics_rmi4_probe(struct i2c_client *client,
		const struct i2c_device_id *dev_id)
{
	int retval;
	unsigned char attr_count;
	struct synaptics_rmi4_data *rmi4_data;
	struct synaptics_dsx_platform_data *platform_data =
			client->dev.platform_data;

	if(touch_hw_data.read_touch_probe_flag)
	{
		retval = touch_hw_data.read_touch_probe_flag();
		if(retval)
		{
			dev_err(&client->dev, "%s:the touch driver has detected! \n",__func__);
			return -EPERM;
		}
	}

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev,
				"%s: SMBus byte data not supported\n",
				__func__);
		return -EIO;
	}

	if (client->dev.of_node) {
		platform_data = devm_kzalloc(&client->dev,
			sizeof(struct synaptics_dsx_platform_data), GFP_KERNEL);
		if (!platform_data) {
			dev_err(&client->dev, "%s: Failed to allocate memory for pdata\n", __func__);
			return -ENOMEM;
		}

		retval = synaptics_rmi4_parse_dt(&client->dev, platform_data);
		if (retval)
			return retval;
	}
	if (!platform_data) {
		dev_err(&client->dev,
				"%s: No platform data found\n",
				__func__);
		return -EINVAL;
	}

	rmi4_data = kzalloc(sizeof(*rmi4_data) * 2, GFP_KERNEL);
	if (!rmi4_data) {
		dev_err(&client->dev,
				"%s: Failed to alloc mem for rmi4_data\n",
				__func__);
		return -ENOMEM;
	}

#ifndef CONFIG_HUAWEI_KERNEL
#ifndef CONFIG_OF
	if (*platform_data->regulator_name != 0x00) {
		rmi4_data->regulator = regulator_get(&client->dev,
				platform_data->regulator_name);
#else
	if (platform_data->reg_en) {
		rmi4_data->regulator = regulator_get(&client->dev,
				"vdd");
#endif
		if (IS_ERR(rmi4_data->regulator)) {
			dev_err(&client->dev,
					"%s: Failed to get regulator\n",
					__func__);
			retval = PTR_ERR(rmi4_data->regulator);
			goto err_regulator;
		}
		regulator_enable(rmi4_data->regulator);
	}
#else /*CONFIG_HUAWEI_KERNEL*/
	retval = synaptics_rmi4_power_on(&client->dev);
	if (retval)
	{
		goto err_regulator;
	}
#endif /*CONFIG_HUAWEI_KERNEL*/

	msleep(platform_data->power_delay_ms);

	if (platform_data->irq_gpio) {
		retval = synaptics_rmi4_set_gpio(platform_data);
		if (retval < 0) {
			dev_err(&client->dev,
					"%s: Failed to set up GPIO's\n",
					__func__);
			goto err_set_gpio;
		}
	}

#if USE_IRQ_THREAD //huawei 11-25
#else
	synaptics_wq = create_singlethread_workqueue("rmi_dsx_wq");
	if (!synaptics_wq)
	{
		tp_log_err("Could not create work queue rmi_dsx_wq: no memory");
		retval = -ENOMEM;
		goto error_wq_creat_failed; 
	}
	INIT_WORK(&rmi4_data->work, synaptics_rmi4_sensor_report);
#endif /*USE_IRQ_THREAD*/
		

	rmi4_data->i2c_client = client;
	rmi4_data->current_page = MASK_8BIT;
	rmi4_data->board = platform_data;
	rmi4_data->touch_stopped = false;
	rmi4_data->sensor_sleep = false;
	rmi4_data->irq_enabled = false;
	rmi4_data->fingers_on_2d = false;
#if USE_WAKEUP_GESTURE
	rmi4_data->gesture_enabled = platform_data->gesture_enabled;
	rmi4_data->palm_enabled = false;
	rmi4_data->easy_wakeup_gesture = false;
	rmi4_data->sleep_gesture_flag = false;
	rmi4_data->glove_enabled = platform_data->glove_enabled;
#endif /*USE_WAKEUP_GESTURE*/

	rmi4_data->i2c_read = synaptics_rmi4_i2c_read;
	rmi4_data->i2c_write = synaptics_rmi4_i2c_write;
	rmi4_data->irq_enable = synaptics_rmi4_irq_enable;
	rmi4_data->reset_device = synaptics_rmi4_reset_device;

	mutex_init(&(rmi4_data->rmi4_io_ctrl_mutex));
	mutex_init(&(rmi4_data->rmi4_reset_mutex));
	mutex_init(&(rmi4_data->rmi4_sysfs_mutex));

	i2c_set_clientdata(client, rmi4_data);

	retval = synaptics_rmi4_set_input_dev(rmi4_data);
	if (retval < 0) {
		dev_err(&client->dev,
				"%s: Failed to set up input device\n",
				__func__);
		goto err_set_input_dev;
	}

#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_FB)
       configure_sleep(rmi4_data);
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	rmi4_data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	rmi4_data->early_suspend.suspend = synaptics_rmi4_early_suspend;
	rmi4_data->early_suspend.resume = synaptics_rmi4_late_resume;
	register_early_suspend(&rmi4_data->early_suspend);
#endif

	rmi4_data->irq = gpio_to_irq(platform_data->irq_gpio);

	retval = synaptics_rmi4_irq_enable(rmi4_data, true);
	if (retval < 0) {
		dev_err(&client->dev,
				"%s: Failed to enable attention interrupt\n",
				__func__);
		goto err_enable_irq;
	}

	if (!exp_data.initialized) {
		mutex_init(&exp_data.mutex);
		INIT_LIST_HEAD(&exp_data.list);
		exp_data.initialized = true;
	}

	exp_data.workqueue = create_singlethread_workqueue("dsx_exp_workqueue");
	INIT_DELAYED_WORK(&exp_data.work, synaptics_rmi4_exp_fn_work);
	exp_data.rmi4_data = rmi4_data;
	exp_data.queue_work = true;
	queue_delayed_work(exp_data.workqueue,
			&exp_data.work,
			msecs_to_jiffies(EXP_FN_WORK_DELAY_MS));

#ifdef CONFIG_PM
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_enable(&rmi4_data->i2c_client->dev);
#endif/*CONFIG_PM_RUNTIME*/
#endif/*CONFIG_PM*/
	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		retval = sysfs_create_file(&rmi4_data->input_dev->dev.kobj,
				&attrs[attr_count].attr);
		if (retval < 0) {
			dev_err(&client->dev,
					"%s: Failed to create sysfs attributes\n",
					__func__);
			goto err_sysfs;
		}
	}
	/*set touch flag. avoid to probe repeatly!*/
	if(touch_hw_data.set_touch_probe_flag)
	{
		touch_hw_data.set_touch_probe_flag(1);
	}
#if USE_WAKEUP_GESTURE
	core_dev_ptr = &rmi4_data->input_dev->dev;
	retval = add_easy_wakeup_interfaces(&rmi4_data->input_dev->dev);
	if (retval < 0) {
		tp_log_err( "%s: Error, easy wakeup init sysfs fail! \n",
			__func__);
		goto err_sysfs;
	}

	device_init_wakeup(&rmi4_data->input_dev->dev, 1);
#endif /*USE_WAKEUP_GESTURE*/
	return retval;

err_sysfs:
	for (attr_count--; attr_count >= 0; attr_count--) {
		sysfs_remove_file(&rmi4_data->input_dev->dev.kobj,
				&attrs[attr_count].attr);
	}

#ifdef CONFIG_PM
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(&client->dev);
#endif/*CONFIG_PM_RUNTIME*/
#endif/*CONFIG_PM*/
	cancel_delayed_work_sync(&exp_data.work);
	flush_workqueue(exp_data.workqueue);
	destroy_workqueue(exp_data.workqueue);

	synaptics_rmi4_irq_enable(rmi4_data, false);

err_enable_irq:
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&rmi4_data->early_suspend);
#endif

	synaptics_rmi4_empty_fn_list(rmi4_data);
	input_unregister_device(rmi4_data->input_dev);
	rmi4_data->input_dev = NULL;

err_set_input_dev:
	if (platform_data->irq_gpio) {
		synaptics_gpio_setup(
				platform_data->irq_gpio,
				false, 0, 0);
	}

	if (platform_data->reset_gpio >= 0) {
		tp_log_info("%s: reset_false ",__func__);
		synaptics_gpio_setup(
				platform_data->reset_gpio,
				false, 0, 0);
	}

	if (platform_data->power_gpio >= 0) {
		synaptics_gpio_setup(
				platform_data->power_gpio,
				false, 0, 0);
	}

#if	USE_IRQ_THREAD //huawei 11-25
#else		
error_wq_creat_failed:
	if (synaptics_wq)
	{
		destroy_workqueue(synaptics_wq);
	}
#endif /*USE_IRQ_THREAD*/

err_set_gpio:
	if (rmi4_data->regulator) {
		regulator_disable(rmi4_data->regulator);
		regulator_put(rmi4_data->regulator);
	}

err_regulator:
#ifdef ENABLE_VIRTUAL_KEYS//huawei 11-25
	free_virtual_keys(&platform_data->vkeys);
#endif//huawei 11-25
	free_wakeup_keys(platform_data->wakeup_keys);
	kfree(rmi4_data);
	if (client->dev.of_node) {
		if (!platform_data) {
			devm_kfree(&client->dev, platform_data);
		}
	}

	return retval;
}

 /**
 * synaptics_rmi4_remove()
 *
 * Called by the kernel when the association with an I2C device of the
 * same name is broken (when the driver is unloaded).
 *
 * This funtion terminates the work queue, stops sensor data acquisition,
 * frees the interrupt, unregisters the driver from the input subsystem,
 * turns off the power to the sensor, and frees other allocated resources.
 */
static int __devexit synaptics_rmi4_remove(struct i2c_client *client)
{
	unsigned char attr_count;
	struct synaptics_rmi4_data *rmi4_data = i2c_get_clientdata(client);
	const struct synaptics_dsx_platform_data *platform_data =
			rmi4_data->board;

	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		sysfs_remove_file(&rmi4_data->input_dev->dev.kobj,
				&attrs[attr_count].attr);
	}

#ifdef CONFIG_PM
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(&client->dev);
#endif/*CONFIG_PM_RUNTIME*/
#endif/*CONFIG_PM*/
	cancel_delayed_work_sync(&exp_data.work);
	flush_workqueue(exp_data.workqueue);
	destroy_workqueue(exp_data.workqueue);

	synaptics_rmi4_irq_enable(rmi4_data, false);

#if !defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_FB)
	fb_unregister_client(&rmi4_data->fb_notif);
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&rmi4_data->early_suspend);
#endif

	synaptics_rmi4_empty_fn_list(rmi4_data);
	input_unregister_device(rmi4_data->input_dev);
	rmi4_data->input_dev = NULL;

	if (platform_data->irq_gpio) {
		synaptics_gpio_setup(
				platform_data->irq_gpio,
				false, 0, 0);
	}

	if (platform_data->reset_gpio >= 0) {
		tp_log_info("%s: reset_false ",__func__);
		synaptics_gpio_setup(
				platform_data->reset_gpio,
				false, 0, 0);
	}

	if (platform_data->power_gpio >= 0) {
		synaptics_gpio_setup(
				platform_data->power_gpio,
				false, 0, 0);
	}

	if (rmi4_data->regulator) {
		regulator_disable(rmi4_data->regulator);
		regulator_put(rmi4_data->regulator);
	}

	kfree(rmi4_data);

	return 0;
}

#ifdef CONFIG_PM
 /**
 * synaptics_rmi4_sensor_sleep()
 *
 * Called by synaptics_rmi4_early_suspend() and synaptics_rmi4_suspend().
 *
 * This function stops finger data acquisition and puts the sensor to sleep.
 */
static void synaptics_rmi4_sensor_sleep(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char device_ctrl;

	tp_log_info("%s: in ",__func__);
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to enter sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = false;
		return;
	}

	device_ctrl = (device_ctrl & ~MASK_3BIT);
	device_ctrl = (device_ctrl | NO_SLEEP_OFF | SENSOR_SLEEP);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to enter sleep mode\n",
				__func__);
		rmi4_data->sensor_sleep = false;
		return;
	} else {
		rmi4_data->sensor_sleep = true;
	}

	return;
}

 /**
 * synaptics_rmi4_sensor_wake()
 *
 * Called by synaptics_rmi4_resume() and synaptics_rmi4_late_resume().
 *
 * This function wakes the sensor from sleep.
 */
static void synaptics_rmi4_sensor_wake(struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char device_ctrl;
	unsigned char no_sleep_setting = rmi4_data->no_sleep_setting;

	tp_log_info("%s: in ",__func__);
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to read reg,retval=%d\n",
				__func__,retval);
		rmi4_data->sensor_sleep = true;
		return;
	}

	device_ctrl = (device_ctrl & ~MASK_3BIT);
	device_ctrl = (device_ctrl | no_sleep_setting | NORMAL_OPERATION);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f01_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to write reg,retval=%d\n",
				__func__,retval);
		rmi4_data->sensor_sleep = true;
		return;
	} else {
		rmi4_data->sensor_sleep = false;
	}

	return;
}
#if USE_WAKEUP_GESTURE
static void synaptics_put_device_into_easy_wakeup(struct device *dev,struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char device_ctrl;

	tp_log_info("%s: in ",__func__);
	if (true == rmi4_data->sleep_gesture_flag)
	{
		tp_log_debug( "%s gesture already enabled,return directly!\n", __func__);
		return;
	}
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f11_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	tp_log_debug( "%s device_ctrl = %d\n", __func__,device_ctrl);
	tp_log_debug( "%s f11_ctrl_base_addr = %d\n", __func__,rmi4_data->f11_ctrl_base_addr);
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to read reg,retval=%d\n",
				__func__,retval);
		rmi4_data->sensor_sleep = false;
		return;
	}

	/*write 0100 to f11_ctrl_00 will not sleep but into gesture mode*/
	device_ctrl = (device_ctrl & ~MASK_3BIT);
	device_ctrl = (device_ctrl | 0x04);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f11_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to write reg,retval=%d\n",
				__func__,retval);
		rmi4_data->sensor_sleep = false;
		return;
	} else {
		rmi4_data->sensor_sleep = true;
	}

	if (device_may_wakeup(&rmi4_data->input_dev->dev)) 
	{
		tp_log_debug( "%s Device may wakeup\n", __func__);
		if (!enable_irq_wake(rmi4_data->irq))
		{
			tp_log_debug( "%s enable irq wake!\n", __func__);
			synaptics_rmi4_irq_enable(rmi4_data, true);
			rmi4_data->sleep_gesture_flag = true;
			rmi4_data->touch_stopped = false;
			tp_log_debug( "%s irq enableed!\n", __func__);
		}
	} else {
		tp_log_debug( "%s enable irq wake fail!gesture_enabled=%d\n", __func__,rmi4_data->gesture_enabled);
	}

	return;
}

static void synaptics_put_device_outof_easy_wakeup(struct device *dev,struct synaptics_rmi4_data *rmi4_data)
{
	int retval;
	unsigned char device_ctrl;

	tp_log_info("%s: in ",__func__);
	if (false == rmi4_data->sleep_gesture_flag)
	{
		tp_log_debug( "%s gesture already disabled,return directly!\n", __func__);
		return;
	}
	retval = synaptics_rmi4_i2c_read(rmi4_data,
			rmi4_data->f11_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	tp_log_debug( "%s f11_ctrl_base_addr = %d\n", __func__,rmi4_data->f11_ctrl_base_addr);
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to read reg,retval=%d\n",
				__func__,retval);
		rmi4_data->sensor_sleep = true;
		return;
	}

	tp_log_debug("%s(line%d)Read device_ctrl=%d\n",__func__,__LINE__,device_ctrl);
	/*write 0000 to f11_ctrl_00 will get into normal mode*/
	device_ctrl = (device_ctrl & ~MASK_3BIT);

	retval = synaptics_rmi4_i2c_write(rmi4_data,
			rmi4_data->f11_ctrl_base_addr,
			&device_ctrl,
			sizeof(device_ctrl));
	if (retval < 0) {
		dev_err(&(rmi4_data->input_dev->dev),
				"%s: Failed to write reg,retval=%d\n",
				__func__,retval);
		rmi4_data->sensor_sleep = true;
		return;
	} else {
		rmi4_data->sensor_sleep = false;
	}
	tp_log_debug("%s(line%d)Write device_ctrl=%d\n",__func__,__LINE__,device_ctrl);

	if (device_may_wakeup(&rmi4_data->input_dev->dev)) 
	{
		tp_log_debug( "%s Device may wakeup\n", __func__);
		if (!disable_irq_wake(rmi4_data->irq))
		{
			tp_log_debug( "%s disable irq wake!\n", __func__);
			rmi4_data->sleep_gesture_flag = false;
			synaptics_rmi4_irq_enable(rmi4_data, false);
			tp_log_debug( "%s set gesture_enabled=%d\n", __func__,rmi4_data->gesture_enabled);
		}
	} else {
		tp_log_debug( "%s disable irq wake fail!gesture_enabled=%d\n", __func__,rmi4_data->gesture_enabled);
	}

	return;
}

static void synaptics_put_device_into_sleep(struct device *dev,struct synaptics_rmi4_data *rmi4_data)
{
	if (true == rmi4_data->gesture_enabled) {
		synaptics_put_device_into_easy_wakeup(dev,rmi4_data);
	}
	else{
		synaptics_rmi4_sensor_sleep(rmi4_data);
	}

	return;
}

static void synaptics_put_device_into_wake(struct device *dev,struct synaptics_rmi4_data *rmi4_data)
{
	if (true == rmi4_data->gesture_enabled) {
		synaptics_put_device_outof_easy_wakeup(dev,rmi4_data);
	}
	else{
		synaptics_rmi4_sensor_wake(rmi4_data);
	}

	return;
}

#endif /*USE_WAKEUP_GESTURE*/

#ifdef CONFIG_HAS_EARLYSUSPEND
 /**
 * synaptics_rmi4_early_suspend()
 *
 * Called by the kernel during the early suspend phase when the system
 * enters suspend.
 *
 * This function calls synaptics_rmi4_sensor_sleep() to stop finger
 * data acquisition and put the sensor to sleep.
 */
static void synaptics_rmi4_early_suspend(struct early_suspend *h)
{
	struct synaptics_rmi4_exp_fhandler *exp_fhandler;
	struct synaptics_rmi4_data *rmi4_data =
			container_of(h, struct synaptics_rmi4_data,
			early_suspend);

	if (rmi4_data->stay_awake) {
		rmi4_data->staying_awake = true;
		return;
	} else {
		rmi4_data->staying_awake = false;
	}

	rmi4_data->touch_stopped = true;
	synaptics_rmi4_irq_enable(rmi4_data, false);
	synaptics_rmi4_sensor_sleep(rmi4_data);
	synaptics_rmi4_free_fingers(rmi4_data);

	mutex_lock(&exp_data.mutex);
	if (!list_empty(&exp_data.list)) {
		list_for_each_entry(exp_fhandler, &exp_data.list, link)
			if (exp_fhandler->exp_fn->early_suspend != NULL)
				exp_fhandler->exp_fn->early_suspend(rmi4_data);
	}
	mutex_unlock(&exp_data.mutex);

	if (rmi4_data->full_pm_cycle)
		synaptics_rmi4_suspend(&(rmi4_data->input_dev->dev));

	return;
}

 /**
 * synaptics_rmi4_late_resume()
 *
 * Called by the kernel during the late resume phase when the system
 * wakes up from suspend.
 *
 * This function goes through the sensor wake process if the system wakes
 * up from early suspend (without going into suspend).
 */
static void synaptics_rmi4_late_resume(struct early_suspend *h)
{
	int retval;
	struct synaptics_rmi4_exp_fhandler *exp_fhandler;
	struct synaptics_rmi4_data *rmi4_data =
			container_of(h, struct synaptics_rmi4_data,
			early_suspend);

	if (rmi4_data->staying_awake)
		return;

	if (rmi4_data->full_pm_cycle)
		synaptics_rmi4_resume(&(rmi4_data->input_dev->dev));

	if (rmi4_data->sensor_sleep == true) {
		synaptics_rmi4_sensor_wake(rmi4_data);
		synaptics_rmi4_irq_enable(rmi4_data, true);
		retval = synaptics_rmi4_reinit_device(rmi4_data);
		if (retval < 0) {
			dev_err(&rmi4_data->i2c_client->dev,
					"%s: Failed to reinit device\n",
					__func__);
		}
	}

	mutex_lock(&exp_data.mutex);
	if (!list_empty(&exp_data.list)) {
		list_for_each_entry(exp_fhandler, &exp_data.list, link)
			if (exp_fhandler->exp_fn->late_resume != NULL)
				exp_fhandler->exp_fn->late_resume(rmi4_data);
	}
	mutex_unlock(&exp_data.mutex);

	rmi4_data->touch_stopped = false;

	return;
}
#endif

 /**
 * synaptics_rmi4_suspend()
 *
 * Called by the kernel during the suspend phase when the system
 * enters suspend.
 *
 * This function stops finger data acquisition and puts the sensor to
 * sleep (if not already done so during the early suspend phase),
 * disables the interrupt, and turns off the power to the sensor.
 */
static int synaptics_rmi4_suspend(struct device *dev)
{
	struct synaptics_rmi4_exp_fhandler *exp_fhandler;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	tp_log_info("%s: in \n",__func__);
	if (rmi4_data->staying_awake)
		return 0;

	if (!rmi4_data->sensor_sleep) {
		rmi4_data->touch_stopped = true;
		synaptics_rmi4_irq_enable(rmi4_data, false);
#if USE_WAKEUP_GESTURE
		synaptics_put_device_into_sleep(dev,rmi4_data);
#else
		synaptics_rmi4_sensor_sleep(rmi4_data);
#endif /*USE_WAKEUP_GESTURE*/
		synaptics_rmi4_free_fingers(rmi4_data);
	}

	mutex_lock(&exp_data.mutex);
	if (!list_empty(&exp_data.list)) {
		list_for_each_entry(exp_fhandler, &exp_data.list, link)
			if (exp_fhandler->exp_fn->suspend != NULL)
				exp_fhandler->exp_fn->suspend(rmi4_data);
	}
	mutex_unlock(&exp_data.mutex);

	if (rmi4_data->regulator)
		regulator_disable(rmi4_data->regulator);

	return 0;
}

 /**
 * synaptics_rmi4_resume()
 *
 * Called by the kernel during the resume phase when the system
 * wakes up from suspend.
 *
 * This function turns on the power to the sensor, wakes the sensor
 * from sleep, enables the interrupt, and starts finger data
 * acquisition.
 */
static int synaptics_rmi4_resume(struct device *dev)
{
	int retval = 0;
	struct synaptics_rmi4_exp_fhandler *exp_fhandler;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	const struct synaptics_dsx_platform_data *platform_data =
			rmi4_data->board;

	tp_log_info("%s: in ",__func__);
	if (rmi4_data->staying_awake)
		return 0;

	if (rmi4_data->regulator) {
		regulator_enable(rmi4_data->regulator);
		msleep(platform_data->reset_delay_ms);
		rmi4_data->current_page = MASK_8BIT;
	}

#if USE_WAKEUP_GESTURE
	synaptics_put_device_into_wake(dev,rmi4_data);
#else
	synaptics_rmi4_sensor_wake(rmi4_data);
#endif /*USE_WAKEUP_GESTURE*/
/*Move down*/
	retval = synaptics_rmi4_reinit_device(rmi4_data);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to reinit device\n",
				__func__);
		return retval;
	}

	mutex_lock(&exp_data.mutex);
	if (!list_empty(&exp_data.list)) {
		list_for_each_entry(exp_fhandler, &exp_data.list, link)
			if (exp_fhandler->exp_fn->resume != NULL)
				exp_fhandler->exp_fn->resume(rmi4_data);
	}
	mutex_unlock(&exp_data.mutex);

	rmi4_data->touch_stopped = false;
	synaptics_rmi4_irq_enable(rmi4_data, true);
	tp_log_info("%s: out\n",__func__);

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
 /**
 * synaptics_rmi4_rt_suspend()
 */
static int synaptics_rmi4_rt_suspend(struct device *dev)
{
	struct synaptics_rmi4_exp_fhandler *exp_fhandler;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);

	tp_log_info("%s: in \n",__func__);
	if (rmi4_data->staying_awake)
		return 0;

	if (!rmi4_data->sensor_sleep) {
		rmi4_data->touch_stopped = true;
		synaptics_rmi4_irq_enable(rmi4_data, false);
#if USE_WAKEUP_GESTURE
		synaptics_put_device_into_sleep(dev,rmi4_data);
#else
		synaptics_rmi4_sensor_sleep(rmi4_data);
#endif /*USE_WAKEUP_GESTURE*/
		synaptics_rmi4_free_fingers(rmi4_data);
	}

	mutex_lock(&exp_data.mutex);
	if (!list_empty(&exp_data.list)) {
		list_for_each_entry(exp_fhandler, &exp_data.list, link)
			if (exp_fhandler->exp_fn->suspend != NULL)
				exp_fhandler->exp_fn->suspend(rmi4_data);
	}
	mutex_unlock(&exp_data.mutex);

	if (rmi4_data->regulator)
		regulator_disable(rmi4_data->regulator);

	tp_log_info("%s: out\n",__func__);
	return 0;
}

 /**
 * synaptics_rmi4_rt_resume()
 */
static int synaptics_rmi4_rt_resume(struct device *dev)
{
	int retval = 0;
	struct synaptics_rmi4_exp_fhandler *exp_fhandler;
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(dev);
	const struct synaptics_dsx_platform_data *platform_data =
			rmi4_data->board;

	tp_log_info("%s: in ",__func__);
	if (rmi4_data->staying_awake)
		return 0;

	if (rmi4_data->regulator) {
		regulator_enable(rmi4_data->regulator);
		msleep(platform_data->reset_delay_ms);
		rmi4_data->current_page = MASK_8BIT;
	}

#if USE_WAKEUP_GESTURE
	synaptics_put_device_into_wake(dev,rmi4_data);
#else
	synaptics_rmi4_sensor_wake(rmi4_data);
#endif /*USE_WAKEUP_GESTURE*/
	retval = synaptics_rmi4_reinit_device(rmi4_data);
	if (retval < 0) {
		dev_err(&rmi4_data->i2c_client->dev,
				"%s: Failed to reinit device\n",
				__func__);
		return retval;
	}

	mutex_lock(&exp_data.mutex);
	if (!list_empty(&exp_data.list)) {
		list_for_each_entry(exp_fhandler, &exp_data.list, link)
			if (exp_fhandler->exp_fn->resume != NULL)
				exp_fhandler->exp_fn->resume(rmi4_data);
	}
	mutex_unlock(&exp_data.mutex);

	rmi4_data->touch_stopped = false;
	synaptics_rmi4_irq_enable(rmi4_data, true);
	tp_log_info("%s: out\n",__func__);

	return 0;
}
#endif/*CONFIG_PM_RUNTIME*/

#ifdef CONFIG_PM_RUNTIME
static const struct dev_pm_ops synaptics_rmi4_dev_pm_rt_ops = {
	SET_RUNTIME_PM_OPS(synaptics_rmi4_rt_suspend, synaptics_rmi4_rt_resume,NULL)
};
#elif CONFIG_PM_SLEEP
static const struct dev_pm_ops synaptics_rmi4_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(synaptics_rmi4_suspend, synaptics_rmi4_resume)
};
#endif/*CONFIG_PM_RUNTIME*/
#endif


static const struct i2c_device_id synaptics_rmi4_id_table[] = {
	{DRIVER_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, synaptics_rmi4_id_table);

#ifdef CONFIG_OF
static struct of_device_id synaptics_match_table[] = {
	{ .compatible = "synaptics,dsx",},
	{ },
};
#else
#define synaptics_match_table NULL
#endif

static struct i2c_driver synaptics_rmi4_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = synaptics_match_table,
#ifdef CONFIG_PM
#ifdef CONFIG_PM_RUNTIME
		.pm = &synaptics_rmi4_dev_pm_rt_ops,
#elif CONFIG_PM_SLEEP
		.pm = &synaptics_rmi4_dev_pm_ops,
#endif/*CONFIG_PM_RUNTIME*/
#endif
	},
	.probe = synaptics_rmi4_probe,
	.remove = __devexit_p(synaptics_rmi4_remove),
	.id_table = synaptics_rmi4_id_table,
};

 /**
 * synaptics_rmi4_init()
 *
 * Called by the kernel during do_initcalls (if built-in)
 * or when the driver is loaded (if a module).
 *
 * This function registers the driver to the I2C subsystem.
 *
 */
static int __init synaptics_rmi4_init(void)
{
	return i2c_add_driver(&synaptics_rmi4_driver);
}

 /**
 * synaptics_rmi4_exit()
 *
 * Called by the kernel when the driver is unloaded.
 *
 * This funtion unregisters the driver from the I2C subsystem.
 *
 */
static void __exit synaptics_rmi4_exit(void)
{
	i2c_del_driver(&synaptics_rmi4_driver);
#if	USE_IRQ_THREAD //huawei 11-25
#else		
	if (synaptics_wq)
		destroy_workqueue(synaptics_wq);
#endif /*USE_IRQ_THREAD*/

	return;
}

module_init(synaptics_rmi4_init);
module_exit(synaptics_rmi4_exit);

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics DSX I2C Touch Driver");
MODULE_LICENSE("GPL v2");
