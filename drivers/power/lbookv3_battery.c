/************************************************************************
 * drivers/power/lbookv3_battery.c					*
 * Battery driver for lbook v3						*
 *									*
 * Author: Piter Konstantinov <pit.here@gmail.com>			*
 *									*
 * Based on code for Palm TX by						*
 *									*
 * Authors: Jan Herman <2hp@seznam.cz>					*
 *  									*
 ************************************************************************/

 
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/apm-emulation.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <asm/arch/gpio.h>
#include <asm/io.h>
#include <asm/arch/io.h>
#include <asm/mach/map.h>

/* connected to AO pin of LTC3455 */
#define LBOOK_V3_BAT_LOWBAT_PIN		S3C2410_GPG2
/* 1 --- connect the PROG pin of LTC3455 to resistor */
#define LBOOK_V3_BAT_ENABLE_CHRG_PIN	S3C2410_GPC0
/* connected to nCHRG pin of LTC3455 */
#define LBOOK_V3_BAT_CHRG_PIN		S3C2410_GPG1

struct lbookv3_battery_dev {
	int battery_registered;
	int current_voltage;
};

struct lbookv3_battery_dev dev_info;
static void __iomem *adc_base;
static struct clk* adc_clk;

#if defined(CONFIG_APM_EMULATION) || defined(CONFIG_APM_MODULE)
/* original APM hook */
static void (*apm_get_power_status_orig)(struct apm_power_info *info);
#endif

#define ADC_BATTERY_CH 1
#define LBOOK_V3_MAX_VOLT 4050
#define LBOOK_V3_MIN_VOLT 3000

static unsigned int adc_get_val (unsigned int ch)
{
	int wait = 0xffff;
	ch &= 0x07;
	ch <<= 3;
	__raw_writel(0x4c41 | ch, adc_base);
	while(((__raw_readl(adc_base) & 0x8000) == 0) && (--wait != 0));
	if(wait == 0)
		return 0;
	else
		return (__raw_readl(adc_base+0xc) & 0x3ff);
}

static int lbookv3_battery_get_voltage(struct power_supply *b)
{
	unsigned int adc_data;
	if (dev_info.battery_registered)
	{
		adc_data = adc_get_val(ADC_BATTERY_CH);
		if(adc_data == 0)
		{
			printk(KERN_DEBUG "lbookv3_battery: cannot get voltage -> ADC timeout\n");
			return 0;
		}
		printk(KERN_DEBUG "lbookv3_battery: adc read %d\n", adc_data);
		dev_info.current_voltage = (adc_data * 1750 * 3300) / 1024000;
		return dev_info.current_voltage;
	} else {
		printk(KERN_DEBUG "lbookv3_battery: cannot get voltage -> battery driver unregistered\n");
		return 0;
	}
}

static int lbookv3_battery_get_capacity(struct power_supply *b)
{
	if (dev_info.battery_registered) {
		return ((lbookv3_battery_get_voltage(b)-LBOOK_V3_MIN_VOLT) * 100)/(LBOOK_V3_MAX_VOLT-LBOOK_V3_MIN_VOLT);
	} else {
		printk(KERN_DEBUG "lbookv3_battery: cannot get capacity -> battery driver unregistered\n");
		return 0;
	}
}

static int lbookv3_battery_charging (void)
{
	if (dev_info.battery_registered) {
		return (gpio_get_value(LBOOK_V3_BAT_CHRG_PIN) == 0 ? 1 : 0);
	} else {
		printk(KERN_DEBUG "lbookv3_battery: cannot get status -> battery driver unregistered\n");
		return 0;
	}

}

static int lbookv3_usb_connected (void)
{
	if (dev_info.battery_registered) {
		return gpio_get_value(S3C2410_GPF4);
	} else	{
		printk(KERN_DEBUG "lbookv3_battery: cannot get status -> battery driver unregistered\n");
		return 0;
	}
}

static int lbookv3_battery_get_status(struct power_supply *b)
{
	if (lbookv3_battery_charging())
		return POWER_SUPPLY_STATUS_CHARGING;
	else
		if (lbookv3_usb_connected())
			return POWER_SUPPLY_STATUS_NOT_CHARGING;
		else
			return POWER_SUPPLY_STATUS_DISCHARGING;
}

static enum power_supply_property lbookv3_battery_props[] = 
{
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
};

static int lbookv3_battery_get_property	(struct power_supply *b,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	switch (psp) 
	{
		case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
			val->intval = LBOOK_V3_MAX_VOLT;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
			val->intval = LBOOK_V3_MIN_VOLT;
			break;
		case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
			val->intval = 100;
			break;
		case POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN:
			val->intval = 0;
			break;
		case POWER_SUPPLY_PROP_CHARGE_NOW:
			val->intval = lbookv3_battery_get_capacity(b);
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			val->intval = lbookv3_battery_get_voltage(b);
			break;
		case POWER_SUPPLY_PROP_STATUS:
			val->intval = lbookv3_battery_get_status(b);
			break;
		default:
			break;
	}
	return 0;
}

struct power_supply lbookv3_battery = 
{
	.name			= "lbookv3_battery",
	.get_property   = lbookv3_battery_get_property,
	.properties     = lbookv3_battery_props,
	.num_properties = ARRAY_SIZE(lbookv3_battery_props),
};

#if defined(CONFIG_APM_EMULATION) || defined(CONFIG_APM_MODULE)
/* APM status query callback implementation */
static void lbookv3_apm_get_power_status(struct apm_power_info *info)
{
	int min, max, curr, percent;

	curr = lbookv3_battery_get_voltage(&lbookv3_battery);
	min  = LBOOK_V3_MIN_VOLT;
	max  = LBOOK_V3_MAX_VOLT;

	curr = curr - min;
	if (curr < 0) curr = 0;
	max = max - min;

	percent = curr*100/max;

	info->battery_life = percent;

	info->ac_line_status = lbookv3_usb_connected() ? APM_AC_ONLINE : APM_AC_OFFLINE;

	if (lbookv3_battery_charging())
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
}
#endif

static int s3c2410_adc_init(void)
{
	int err = 0;
	adc_base = __arm_ioremap(0x58000000, 0x00100000, MT_DEVICE);
	adc_clk = clk_get(NULL, "adc");
	if(IS_ERR(adc_clk))
	{
		printk(KERN_ALERT "lbookv3_battery: adc clock get failed\n");
		goto err1;
	}

	err = clk_enable(adc_clk);
	if(err != 0)
	{
		printk(KERN_ALERT "lbookv3_battery: adc clock enable failed\n");
		goto err2;
	}

	__raw_writel(0x00, adc_base + 0x04);
	__raw_writel(0x00, adc_base + 0x08);
	return 0;
err2:	
	clk_put(adc_clk);
err1:
	return err;

}

static int lbookv3_battery_probe(struct platform_device *dev)
{
	return 0;
}

static int lbookv3_battery_remove(struct platform_device *dev)
{
	return 0;
	/*
#if defined(CONFIG_APM_EMULATION) || defined(CONFIG_APM_MODULE)
apm_get_power_status = apm_get_power_status_orig;
#endif
*/
}

static struct platform_driver lbookv3_battery_driver = {
	.driver = 
	{
		.name = "lbookv3-battery",
	},
	.probe = lbookv3_battery_probe,
	.remove = lbookv3_battery_remove,
};

static int __init lbookv3_battery_init(void)
{
	int ret;
	ret = s3c2410_adc_init();
	if(ret != 0)
		goto err1;

	gpio_direction_input(S3C2410_GPF4);	//USB_pin
	gpio_direction_input(LBOOK_V3_BAT_CHRG_PIN);	//nCHRG
	gpio_direction_input(LBOOK_V3_BAT_LOWBAT_PIN);	//nLBO
	gpio_direction_output(LBOOK_V3_BAT_ENABLE_CHRG_PIN, 1);	//PROG - auto charge, when pulled high
	s3c2410_gpio_setpin(LBOOK_V3_BAT_ENABLE_CHRG_PIN, 1);

	/* register battery to APM layer */
	dev_info.battery_registered = 0;
	ret = power_supply_register(NULL, &lbookv3_battery);
	if(ret != 0)
	{
		printk(KERN_ERR "lbookv3_battery: could not register battery class\n");
		goto err2;
	}
	else 
	{
		dev_info.battery_registered = 1;
		printk(KERN_DEBUG "blbookv3_battery: battery registered\n");
	}
#if defined(CONFIG_APM_EMULATION) || defined(CONFIG_APM_MODULE)
	apm_get_power_status_orig = apm_get_power_status;
	apm_get_power_status = lbookv3_apm_get_power_status;
#endif
	ret = platform_driver_register(&lbookv3_battery_driver);
	if(ret != 0)
	{
		printk(KERN_ERR "lbookv3_battery: could not register battery platform driver\n");
		goto err3;
	}
	return ret;
err3:
#if defined(CONFIG_APM_EMULATION) || defined(CONFIG_APM_MODULE)
	apm_get_power_status = apm_get_power_status_orig;
#endif
	power_supply_unregister(&lbookv3_battery);	
err2:
	clk_disable(adc_clk);
	clk_put(adc_clk);
err1:
	return ret;
}

static void __exit lbookv3_battery_exit(void)
{
#if defined(CONFIG_APM_EMULATION) || defined(CONFIG_APM_MODULE)
	apm_get_power_status = apm_get_power_status_orig;
#endif
	platform_driver_unregister(&lbookv3_battery_driver);
	power_supply_unregister(&lbookv3_battery);
	clk_disable(adc_clk);
	clk_put(adc_clk);
}

module_init(lbookv3_battery_init);
module_exit(lbookv3_battery_exit);

/* Module information */
MODULE_AUTHOR("Piter Konstantinov <pit.here@gmail.com>");
MODULE_DESCRIPTION("Battery driver for lbook v3");
MODULE_LICENSE("GPL");
