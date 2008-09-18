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
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>

#include <mach/regs-gpio.h>
#include <mach/io.h>
#include <asm/mach/map.h>
#include <asm/io.h>

/* connected to AO pin of LTC3455 */
#define LBOOK_V3_BAT_LOWBAT_PIN		S3C2410_GPG2
#define LBOOK_V3_BAT_LOWBAT_PIN_INP	S3C2410_GPG2_INP
/* 1 --- connect the PROG pin of LTC3455 to resistor */
#define LBOOK_V3_BAT_ENABLE_CHRG_PIN	S3C2410_GPC0
#define LBOOK_V3_BAT_ENABLE_CHRG_PIN_OUTP	S3C2410_GPC0_OUTP
/* connected to nCHRG pin of LTC3455 */
#define LBOOK_V3_BAT_CHRG_PIN		S3C2410_GPG1
#define LBOOK_V3_BAT_CHRG_PIN_INP		S3C2410_GPG1_INP

static void __iomem *adc_base;
static struct clk* adc_clk;

#define ADC_BATTERY_CH 1
#define LBOOK_V3_MAX_VOLT 4050
#define LBOOK_V3_MIN_VOLT 3150
#define LBOOK_V3_5PERC_VOLT 3540

static unsigned int adc_get_val (unsigned int ch)
{
	int wait = 0xffff;
	ch &= 0x07;
	ch <<= 3;
	__raw_writel(0x4c41 | ch, adc_base);
	while (((__raw_readl(adc_base) & 0x8000) == 0) && (--wait != 0));
	if (wait == 0)
		return 0;
	else
		return (__raw_readl(adc_base+0xc) & 0x3ff);
}

static int lbookv3_battery_get_voltage(struct power_supply *b)
{
	unsigned int adc_data;

	adc_data = adc_get_val(ADC_BATTERY_CH);
	if (adc_data == 0) {
		printk(KERN_DEBUG "lbookv3_battery: cannot get voltage -> ADC timeout\n");
		return 0;
	}

	return (adc_data * 5861) / 1000;
}

static int lbookv3_battery_get_capacity(struct power_supply *b)
{
	unsigned int voltage;

	voltage = lbookv3_battery_get_voltage(b);

	if (voltage > LBOOK_V3_5PERC_VOLT)
		return ((voltage - LBOOK_V3_5PERC_VOLT) * 95)/(LBOOK_V3_MAX_VOLT-LBOOK_V3_5PERC_VOLT);
	else
		return ((voltage - LBOOK_V3_MIN_VOLT) * 5) / (LBOOK_V3_MAX_VOLT - LBOOK_V3_MIN_VOLT);

}

static int lbookv3_battery_charging (void)
{
	return s3c2410_gpio_getpin(LBOOK_V3_BAT_CHRG_PIN) == 0 ? 1 : 0;
}

static int lbookv3_usb_connected (void)
{
	return s3c2410_gpio_getpin(S3C2410_GPF4) ? 1 : 0;
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
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
};

static int lbookv3_battery_get_property	(struct power_supply *b,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = LBOOK_V3_MAX_VOLT;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = LBOOK_V3_MIN_VOLT;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = lbookv3_battery_get_capacity(b);
		if (val->intval > 100)
			val->intval = 100;
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

static int lbookv3_usb_get_property (struct power_supply *b,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = lbookv3_usb_connected();
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

struct power_supply lbookv3_battery =
{
	.name		= "lbookv3_battery",
	.get_property   = lbookv3_battery_get_property,
	.properties     = lbookv3_battery_props,
	.num_properties = ARRAY_SIZE(lbookv3_battery_props),
};

static char *lbookv3_power_supplied_to[] = {
	"lbookv3_battery",
};

static enum power_supply_property lbookv3_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

struct power_supply lbookv3_usb = {
	.name		= "usb",
	.type		= POWER_SUPPLY_TYPE_USB,
	.supplied_to	= lbookv3_power_supplied_to,
	.num_supplicants = ARRAY_SIZE(lbookv3_power_supplied_to),
	.properties	= lbookv3_usb_props,
	.num_properties = ARRAY_SIZE(lbookv3_usb_props),
	.get_property	= lbookv3_usb_get_property,

};

static irqreturn_t lbookv3_usb_change_irq(int irq, void *dev)
{
	printk(KERN_DEBUG "USB change irq: usb cable has been %s\n",
			lbookv3_usb_connected() ? "connected" : "disconnected");

	power_supply_changed(&lbookv3_usb);

	return IRQ_HANDLED;
}

static irqreturn_t lbookv3_battery_change_irq(int irq, void *dev)
{
	printk(KERN_DEBUG "battery change irq: IRQ %u was raised\n", irq);

	power_supply_changed(&lbookv3_battery);

	return IRQ_HANDLED;
}

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
	int ret;
	int irq;

	ret = s3c2410_adc_init();
	if(ret != 0)
		goto err1;

	s3c2410_gpio_cfgpin(S3C2410_GPF4, S3C2410_GPF4_INP);	//USB_pin
	s3c2410_gpio_cfgpin(LBOOK_V3_BAT_CHRG_PIN, LBOOK_V3_BAT_CHRG_PIN_INP);	//nCHRG
	s3c2410_gpio_cfgpin(LBOOK_V3_BAT_LOWBAT_PIN, LBOOK_V3_BAT_LOWBAT_PIN_INP);	//nLBO
	s3c2410_gpio_cfgpin(LBOOK_V3_BAT_ENABLE_CHRG_PIN, LBOOK_V3_BAT_ENABLE_CHRG_PIN_OUTP);	//PROG - auto charge, when pulled high
	s3c2410_gpio_setpin(LBOOK_V3_BAT_ENABLE_CHRG_PIN, 1);

	s3c2410_gpio_cfgpin(S3C2410_GPF4, S3C2410_GPF4_EINT4);

	ret = power_supply_register(NULL, &lbookv3_battery);
	if(ret != 0)
	{
		printk(KERN_ERR "lbookv3_battery: could not register battery class\n");
		goto err2;
	}

	ret = power_supply_register(NULL, &lbookv3_usb);
	if(ret) {
		printk(KERN_ERR "lbookv3_battery: could not register USB power supply\n");
		goto err_reg_usb;
	}

	irq = s3c2410_gpio_getirq(S3C2410_GPF4);
	ret = request_irq(irq, lbookv3_usb_change_irq,
			IRQF_DISABLED | IRQF_TRIGGER_RISING
			| IRQF_TRIGGER_FALLING | IRQF_SHARED,
			"usb power", &lbookv3_usb);

	if (ret) {
		printk(KERN_ERR "lbookv3_battery: could not request USB VBUS irq\n");
		goto err_usb_irq;
	}

	enable_irq_wake(irq);

	irq = s3c2410_gpio_getirq(LBOOK_V3_BAT_CHRG_PIN);
	ret = request_irq(irq, lbookv3_battery_change_irq,
			IRQF_TRIGGER_RISING
			| IRQF_TRIGGER_FALLING | IRQF_SHARED,
			"lbookv3-battery", &lbookv3_battery);

	if (ret) {
		printk(KERN_ERR "lbookv3_battery: could not request CHRG irq\n");
		goto err_chrg_irq;
	}

	enable_irq_wake(irq);

	irq = s3c2410_gpio_getirq(LBOOK_V3_BAT_LOWBAT_PIN);
	ret = request_irq(irq, lbookv3_battery_change_irq,
			IRQF_TRIGGER_FALLING | IRQF_SHARED,
			"lbookv3-battery", &lbookv3_battery);

	if (ret) {
		printk(KERN_ERR "lbookv3_battery: could not request LOWBAT irq\n");
		goto err_lowbat_irq;
	}

	enable_irq_wake(irq);

	return ret;

	free_irq(s3c2410_gpio_getirq(LBOOK_V3_BAT_LOWBAT_PIN), &lbookv3_battery);
err_lowbat_irq:
	free_irq(s3c2410_gpio_getirq(LBOOK_V3_BAT_CHRG_PIN), &lbookv3_battery);
err_chrg_irq:
	free_irq(s3c2410_gpio_getirq(S3C2410_GPF4), &lbookv3_usb);
err_usb_irq:
	power_supply_unregister(&lbookv3_usb);
err_reg_usb:
	power_supply_unregister(&lbookv3_battery);
err2:
	clk_disable(adc_clk);
	clk_put(adc_clk);
err1:
	return 0;
}

static int lbookv3_battery_remove(struct platform_device *dev)
{
	disable_irq_wake(s3c2410_gpio_getirq(LBOOK_V3_BAT_LOWBAT_PIN));
	disable_irq_wake(s3c2410_gpio_getirq(LBOOK_V3_BAT_CHRG_PIN));
	disable_irq_wake(s3c2410_gpio_getirq(S3C2410_GPF4));
	free_irq(s3c2410_gpio_getirq(LBOOK_V3_BAT_LOWBAT_PIN), &lbookv3_battery);
	free_irq(s3c2410_gpio_getirq(LBOOK_V3_BAT_CHRG_PIN), &lbookv3_battery);
	free_irq(s3c2410_gpio_getirq(S3C2410_GPF4), &lbookv3_usb);
	power_supply_unregister(&lbookv3_usb);
	power_supply_unregister(&lbookv3_battery);
	clk_disable(adc_clk);
	clk_put(adc_clk);

	return 0;
}

static struct platform_driver lbookv3_battery_driver = {
	.driver = {
		.name = "lbookv3-battery",
	},
	.probe = lbookv3_battery_probe,
	.remove = lbookv3_battery_remove,
};

static int __init lbookv3_battery_init(void)
{
	return platform_driver_register(&lbookv3_battery_driver);
}

static void __exit lbookv3_battery_exit(void)
{
	platform_driver_unregister(&lbookv3_battery_driver);
}

module_init(lbookv3_battery_init);
module_exit(lbookv3_battery_exit);

/* Module information */
MODULE_AUTHOR("Piter Konstantinov <pit.here@gmail.com>");
MODULE_AUTHOR("Yauhen Kharuzhy <jekhor@gmail.com>");
MODULE_DESCRIPTION("Battery driver for lbook v3");
MODULE_LICENSE("GPL");
