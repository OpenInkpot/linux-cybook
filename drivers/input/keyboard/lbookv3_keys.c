/*
 * Driver for lBook/Jinke eReader V3 keys
 *
 * Copyright 2008 Eugene Konev <ejka@imfi.kspu.ru>
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
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/arch/regs-gpio.h>

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

static irqreturn_t lbookv3_keys_isr(int irq, void *dev_id)
{
	int i, j;
	struct gpio_line *line;
	struct input_dev *input = dev_id;

	for (i = S3C2410_GPF0; i <= S3C2410_GPF2; i++) {
		line = &lines[i - S3C2410_GPF0];
		if (gpio_get_value(i)) {
			if (!line->state)
				continue;
			for (j = 0; j < 7; j++)
				if (line->state & (1 << j)) {
					input_event(input, EV_KEY,
						    line->codes[j], 0);
				}
			line->state = 0;
			continue;
		}

		s3c2410_gpio_cfgpin(i, S3C2410_GPIO_INPUT);

		for (j = 0; j < ARRAY_SIZE(pullups); j++)
			gpio_set_value(pullups[j], 1);

		mdelay(10);
		
		for (j = 0; j < ARRAY_SIZE(pullups); j++) {
			gpio_set_value(pullups[j], 0);
			udelay(10);

			if (!gpio_get_value(i)) {
				line->state |= 1 << j;
				input_event(input, EV_KEY, line->codes[j], 1);
				input_sync(input);
				break;
			}
		}

		for (j = 0; j < ARRAY_SIZE(pullups); j++)
			gpio_set_value(pullups[j], 0);

		s3c2410_gpio_cfgpin(i, S3C2410_GPIO_SFN2);
	}

	return IRQ_HANDLED;
}

static struct input_dev *input;
static int __init lbookv3_keys_init(void)
{
	int i, j, error;
	struct gpio_line *line;

	for (i = 0; i < ARRAY_SIZE(pullups); i++) {
		s3c2410_gpio_cfgpin(i, S3C2410_GPIO_OUTPUT);
		gpio_set_value(i, 0);
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
		int irq = gpio_to_irq(i);
		s3c2410_gpio_cfgpin(i, S3C2410_GPIO_SFN2);
		line = &lines[i - S3C2410_GPF0];

		set_irq_type(irq, IRQ_TYPE_EDGE_BOTH);
		error = request_irq(irq, lbookv3_keys_isr, IRQF_SAMPLE_RANDOM,
				    "lbookv3_keys", input);
		if (error) {
			printk(KERN_ERR "lbookv3-keys: unable to claim irq %d; error %d\n",
				irq, error);
			goto fail;
		}

		for (j = 0; j < 7; j++)
			if (line->codes[j] != KEY_RESERVED)
				input_set_capability(input, EV_KEY, line->codes[j]);
	}

	error = input_register_device(input);
	if (error) {
		printk(KERN_ERR "Unable to register lbookv3-keys input device\n");
		goto fail;
	}

	return 0;

 fail:
	for (i = i - 1; i >= S3C2410_GPF0; i--)
		free_irq(gpio_to_irq(i), input);

	input_free_device(input);

	return error;
}

static void __exit lbookv3_keys_exit(void)
{
	int i;

	for (i = S3C2410_GPF0; i <= S3C2410_GPF2; i++) {
		int irq = gpio_to_irq(i);
		free_irq(irq, input);
	}

	input_unregister_device(input);

	input_free_device(input);
}

module_init(lbookv3_keys_init);
module_exit(lbookv3_keys_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eugene Konev <ejka@imfi.kspu.ru>");
MODULE_DESCRIPTION("Keyboard driver for Lbook V3 GPIOs");
