/*
 * RTC subsystem, save system time to RTC
 *
 * Copyright (C) 2008 Yauhen Kharuzhy
 * Author: Yauhen Kharuzhy <jekhor@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/rtc.h>

int rtc_systohc(unsigned long time)
{
	int err;
	struct rtc_time tm;
	struct rtc_device *rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);

	if (rtc == NULL) {
		printk("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return -ENODEV;
	}

	rtc_time_to_tm(time, &tm);
	err = rtc_set_time(rtc, &tm);
	if (err)
		dev_err(rtc->dev.parent,
				"systohc: unable to set clock\n");

	rtc_class_close(rtc);

	return err;
}
