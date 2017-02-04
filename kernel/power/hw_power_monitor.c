/*
 * Copyright (c) Huawei Technologies Co., Ltd. 1998-2014. All rights reserved.
 *
 * File name: hw_power_monitor.c
 * Description: This file use to record power state for upper layer
 * Author: ivan.chengfeifei@huawei.com
 * Version: 0.1
 * Date:  2014/11/27
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/kobject.h>
#include <linux/debugfs.h>
#include <linux/hw_power_monitor.h>
#include "power.h"

unsigned int suspend_total = 0;
bool freezing_wakeup_check = false;
static struct power_monitor_info pm_info[POWER_MONITOR_MAX];
static DEFINE_MUTEX(power_monitor_mutex);


/*
 * Function: is_id_valid
 * Description: check ID valid or not
 * Input:  @id - id to check
 * Output:
 * Return: false -- invalid
 *         true -- valid
 */
static inline bool is_id_valid(int id)
{
	return (id >= AP_SLEEP && id < POWER_MONITOR_MAX);
}

/*
 * Function: report_handle
 * Description: Packet and Send data to power node
 * Input: id --- message mask
 *        fmt -- string
 * Return: -1--failed, 0--success
 */
static int report_handle(unsigned int id,  va_list args, const char *fmt)
{
	int length = 0;
	char buff[BUFF_SIZE] = {0};

	memset(&buff, 0, sizeof(buff));
	length = vscnprintf(buff, BUFF_SIZE - 1, fmt, args);
	if (length > 0) {
		length ++;
		pr_info("%s: id = %d length = %d buff = %s\n", __func__, id, length, buff);
	}

	if (BUFF_SIZE <= length) // or < ?
	{
		pr_err("%s: string too long!\n", __func__);
		return -ENAMETOOLONG;
	}

	mutex_lock(&power_monitor_mutex);
	pm_info[id].idx = id;
	pm_info[id].len = length;
	//pm_info[id].count ++;
	//strlcpy(pm_info[id].buffer, buff, sizeof(pm_info[id].buffer));
	mutex_unlock(&power_monitor_mutex);

	switch (id) {
	case AP_SLEEP:
		if (strncmp(buff, "[ap_sleep]:", 11) == 0){
			pm_info[id].count ++;
			strlcpy(pm_info[id].buffer, buff, sizeof(pm_info[id].buffer));
		}
		break;
	case MODEM_SLEEP:
		pm_info[id].count ++;
		strlcpy(pm_info[id].buffer, buff, sizeof(pm_info[id].buffer));
		break;
	case SUSPEND_FAILED:
		if (strncmp(buff, "[suspend_total]", 15) == 0){
			suspend_total ++;
		}
		else if (strncmp(buff, "[error_dev_name]:", 17) == 0){
			pm_info[id].count ++;
			strlcpy(pm_info[id].buffer, buff, sizeof(pm_info[id].buffer));
		}
		break;
	case FREEZING_FAILED:
		if (strncmp(buff, "[last_active_ws]:", 17) == 0){
			pm_info[id].count ++;
			strlcpy(pm_info[id].buffer, buff, sizeof(pm_info[id].buffer));
		}
		break;
	case WAKEUP_ABNORMAL:
		pm_info[id].count ++;
		strlcpy(pm_info[id].buffer, buff, sizeof(pm_info[id].buffer));
		break;
	case DRIVER_ABNORMAL:
		pm_info[id].count ++;
		strlcpy(pm_info[id].buffer, buff, sizeof(pm_info[id].buffer));
		break;
	default:
		break;
	}

	return 0;
}

/*
 * Function: power_monitor_report
 * Description: report data to power nodes
 * Input: id --- power radar nodes data struct
 *        fmt -- args from reported devices
 * Return: -x--failed, 0--success
 */
int power_monitor_report(unsigned int id, const char *fmt, ...)
{
	va_list args;
	int ret = -EINVAL;

	if (!is_id_valid(id)) {
		pr_err("%s: id %d is invalid!\n", __func__, id);
		return ret;
	}

	va_start(args, fmt);
	ret = report_handle(id, args, fmt);
	va_end(args);

	return ret;
}

EXPORT_SYMBOL(power_monitor_report);

static ssize_t ap_sleep_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	unsigned int id = AP_SLEEP;

	return sprintf(buf, "count:%8d %s\n", pm_info[id].count, pm_info[id].buffer);
}

static ssize_t ap_sleep_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	unsigned int size = 0;
	unsigned int id = AP_SLEEP;

	if (sscanf(buf, "%d", &size) == 1) {
		pm_info[id].count = size;
		return n;
	}

	return -EINVAL;
}

power_attr(ap_sleep);

static ssize_t modem_sleep_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	unsigned int id = MODEM_SLEEP;

	return sprintf(buf, "count:%8d %s\n", pm_info[id].count, pm_info[id].buffer);
}

static ssize_t modem_sleep_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	unsigned int size = 0;
	unsigned int id = MODEM_SLEEP;

	if (sscanf(buf, "%d", &size) == 1) {
		pm_info[id].count = size;
		return n;
	}

	return -EINVAL;
}

power_attr(modem_sleep);

static ssize_t suspend_failed_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	unsigned int id = SUSPEND_FAILED;

	return sprintf(buf, "count:%8d %s\n", pm_info[id].count, pm_info[id].buffer);
}

static ssize_t suspend_failed_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	unsigned int size = 0;
	unsigned int id = SUSPEND_FAILED;

	if (sscanf(buf, "%d", &size) == 1) {
		pm_info[id].count = size;
		return n;
	}

	return -EINVAL;
}

power_attr(suspend_failed);

static ssize_t freezing_failed_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	unsigned int id = FREEZING_FAILED;

	return sprintf(buf, "count:%8d %s\n", pm_info[id].count, pm_info[id].buffer);
}

static ssize_t freezing_failed_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	unsigned int size = 0;
	unsigned int id = FREEZING_FAILED;

	if (sscanf(buf, "%d", &size) == 1) {
		pm_info[id].count = size;
		return n;
	}

	return -EINVAL;
}

power_attr(freezing_failed);

static ssize_t wakeup_abnormal_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	unsigned int id = WAKEUP_ABNORMAL;

	return sprintf(buf, "count:%8d %s\n", pm_info[id].count, pm_info[id].buffer);
}

static ssize_t wakeup_abnormal_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	unsigned int size = 0;
	unsigned int id = WAKEUP_ABNORMAL;

	if (sscanf(buf, "%d", &size) == 1) {
		pm_info[id].count = size;
		return n;
	}

	return -EINVAL;
}

power_attr(wakeup_abnormal);

static ssize_t driver_abnormal_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	unsigned int id = DRIVER_ABNORMAL;

	return sprintf(buf, "count:%8d %s\n", pm_info[id].count, pm_info[id].buffer);
}

static ssize_t driver_abnormal_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	unsigned int size = 0;
	unsigned int id = DRIVER_ABNORMAL;

	if (sscanf(buf, "%d", &size) == 1) {
		pm_info[id].count = size;
		return n;
	}

	return -EINVAL;
}

power_attr(driver_abnormal);

static struct attribute * monitor_attrs[] = {
	&ap_sleep_attr.attr,
	&modem_sleep_attr.attr,
	&suspend_failed_attr.attr,
	&freezing_failed_attr.attr,
	&wakeup_abnormal_attr.attr,
	&driver_abnormal_attr.attr,
	NULL,
};

static struct attribute_group monitor_attr_group = {
	.name = "monitor", /* Directory of power monitor */
	.attrs = monitor_attrs,
};

static int __init power_monitor_init(void)
{
	int ret = -1;
	int i = 0, length = 0;

	/* power_kobj is created in kernel/power/main.c */
	if (!power_kobj){
		pr_err("%s: power_kobj is null!\n", __func__);
		return -ENOMEM;
	}

	/* Initialized struct data */
	length = sizeof(struct power_monitor_info);
	for (i = 0; i < POWER_MONITOR_MAX; i++) {
		memset(&pm_info[i], 0, length);
	}

	/* create all nodes under power sysfs */
	ret = sysfs_create_group(power_kobj, &monitor_attr_group);
	if (ret < 0) {
		pr_err("%s: sysfs_create_group power_kobj error\n", __func__);
	} else {
		pr_info("%s: sysfs_create_group power_kobj success\n", __func__);
	}

	return ret;
}

core_initcall(power_monitor_init);


