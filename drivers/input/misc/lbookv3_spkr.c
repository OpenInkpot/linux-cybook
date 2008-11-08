/*
 * lBook eReader V3 speaker driver
 *
 * Copyright (c) 2008 Yauhen Kharuzhy
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#include <mach/hardware.h>
#include <mach/regs-gpio.h>

MODULE_AUTHOR("Yauhen Kharuzhy <jekhor@gmail.com>");
MODULE_DESCRIPTION("HanLin/lBook eReader V3 beeper driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lbookv3_spkr");

static struct pwm_device *lbookv3_spkr_pwm;

static int lbookv3_spkr_event(struct input_dev *dev,
		unsigned int type, unsigned int code, int value)
{
	if (type != EV_SND)
		return -1;

	switch (code) {
	case SND_BELL:
		if (value)
			value = 1000;
	case SND_TONE:
		break;
	default:
		return -1;
	}

	if ((value > 20) && (value < 20000)) {
		pwm_config(lbookv3_spkr_pwm, 1000000000 / value / 2,
				1000000000 / value);
		pwm_enable(lbookv3_spkr_pwm);
	} else {
		pwm_disable(lbookv3_spkr_pwm);
	}

	return 0;
}

static int __devinit lbookv3_spkr_probe(struct platform_device *dev)
{
	struct input_dev *lbookv3_spkr_dev;
	int err;

	lbookv3_spkr_dev = input_allocate_device();
	if (!lbookv3_spkr_dev)
		return -ENOMEM;

	lbookv3_spkr_dev->name = "lBook V3 Speaker";
	lbookv3_spkr_dev->phys = "pwm0";
	lbookv3_spkr_dev->id.bustype = BUS_HOST;
	lbookv3_spkr_dev->id.vendor = 0x001f;
	lbookv3_spkr_dev->id.product = 0x0001;
	lbookv3_spkr_dev->id.version = 0x0100;
	lbookv3_spkr_dev->dev.parent = &dev->dev;

	lbookv3_spkr_dev->evbit[0] = BIT_MASK(EV_SND);
	lbookv3_spkr_dev->sndbit[0] = BIT_MASK(SND_BELL) | BIT_MASK(SND_TONE);
	lbookv3_spkr_dev->event = lbookv3_spkr_event;

	lbookv3_spkr_pwm = pwm_request(0, "lbookv3-speaker");
	if (IS_ERR(lbookv3_spkr_pwm)) {
		dev_err(&dev->dev, "PWM request failed\n");
		err = PTR_ERR(lbookv3_spkr_pwm);
		goto pwm_req_err;
	}
	s3c2410_gpio_cfgpin(S3C2410_GPB0, S3C2410_GPB0_TOUT0);

	err = input_register_device(lbookv3_spkr_dev);
	if (err)
		goto input_reg_err;

	platform_set_drvdata(dev, lbookv3_spkr_dev);

	return 0;

	input_unregister_device(lbookv3_spkr_dev);
input_reg_err:
	pwm_free(lbookv3_spkr_pwm);
pwm_req_err:
	input_free_device(lbookv3_spkr_dev);

	return err;
}

static int __devexit lbookv3_spkr_remove(struct platform_device *dev)
{
	struct input_dev *lbookv3_spkr_dev = platform_get_drvdata(dev);

	input_unregister_device(lbookv3_spkr_dev);
	platform_set_drvdata(dev, NULL);
	/* turn off the speaker */
	lbookv3_spkr_event(NULL, EV_SND, SND_BELL, 0);
	s3c2410_gpio_cfgpin(S3C2410_GPB0, S3C2410_GPB0_OUTP);
	pwm_free(lbookv3_spkr_pwm);

	return 0;
}

static int lbookv3_spkr_suspend(struct platform_device *dev, pm_message_t state)
{
	lbookv3_spkr_event(NULL, EV_SND, SND_BELL, 0);

	return 0;
}

static void lbookv3_spkr_shutdown(struct platform_device *dev)
{
	/* turn off the speaker */
	lbookv3_spkr_event(NULL, EV_SND, SND_BELL, 0);
}

static struct platform_driver lbookv3_spkr_platform_driver = {
	.driver		= {
		.name	= "lbookv3-speaker",
		.owner	= THIS_MODULE,
	},
	.probe		= lbookv3_spkr_probe,
	.remove		= __devexit_p(lbookv3_spkr_remove),
	.suspend	= lbookv3_spkr_suspend,
	.shutdown	= lbookv3_spkr_shutdown,
};


static int __init lbookv3_spkr_init(void)
{
	return platform_driver_register(&lbookv3_spkr_platform_driver);
}

static void __exit lbookv3_spkr_exit(void)
{
	platform_driver_unregister(&lbookv3_spkr_platform_driver);
}

module_init(lbookv3_spkr_init);
module_exit(lbookv3_spkr_exit);
