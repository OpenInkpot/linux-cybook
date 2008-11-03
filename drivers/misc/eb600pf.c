/* loosely modeled after msi-laptop.c in this directory */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/pm.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <linux/delay.h>

#include <mach/hardware.h>
#include <mach/usb-control.h>
#include <mach/regs-gpio.h>

#include <asm/gpio.h>
#include <asm/io.h>

MODULE_AUTHOR("Ondrej Herman <ondra.herman@gmail.com>");
MODULE_DESCRIPTION("EB600 platform control");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

static int expose_usb_po = 1;
module_param(expose_usb_po, bool, S_IRUGO);
MODULE_PARM_DESC(expose_usb_po, "1 to expose USB storage during shutdown");

static int expose_usb = 0;
module_param(expose_usb, bool, S_IRUGO);
MODULE_PARM_DESC(expose_usb, "1 to expose USB storage during boot");

struct eb600pf_gpio {
	int gpio;
	bool irq;
	bool input;
	char *desc;
};

#define CARD_SENSE 0
#define USB_SENSE 1
#define USB_POWER 2
#define USB_EXPORT 3
#define USB_RESET 4

struct announce_data {
	int irq;
	struct platform_device *pdev;
};

static struct announce_data isr_data;

struct eb600pf_gpio gpios[] = {
	[CARD_SENSE] = {
		.gpio	= S3C2410_GPG5,
		.irq	= true,
		.input	= true,
		.desc	= "card insertion detection",
	},
	[USB_SENSE] = {
		.gpio	= S3C2410_GPF1,
		.irq	= true,
		.input	= true,
		.desc	= "USB connection detection",
	},
	[USB_POWER] = {
		.gpio	= S3C2410_GPD8,
		.irq	= false,
		.input	= false,
	},
	[USB_EXPORT] = {
		.gpio	= S3C2410_GPD12,
		.irq	= false,
		.input	= false,
	},
	[USB_RESET] = {
		.gpio	= S3C2410_GPD11,
		.irq	= false,
		.input	= false,
	},
};

// GPD10 -> 0    - kills power

static void eb600pf_usb_reset (void)
{
	gpio_set_value (gpios[USB_RESET].gpio, 0);
	mdelay (200);
	gpio_set_value (gpios[USB_RESET].gpio, 1);
};

static void eb600pf_expose_usb (void)
{
	if (gpio_get_value (gpios[USB_SENSE].gpio)) {
		printk (KERN_INFO "eb600pf: NOT exposing mass storage device");
		return;
	}

	s3c2410_modify_misccr (S3C2410_MISCCR_USBSUSPND0, S3C2410_MISCCR_USBSUSPND0);
	gpio_set_value (gpios[USB_POWER].gpio, 0);
	gpio_set_value (gpios[USB_EXPORT].gpio, 0);
	mdelay (10);
	gpio_set_value (gpios[USB_POWER].gpio, 1);
		
	printk (KERN_INFO "eb600pf: Exposing mass storage device...");
	while (!gpio_get_value (gpios[USB_SENSE].gpio));
	printk (KERN_INFO "done!\n");

	gpio_set_value (gpios[USB_POWER].gpio, 0);
	gpio_set_value (gpios[USB_EXPORT].gpio, 1);
	mdelay (10);
	gpio_set_value (gpios[USB_POWER].gpio, 1);
	//gpio_set_value (gpios[USB_RESET].gpio, 0);

	s3c2410_modify_misccr (S3C2410_MISCCR_USBSUSPND0, 0);
};




static ssize_t show_usb_power (struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int val = !!gpio_get_value (gpios[USB_POWER].gpio);
	return sprintf (buf, "%i\n", val);
};

static ssize_t store_usb_power (struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int power;
	if (sscanf (buf, "%i", &power) != 1 || (power < 0 || power > 1))
		return -EINVAL;

	gpio_set_value (gpios[USB_POWER].gpio, power);
//	eb600pf_usbreset ();
	return count;
};




static ssize_t show_usb_export (struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int val = !!gpio_get_value (gpios[USB_EXPORT].gpio);
	return sprintf (buf, "%i\n", val);
};

static ssize_t store_usb_export (struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int export;
	if (sscanf (buf, "%i", &export) != 1 || (export < 0 || export > 1))
		return -EINVAL;

	s3c2410_modify_misccr (S3C2410_MISCCR_USBSUSPND0, export?
		0 : S3C2410_MISCCR_USBSUSPND0);

	gpio_set_value (gpios[USB_EXPORT].gpio, export);
	eb600pf_usb_reset ();
	return count;
};




static ssize_t show_card_sense (struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int val = !!gpio_get_value (gpios[CARD_SENSE].gpio);
	return sprintf (buf, "%i\n", val);
};

static ssize_t show_usb_sense (struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int val = !!gpio_get_value (gpios[USB_SENSE].gpio);
	return sprintf (buf, "%i\n", val);
};





static ssize_t store_enable (struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int num;
	if (sscanf (buf+1, "%i", &num) < 1 || (num < 0 || num > 15))
		return -EINVAL;
	
	gpio_direction_output ((buf[0]-'a')*32 + num, 1);
	return count;
};

static ssize_t store_disable (struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int num;
	if (sscanf (buf+1, "%i", &num) < 1 || (num < 0 || num > 15))
		return -EINVAL;
	
	gpio_direction_output ((buf[0]-'a')*32 + num, 0);
	return count;
};



static ssize_t show_expose_usb_po (struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf (buf, "%i\n", expose_usb_po);
};

static ssize_t store_expose_usb_po (struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int val;
	if (sscanf (buf, "%i", &val) != 1)
		return -EINVAL;

	expose_usb_po = val;
	return count;
};




DEVICE_ATTR(usb_power, 0644, show_usb_power, store_usb_power);
DEVICE_ATTR(usb_export, 0644, show_usb_export, store_usb_export);
DEVICE_ATTR(card_sense, 0444, show_card_sense, NULL);
DEVICE_ATTR(usb_sense, 0444, show_usb_sense, NULL);
DEVICE_ATTR(enable, 0644, NULL, store_enable);
DEVICE_ATTR(disable, 0644, NULL, store_disable);
DEVICE_ATTR(expose_usb_po, 0644, show_expose_usb_po, store_expose_usb_po);

static struct attribute *eb600pf_attributes[] = {
	&dev_attr_usb_power.attr,
	&dev_attr_usb_export.attr,
	&dev_attr_card_sense.attr,
	&dev_attr_usb_sense.attr,
	&dev_attr_enable.attr,
	&dev_attr_disable.attr,
	&dev_attr_expose_usb_po.attr,
	NULL,
};

struct attribute_group eb600pf_attribute_group = {
	.attrs = eb600pf_attributes,
};




#ifdef CONFIG_PM
static int eb600pf_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	if (device_may_wakeup(&pdev->dev)) {
		enable_irq_wake (gpio_to_irq (gpios[CARD_SENSE].gpio));
		enable_irq_wake (gpio_to_irq (gpios[USB_SENSE].gpio));
	}

	return 0;
};

static int eb600pf_resume(struct platform_device *pdev)
{
	if (device_may_wakeup(&pdev->dev)) {
		disable_irq_wake (gpio_to_irq (gpios[CARD_SENSE].gpio));
		disable_irq_wake (gpio_to_irq (gpios[USB_SENSE].gpio));
	}

        //s3c2410_gpio_setpin(S3C2410_GPD2, 1); // wake Apollo

	s3c2410_modify_misccr (S3C2410_MISCCR_USBSUSPND1, S3C2410_MISCCR_USBSUSPND1);
	s3c2410_modify_misccr (S3C2410_MISCCR_USBHOST, 0);

	return 0;
};
#else
#define eb600pf_suspend NULL
#define eb600pf_resume NULL
#endif




static void gen_events (struct work_struct *work)
{
	char *env[] = {NULL, NULL};
	
	if (isr_data.irq == gpio_to_irq (gpios[CARD_SENSE].gpio))
		env[0] = gpio_get_value (gpios[CARD_SENSE].gpio) ?
			"EVENT=card_insert" : "EVENT=card_remove";

	else if (isr_data.irq == gpio_to_irq (gpios[USB_SENSE].gpio))
		env[0] = gpio_get_value (gpios[USB_SENSE].gpio) ?
			"EVENT=usb_connect" : "EVENT=usb_disconnect";
	
	kobject_uevent_env (&isr_data.pdev->dev.kobj, KOBJ_CHANGE, env);
}


static irqreturn_t eb600pf_isr (int irq, void *dev_id)
{
	static bool initialised = false;
	static struct work_struct task;

	isr_data.irq = irq;
	isr_data.pdev = dev_id;
	
	if (initialised) {
		PREPARE_WORK(&task, gen_events);
	}
	else
	{
		INIT_WORK(&task, gen_events);
		initialised = true;
	}

	schedule_work (&task);

	return IRQ_HANDLED;
};


static int __devinit eb600pf_probe(struct platform_device *pdev)
{
	int ret, i, irq;

	for (i = 0; i < ARRAY_SIZE(gpios); i++) {
		ret = gpio_request (gpios[i].gpio, "eb600pf");
		if (ret) {
			printk (KERN_ERR "eb600pf: gpio_request failed\n");
			goto fail;
		}

		ret = gpios[i].input?	gpio_direction_input (gpios[i].gpio):
					gpio_direction_output (gpios[i].gpio, 1);

		if (ret) {
			printk (KERN_ERR "eb600pf: gpio_direction failed\n");
			goto fail;
		}
		
		if (!gpios[i].irq) continue;
		
		irq = gpio_to_irq (gpios[i].gpio);
		if (irq < 0) {
			ret = irq;
			printk (KERN_ERR "eb600pf: gpio_to_irq failed\n");
			goto fail;
		}
		
		ret = request_irq (irq, eb600pf_isr, IRQF_SAMPLE_RANDOM |
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			gpios[i].desc, pdev);

		if (ret) {
			printk (KERN_ERR "eb600pf: error %d; can't register "
				"irq %d\n", ret, irq);
			goto fail;
		}
	}

//	eb600pf_usbreset ();
	if (expose_usb) eb600pf_expose_usb();

	ret = sysfs_create_group (&pdev->dev.kobj, &eb600pf_attribute_group);
	if (ret) { 
		printk (KERN_ERR "eb600pf: cannot register sysfs controls\n");
		goto fail;
	}

	device_init_wakeup (&pdev->dev, 1);
	
	return 0;


fail:   for (i = 0; i < ARRAY_SIZE(gpios); i++) {
		free_irq (gpio_to_irq (gpios[i].gpio), pdev);
		gpio_free (gpios[i].gpio);
	}

	sysfs_remove_group (&pdev->dev.kobj, &eb600pf_attribute_group);
	device_init_wakeup (&pdev->dev, 0);

	return ret;
};

static int __devexit eb600pf_remove (struct platform_device *pdev)
{
	int i;

	device_init_wakeup (&pdev->dev, 0);

	for (i = 0; i < ARRAY_SIZE(gpios); i++)
	{
		free_irq (gpio_to_irq (gpios[i].gpio), pdev);
		gpio_free (gpios[i].gpio);
	}

	sysfs_remove_group (&pdev->dev.kobj, &eb600pf_attribute_group);
	
	return 0;
};

static void eb600pf_shutdown(struct platform_device *pdev)
{
	if (expose_usb_po)
		eb600pf_expose_usb();
};

struct platform_driver eb600pf_driver = {
	.probe		= eb600pf_probe,
	.remove		= __devexit_p (eb600pf_remove),
	.suspend	= eb600pf_suspend,
	.resume		= eb600pf_resume,
	.shutdown	= eb600pf_shutdown,
	.driver		= {
		.name		= "eb600pf",
	},
};

static int __init eb600pf_init(void)
{
	return platform_driver_register (&eb600pf_driver);
};


static void __exit eb600pf_exit(void)
{
	platform_driver_unregister (&eb600pf_driver);
};

module_init(eb600pf_init);
module_exit(eb600pf_exit);

