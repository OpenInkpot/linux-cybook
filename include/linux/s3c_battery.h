#ifndef __S3C_BATTERY_H_
#define __S3C_BATTERY_H_

struct s3c_battery_platform_data {
	int adc_channel;
	int min_voltage;
	int max_voltage;
	unsigned powered_gpio;
	bool gpio_active_low;
};

#endif

