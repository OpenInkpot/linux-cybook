/*
 * Driver for lBook/Jinke eReader V3 keys
 *
 * Copyright 2008 Eugene Konev <ejka@imfi.kspu.ru>
 * Copyright 2008 Yauhen Kharuzhy <jekhor@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/version.h>

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/input.h>

#include <asm/gpio.h>
#include <asm/io.h>
#include <mach/hardware.h>
#include <mach/regs-gpio.h>

static int pullups[] = { 
	S3C2410_GPG6, S3C2410_GPG7, S3C2410_GPG8, S3C2410_GPG9, S3C2410_GPG10,
	S3C2410_GPG11, S3C2410_GPG4,
};

struct gpio_line {
	unsigned char state;
	unsigned char codes[7];
};

static struct gpio_line lines[3] = {
	[0] = {
		.codes = { KEY_KP1, KEY_KP7, KEY_KP4, KEY_KP0, KEY_KPPLUS,
			   KEY_ENTER, KEY_UP },
	},
	[1] = {
		.codes = { KEY_KP6, KEY_KP3, KEY_KP9, KEY_RESERVED, KEY_KPMINUS,
			   KEY_ESC, KEY_DOWN },
	},
	[2] = {
		.codes = { KEY_KP2, KEY_KP8, KEY_KP5, KEY_RESERVED,
			   KEY_RESERVED, KEY_RESERVED, KEY_RESERVED },
	},
};

static irqreturn_t lbookv3_powerkey_isr(int irq, void *dev_id)
{
	struct input_dev *input = dev_id;

	s3c2410_gpio_cfgpin(S3C2410_GPF6, S3C2410_GPF6_INP);

	if (s3c2410_gpio_getpin(S3C2410_GPF6))
		input_event(input, EV_KEY, KEY_POWER, 0);
	else
		input_event(input, EV_KEY, KEY_POWER, 1);

	input_sync(input);

	s3c2410_gpio_cfgpin(S3C2410_GPF6, S3C2410_GPF6_EINT6);

	return IRQ_HANDLED;
}

static irqreturn_t lbookv3_keys_isr(int irq, void *dev_id)
{
	int i, j;
	struct gpio_line *line;
	struct input_dev *input = dev_id;

	for (i = S3C2410_GPF0; i <= S3C2410_GPF2; i++)
		s3c2410_gpio_cfgpin(i, S3C2410_GPIO_INPUT);

	for (i = S3C2410_GPF0; i <= S3C2410_GPF2; i++) {
		line = &lines[i - S3C2410_GPF0];
		if (gpio_get_value(i)) {
			if (!line->state)
				continue;
			for (j = 0; j < 7; j++)
				if (line->state & (1 << j)) {
					input_event(input, EV_KEY,
						    line->codes[j], 0);
					input_sync(input);
				}
			line->state = 0;
			continue;
		}


		for (j = 0; j < ARRAY_SIZE(pullups); j++)
			do {
				s3c2410_gpio_setpin(pullups[j], 1);
			} while (!gpio_get_value(pullups[j]));

		mdelay(10);

		for (j = 0; j < ARRAY_SIZE(pullups); j++) {
			s3c2410_gpio_setpin(pullups[j], 0);
			udelay(10);

			if (!gpio_get_value(i)) {
				line->state |= 1 << j;
				input_event(input, EV_KEY, line->codes[j], 1);
				input_sync(input);
				break;
			}
		}

		for (j = 0; j < ARRAY_SIZE(pullups); j++)
			while (s3c2410_gpio_getpin(pullups[j]))
				s3c2410_gpio_setpin(pullups[j], 0);

		udelay(10);
	}

	for (i = S3C2410_GPF0; i <= S3C2410_GPF2; i++)
		s3c2410_gpio_cfgpin(i, S3C2410_GPIO_SFN2);

	return IRQ_HANDLED;
}

static struct input_dev *input;
static int __init lbookv3_keys_init(void)
{
	int i, j, error;
	struct gpio_line *line;

	for (i = 0; i < ARRAY_SIZE(pullups); i++) {
		s3c2410_gpio_cfgpin(pullups[i], S3C2410_GPIO_OUTPUT);
		s3c2410_gpio_setpin(pullups[i], 0);
	}

	input = input_allocate_device();
	if (!input)
		return -ENOMEM;

	input->evbit[0] = BIT(EV_KEY);

	input->name = "lbookv3-keys";
	input->phys = "lbookv3-keys/input0";

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	for (i = S3C2410_GPF0; i <= S3C2410_GPF2; i++) {
		line = &lines[i - S3C2410_GPF0];

		for (j = 0; j < 7; j++)
			if (line->codes[j] != KEY_RESERVED)
				input_set_capability(input, EV_KEY,
						line->codes[j]);
	}

	input_set_capability(input, EV_KEY, KEY_POWER);

	error = input_register_device(input);
	if (error) {
		printk(KERN_ERR "Unable to register lbookv3-keys input device\n");
		goto fail1;
	}

	lbookv3_powerkey_isr(0, input);
	lbookv3_keys_isr(0, input);

	for (i = S3C2410_GPF0; i <= S3C2410_GPF2; i++) {
		int irq = s3c2410_gpio_getirq(i);

		s3c2410_gpio_cfgpin(i, S3C2410_GPIO_SFN2);
		s3c2410_gpio_pullup(i, 1);

		set_irq_type(irq, IRQ_TYPE_EDGE_BOTH);
		error = request_irq(irq, lbookv3_keys_isr, IRQF_SAMPLE_RANDOM,
				    "lbookv3_keys", input);
		if (error) {
			printk(KERN_ERR "lbookv3-keys: unable to claim irq %d; error %d\n",
				irq, error);
			goto fail_reg_irqs;
		}
		enable_irq_wake(irq);
	}

	set_irq_type(IRQ_EINT6, IRQ_TYPE_EDGE_BOTH);
	error = request_irq(IRQ_EINT6, lbookv3_powerkey_isr, IRQF_SAMPLE_RANDOM,
			"lbookv3_keys", input);
	if (error) {
		printk(KERN_ERR "lbookv3-keys: unable to claim irq %d; error %d\n",
				IRQ_EINT6, error);
		goto fail_reg_eint6;
	}
	enable_irq_wake(IRQ_EINT6);

	s3c2410_gpio_cfgpin(S3C2410_GPF6, S3C2410_GPF6_EINT6);
	s3c2410_gpio_pullup(S3C2410_GPF6, 1);

	return 0;

	free_irq(IRQ_EINT6, input);
fail_reg_irqs:
fail_reg_eint6:
	for (i = i - 1; i >= S3C2410_GPF0; i--)
		free_irq(s3c2410_gpio_getirq(i), input);

	input_free_device(input);
fail1:

	return error;
}

static void __exit lbookv3_keys_exit(void)
{
	int i;

	disable_irq_wake(IRQ_EINT6);
	free_irq(IRQ_EINT6, input);

	for (i = S3C2410_GPF0; i <= S3C2410_GPF2; i++) {
		int irq = s3c2410_gpio_getirq(i);

		disable_irq_wake(irq);
		free_irq(irq, input);
	}

	input_unregister_device(input);

	input_free_device(input);
}
/*
#ifdef CONFIG_PM
static lbookv3_keys_resume_early(struct platform_device *pdev)
{
	struct input_dev *input = platform_get_drvdata(pdev);


}
#endif

static struct platform_driver lbookv3_keys_driver = {
	.probe = lbookv3_keys_probe,
	.remove = lbookv3_keys_remove,
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "lbookv3-keys",
	},
#ifdef CONFIG_PM
	.resume_early	= lbookv3_keys_resume_early,
#endif
};
*/
module_init(lbookv3_keys_init);
module_exit(lbookv3_keys_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eugene Konev <ejka@imfi.kspu.ru>");
MODULE_DESCRIPTION("Keyboard driver for Lbook V3 GPIOs");
