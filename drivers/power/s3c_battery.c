/* 
 * Derived from
 *  lbookv3_battery by Piter Konstantinov <pit.here@gmail.com>,
 *  Palm TX battery driver by Jan Herman <2hp@seznam.cz> 
 */

MODULE_AUTHOR("Ondra Herman <xherman1@fi.muni.cz>");
MODULE_DESCRIPTION("Generic S3C24XX ADC battery driver");
MODULE_LICENSE("GPL");

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/apm-emulation.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/s3c_battery.h>

#include <asm/arch/gpio.h>
#include <asm/io.h>
#include <asm/arch/io.h>
#include <asm/mach/map.h>
#include <asm/plat-s3c/regs-adc.h>

struct s3c_battery_device_info {
	struct device *dev;

	int current_voltage;
	void __iomem *adc_base;
	struct clk *adc_clk;
	int adc_channel;
	int min_voltage;
	int max_voltage;

	unsigned powered_gpio;
	bool gpio_active_low;

	struct power_supply batt;
};

#define to_s3c_battery_di(x) container_of((x), struct s3c_battery_device_info, \
	batt);

static unsigned int adc_get_val (struct s3c_battery_device_info *di)
{
	int wait = 0xffff;
	int ret, adccon;
	//__raw_writel(0x4c41, adc_base);

	ret = clk_enable (di->adc_clk);

	if (ret)
	{
		printk (KERN_ERR "s3c_battery: clk_enable failed\n");
		return 0;
	}

	adccon = 0; // readl (di->adc_base + S3C2410_ADCCON);

	adccon &= ~S3C2410_ADCCON_PRSCVLMASK;
	adccon |= S3C2410_ADCCON_PRSCVL(31);

	adccon &= ~S3C2410_ADCCON_MUXMASK;
	adccon |= S3C2410_ADCCON_SELMUX(di->adc_channel);
	
	adccon |= S3C2410_ADCCON_PRSCEN;
	adccon |= S3C2410_ADCCON_ENABLE_START;

	writel (adccon, di->adc_base + S3C2410_ADCCON);

	while (!(readl (di->adc_base + S3C2410_ADCCON) & S3C2410_ADCCON_ECFLG) && (--wait));

	clk_disable (di->adc_clk);

	if (!wait) {
		printk (KERN_ERR "s3c_battery: ADC timed out\n");
		return 0;
	}
	else return (readl(di->adc_base + S3C2410_ADCDAT0) & S3C2410_ADCDAT0_XPDATA_MASK);

}

static int s3c_battery_get_voltage(struct power_supply *b)
{
	unsigned int adc_data;
	struct s3c_battery_device_info *di = to_s3c_battery_di(b);

	adc_data = adc_get_val(di);

	printk(KERN_DEBUG "s3c_battery: adc read %d\n", adc_data);
	di->current_voltage = (adc_data * 1750 * 3300) / 1024000;
	return di->current_voltage;
}

static int s3c_battery_get_capacity(struct power_supply *b)
{
	struct s3c_battery_device_info *di = to_s3c_battery_di(b);

	return ((s3c_battery_get_voltage(b) - di->min_voltage) * 100)/
		(di->max_voltage - di->min_voltage);
}

static int s3c_battery_get_status(struct power_supply *b)
{
	struct s3c_battery_device_info *di = to_s3c_battery_di(b);
	if (gpio_get_value (di->powered_gpio) ^ di->gpio_active_low) {
		if (s3c_battery_get_capacity (b) >= 100)
			return POWER_SUPPLY_STATUS_NOT_CHARGING;
		else return POWER_SUPPLY_STATUS_CHARGING;
	}
	else return POWER_SUPPLY_STATUS_DISCHARGING;
}

static enum power_supply_property s3c_battery_props[] = 
{
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
};

static int s3c_battery_get_property	(struct power_supply *b,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct s3c_battery_device_info *di = to_s3c_battery_di(b);

	switch (psp) 
	{
		case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
			val->intval = di->max_voltage;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
			val->intval = di->min_voltage;
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval = s3c_battery_get_capacity(b);
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			val->intval = s3c_battery_get_voltage(b);
			break;
		case POWER_SUPPLY_PROP_STATUS:
			val->intval = s3c_battery_get_status(b);
			break;
		default:
			break;
	}
	return 0;
}


#if defined(CONFIG_APM_EMULATION) || defined(CONFIG_APM_MODULE)
/* APM status query callback implementation */
/*static void s3c_adc_apm_get_power_status(struct apm_power_info *info)
{
	int min, max, curr, percent;

	curr = s3c_adc_battery_get_voltage(&s3c_adc_battery);
	min  = LBOOK_V3_MIN_VOLT;
	max  = LBOOK_V3_MAX_VOLT;

	curr = curr - min;
	if (curr < 0) curr = 0;
	max = max - min;

	percent = curr*100/max;

	info->battery_life = percent;

	info->ac_line_status = s3c_adc_usb_connected() ? APM_AC_ONLINE : APM_AC_OFFLINE;

	if (s3c_adc_battery_charging())
		info->battery_status = APM_BATTERY_STATUS_CHARGING;
	else
	{
		if (percent > 50)
			info->battery_status = APM_BATTERY_STATUS_HIGH;
		else if (percent < 5)
			info->battery_status = APM_BATTERY_STATUS_CRITICAL;
		else
			info->battery_status = APM_BATTERY_STATUS_LOW;
	}

	info->time = 0;
	info->units = APM_UNITS_UNKNOWN;
}*/
#endif

static int s3c2410_adc_init(struct s3c_battery_device_info *di)
{
	di->adc_base = __arm_ioremap (0x58000000, 0x00100000, MT_DEVICE);
	di->adc_clk = clk_get (NULL, "adc");
	if(IS_ERR(di->adc_clk))
	{
		printk(KERN_ALERT "s3c_battery: adc clock get failed\n");
		return (int) di->adc_clk;
	}

	writel (0, di->adc_base + S3C2410_ADCTSC);
	writel (0, di->adc_base + S3C2410_ADCDLY);
	
	return 0;
}

static int __devinit  s3c_battery_probe (struct platform_device *pdev)
{
	int ret;
	struct s3c_battery_device_info *di;
	struct s3c_battery_platform_data *pdata;

	di = kzalloc(sizeof(*di), GFP_KERNEL);
	if (!di) 
		return -ENOMEM;


	platform_set_drvdata (pdev, di);

	pdata = pdev->dev.platform_data;
	di->dev			= &pdev->dev;
	di->adc_channel 	= pdata->adc_channel;
	di->min_voltage		= pdata->min_voltage;
	di->max_voltage		= pdata->max_voltage;
	di->gpio_active_low 	= pdata->gpio_active_low;
	di->batt.name		= pdev->dev.bus_id;
	di->batt.type		= POWER_SUPPLY_TYPE_BATTERY;
	di->batt.properties	= s3c_battery_props;
	di->batt.num_properties	= ARRAY_SIZE(s3c_battery_props);
	di->batt.get_property	= s3c_battery_get_property;
	di->batt.use_for_apm	= 1;
	di->powered_gpio = pdata->powered_gpio;

	ret = s3c2410_adc_init(di);
	if (ret)
		goto err1;

	gpio_direction_input(di->powered_gpio);

	ret = power_supply_register(&pdev->dev, &di->batt);
	if(ret != 0)
	{
		printk(KERN_ERR "s3c_battery: could not register battery class\n");
		goto err2;
	}

	return ret;
err2:
	clk_disable (di->adc_clk);
	clk_put (di->adc_clk);
err1:
	kfree (di);
	return ret;
}

static int s3c_battery_remove(struct platform_device *pdev)
{
	struct s3c_battery_device_info *di = platform_get_drvdata(pdev);

	power_supply_unregister(&di->batt);
	clk_disable(di->adc_clk);
	clk_put(di->adc_clk);
	kfree (di);

	return 0;
}

static struct platform_driver s3c_battery_driver = {
	.driver = 
	{
		.name = "s3c_battery",
	},
	.probe = s3c_battery_probe,
	.remove = s3c_battery_remove,
};

static int __init s3c_battery_init(void)
{
	return platform_driver_register(&s3c_battery_driver);
}

static void __exit s3c_battery_exit(void)
{
	platform_driver_unregister(&s3c_battery_driver);
}

module_init(s3c_battery_init);
module_exit(s3c_battery_exit);

