/*
 * linux/drivers/video/lbookv3fb.c -- FB driver for lBook/Jinke eReader V3
 *
 * Copyright (C) 2007, Yauhen Kharuzhy
 * This driver is part of openinkpot.org project
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 * This driver based on hecubafb driver code.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <asm/uaccess.h>

#include <asm/arch/regs-gpio.h>
#include <asm/hardware.h>
#include <asm/io.h>

/* #define debug(fmt, args...) printk(KERN_INFO "%s: " fmt, __FUNCTION__, args) */
#define debug(fmt, args...)

/* Apollo controller specific defines */
#define APOLLO_WRITE_TO_FLASH		0x01
#define APOLLO_READ_FROM_FLASH		0x02
#define APOLLO_WRITE_REGISTER		0x10
#define APOLLO_READ_REGISTER		0x11
#define APOLLO_READ_TEMPERATURE		0x21
#define APOLLO_LOAD_PICTURE		0xA0
#define APOLLO_STOP_LOADING		0xA1
#define APOLLO_DISPLAY_PICTURE		0xA2
#define APOLLO_ERASE_DISPLAY		0xA3
#define APOLLO_INIT_DISPLAY		0xA4
#define APOLLO_GET_STATUS		0xAA
#define APOLLO_LOAD_PARTIAL_PICTURE	0xB0
#define APOLLO_DISPLAY_PARTIAL_PICTURE	0xB1
#define APOLLO_VERSION_NUMBER		0xE0
#define APOLLO_DISPLAY_SIZE		0xE2
#define APOLLO_RESET			0xEE
#define APOLLO_NORMAL_MODE		0xF0
#define APOLLO_SLEEP_MODE		0xF1
#define APOLLO_STANDBY_MODE		0xF2
#define APOLLO_SET_DEPTH		0xF3
#define APOLLO_ORIENTATION		0xF5
#define APOLLO_POSITIVE_PICTURE		0xF7
#define APOLLO_NEGATIVE_PICTURE		0xF8
#define APOLLO_AUTO_REFRESH		0xF9
#define APOLLO_CANCEL_AUTO_REFRESH	0xFA
#define APOLLO_SET_REFRESH_TIMER	0xFB
#define APOLLO_MANUAL_REFRESH		0xFC
#define APOLLO_READ_REFRESH_TIMER	0xFD



#define H_CD	S3C2410_GPD10
#define H_RW	S3C2410_GPD12
#define H_DS	S3C2410_GPD13
#define H_ACK	S3C2410_GPD11
#define H_WUP	S3C2410_GPD14
#define H_NRST	S3C2410_GPD15


/* Display specific information */
#define DPY_W 600
#define DPY_H 800

struct lbookv3fb_par {
	struct fb_info *info;
};

static struct fb_fix_screeninfo lbookv3fb_fix __devinitdata = {
	.id =		"lbookv3fb",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_STATIC_PSEUDOCOLOR,
	.xpanstep =	0,
	.ypanstep =	0,
	.ywrapstep =	0,
	.accel =	FB_ACCEL_NONE,
};

static struct fb_var_screeninfo lbookv3fb_var __devinitdata = {
	.xres		= DPY_W,
	.yres		= DPY_H,
	.xres_virtual	= DPY_W,
	.yres_virtual	= DPY_H,
	.grayscale	= 1,
	.bits_per_pixel	= 2,
	.nonstd		= 1,
};

static inline int apollo_get_ctlpin(int pin)
{
	return s3c2410_gpio_getpin(pin) ? 1 : 0;
}

static inline void apollo_set_ctlpin(int pin, unsigned char val)
{
	s3c2410_gpio_setpin(pin, val);
}

static void apollo_set_gpa_14_15(int val)
{
	if (val != 1) {
		s3c2410_gpio_cfgpin(S3C2410_GPA15, S3C2410_GPA15_OUT);
		s3c2410_gpio_setpin(S3C2410_GPA15, 0);
		s3c2410_gpio_cfgpin(S3C2410_GPA14, 0);
		s3c2410_gpio_setpin(S3C2410_GPA14, 1);

	} else {
		s3c2410_gpio_cfgpin(S3C2410_GPA15, S3C2410_GPA15_OUT);
		s3c2410_gpio_setpin(S3C2410_GPA15, 1);
		s3c2410_gpio_cfgpin(S3C2410_GPA14, S3C2410_GPA14_OUT);
		s3c2410_gpio_setpin(S3C2410_GPA14, 0);
	}
}



static int __devinit apollo_setuphw(void) {
	apollo_set_gpa_14_15(0);
	s3c2410_gpio_cfgpin(S3C2410_GPD10, S3C2410_GPD10_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPD13, S3C2410_GPD13_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPD12, S3C2410_GPD12_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPD11, S3C2410_GPD11_INP);
	s3c2410_gpio_cfgpin(S3C2410_GPD14, S3C2410_GPD14_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPD15, S3C2410_GPD15_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPE1, S3C2410_GPE1_OUTP);

	s3c2410_gpio_setpin(S3C2410_GPD10, 0);
	s3c2410_gpio_setpin(S3C2410_GPD13, 1);
	s3c2410_gpio_setpin(S3C2410_GPD12, 0);
	s3c2410_gpio_setpin(S3C2410_GPD14, 0);
	s3c2410_gpio_setpin(S3C2410_GPD15, 1);
	s3c2410_gpio_setpin(S3C2410_GPE1, 0);

	s3c2410_gpio_cfgpin(S3C2410_GPD2, S3C2410_GPD2_OUTP);
	s3c2410_gpio_setpin(S3C2410_GPD2, 1);

	s3c2410_gpio_cfgpin(S3C2410_GPC13, S3C2410_GPC13_OUTP);
	s3c2410_gpio_setpin(S3C2410_GPC13, 1);

	s3c2410_gpio_setpin(S3C2410_GPD15, 0);
	udelay(20);
	s3c2410_gpio_setpin(S3C2410_GPD15, 1);
	udelay(20);

	apollo_set_ctlpin(H_CD, 0);
	return 0;
}

static int apollo_wait_for_ack(void)
{
	int i=500000;

	while (s3c2410_gpio_getpin(H_ACK)) {
		if (!i) {
			printk(KERN_ERR "%s: H_ACK timeout\n", __FUNCTION__);
			return 0;
		}
		i--;
		udelay(10);
	}

	return 1;
}

static int apollo_wait_for_ack_clear(void)
{
	int i=500000;
	while (!s3c2410_gpio_getpin(H_ACK)) {
		if (!i) {
			printk(KERN_ERR "%s: H_ACK timeout\n", __FUNCTION__);
			return 0;
		}
		i--;
		udelay(10);
	}

	return 1;
}
static void apollo_send_data(unsigned char data)
{

//	debug("data = 0x%02x\n", data);
	apollo_set_ctlpin(H_RW, 0);
	udelay(10);
	writeb(data, 0xE8000000);
	apollo_set_ctlpin(H_DS, 0);
	apollo_wait_for_ack();
	apollo_set_ctlpin(H_DS, 1);
	apollo_wait_for_ack_clear();
}


static void apollo_send_command(unsigned char cmd)
{
	debug("cmd = 0x%02x\n", cmd);

	apollo_set_ctlpin(H_CD, 1);
	apollo_send_data(cmd);
	apollo_set_ctlpin(H_CD, 0);
}

static unsigned char apollo_read_data(void)
{
	unsigned char res;

	apollo_set_ctlpin(H_RW, 1);
	apollo_set_ctlpin(H_DS, 0);
	apollo_wait_for_ack();
	res = readb(0xE8000000);
	apollo_set_ctlpin(H_DS, 1);
	apollo_wait_for_ack_clear();
	apollo_set_ctlpin(H_RW, 0);

	return res;
}

static unsigned char apollo_get_status(void)
{
	unsigned char res;

	apollo_send_command(0xAA);
	apollo_set_gpa_14_15(1);
	res = apollo_read_data();
	apollo_set_gpa_14_15(0);
	printk(KERN_ERR "%s: status = 0x%02x\n", __FUNCTION__, res);
	return res;
}

static void apollo_reinitialize(void)
{
	int timeout = 100;

	s3c2410_gpio_setpin(S3C2410_GPA14, 1);
	s3c2410_gpio_setpin(S3C2410_GPA15, 0);
	s3c2410_gpio_cfgpin(S3C2410_GPE1, S3C2410_GPE1_OUTP);
	s3c2410_gpio_setpin(S3C2410_GPE1, 0);
	s3c2410_gpio_cfgpin(S3C2410_GPC13, S3C2410_GPC13_OUTP);
	s3c2410_gpio_setpin(S3C2410_GPC13, 1);
	do {
		apollo_send_command(0xF0);
		udelay(100);
		timeout--;
	} while ((apollo_get_status() == 0xff) & timeout);
}

/* main lbookv3fb functions */

static void lbookv3fb_dpy_update(struct lbookv3fb_par *par)
{
	int i;
	unsigned char *buf = (unsigned char __force *)par->info->screen_base;

	debug("called\n", par);

	apollo_send_command(0xA0);

	for (i=0; i < (DPY_W*DPY_H/8 * par->info->var.bits_per_pixel); i++) {
		apollo_send_data(*(buf++));
	}

	apollo_send_command(0xA1);
	apollo_send_command(0xA2);
}

/* this is called back from the deferred io workqueue */
static void lbookv3fb_dpy_deferred_io(struct fb_info *info,
				struct list_head *pagelist)
{
	lbookv3fb_dpy_update(info->par);
}

static void lbookv3fb_fillrect(struct fb_info *info,
				   const struct fb_fillrect *rect)
{
	struct lbookv3fb_par *par = info->par;
/*	int i;
	unsigned char *buf = (unsigned char __force *)par->info->screen_base;

	apollo_send_command(APOLLO_LOAD_PARTIAL_PICTURE);
	apollo_send_data(
*/
	sys_fillrect(info, rect);

	lbookv3fb_dpy_update(par);
}

static void lbookv3fb_copyarea(struct fb_info *info,
				   const struct fb_copyarea *area)
{
	struct lbookv3fb_par *par = info->par;

	debug("called\n", area);

	sys_copyarea(info, area);

	lbookv3fb_dpy_update(par);
}

static void lbookv3fb_imageblit(struct fb_info *info,
				const struct fb_image *image)
{
	struct lbookv3fb_par *par = info->par;

	debug("called, fb_info = %p\n", info);

	sys_imageblit(info, image);

	lbookv3fb_dpy_update(par);
}

/*
 * this is the slow path from userspace. they can seek and write to
 * the fb. it's inefficient to do anything less than a full screen draw
 */
static ssize_t lbookv3fb_write(struct fb_info *info, const char __user *buf,
				size_t count, loff_t *ppos)
{
	unsigned long p;
	int err=-EINVAL;
	struct lbookv3fb_par *par;
	unsigned int xres;
	unsigned int fbmemlength;

	p = *ppos;
	par = info->par;
	xres = info->var.xres;
	fbmemlength = (xres * info->var.yres)/8 * info->var.bits_per_pixel;

	if (p > fbmemlength)
		return -ENOSPC;

	err = 0;
	if ((count + p) > fbmemlength) {
		count = fbmemlength - p;
		err = -ENOSPC;
	}

	if (count) {
		char *base_addr;

		base_addr = (char __force *)info->screen_base;
		count -= copy_from_user(base_addr + p, buf, count);
		*ppos += count;
		err = -EFAULT;
	}

	lbookv3fb_dpy_update(par);

	if (count)
		return count;

	return err;
}

static struct fb_ops lbookv3fb_ops = {
	.owner		= THIS_MODULE,
	.fb_read        = fb_sys_read,
	.fb_write	= lbookv3fb_write,
	.fb_fillrect	= lbookv3fb_fillrect,
	.fb_copyarea	= lbookv3fb_copyarea,
	.fb_imageblit	= lbookv3fb_imageblit,
};

static struct fb_deferred_io lbookv3fb_defio = {
	.delay		= HZ,
	.deferred_io	= lbookv3fb_dpy_deferred_io,
};

static int __devinit lbookv3fb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	int retval = -ENOMEM;
	int videomemorysize;
	unsigned char *videomemory;
	struct lbookv3fb_par *par;

	videomemorysize = (DPY_W*DPY_H)/8 * lbookv3fb_var.bits_per_pixel;

	if (!(videomemory = vmalloc(videomemorysize)))
		return retval;

	memset(videomemory, 0xFF, videomemorysize);

	info = framebuffer_alloc(sizeof(struct lbookv3fb_par), &dev->dev);
	if (!info)
		goto err;

	info->screen_base = (char __iomem *) videomemory;
	info->fbops = &lbookv3fb_ops;

	info->var = lbookv3fb_var;
	info->fix = lbookv3fb_fix;
	info->fix.smem_len = videomemorysize;
	par = info->par;
	par->info = info;

	info->flags = FBINFO_FLAG_DEFAULT;

	info->fbdefio = &lbookv3fb_defio;
	fb_deferred_io_init(info);

	fb_alloc_cmap(&info->cmap, 4, 0);

	retval = register_framebuffer(info);
	if (retval < 0)
		goto err1;
	platform_set_drvdata(dev, info);

	printk(KERN_INFO
	       "fb%d: lbookv3 frame buffer device, using %dK of video memory\n",
	       info->node, videomemorysize >> 10);

	apollo_setuphw();
	apollo_reinitialize();

	apollo_send_command(APOLLO_SET_DEPTH);
	apollo_send_data(0x02);
	apollo_send_command(APOLLO_ERASE_DISPLAY);
	apollo_send_data(0x01);

	return 0;
err1:
	framebuffer_release(info);
err:
	vfree(videomemory);
	return retval;
}

static int __devexit lbookv3fb_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);

	if (info) {
		fb_deferred_io_cleanup(info);
		unregister_framebuffer(info);
		vfree((void __force *)info->screen_base);
		framebuffer_release(info);
	}
	return 0;
}

static struct platform_driver lbookv3fb_driver = {
	.probe	= lbookv3fb_probe,
	.remove = lbookv3fb_remove,
	.driver	= {
		.name	= "lbookv3fb",
	},
};

static struct platform_device *lbookv3fb_device;

static int __init lbookv3fb_init(void)
{
	int ret;

	ret = platform_driver_register(&lbookv3fb_driver);
	if (!ret) {
		lbookv3fb_device = platform_device_alloc("lbookv3fb", 0);
		if (lbookv3fb_device)
			ret = platform_device_add(lbookv3fb_device);
		else
			ret = -ENOMEM;

		if (ret) {
			platform_device_put(lbookv3fb_device);
			platform_driver_unregister(&lbookv3fb_driver);
		}
	}
	return ret;

}

static void __exit lbookv3fb_exit(void)
{
	platform_device_unregister(lbookv3fb_device);
	platform_driver_unregister(&lbookv3fb_driver);
}


module_init(lbookv3fb_init);
module_exit(lbookv3fb_exit);

MODULE_DESCRIPTION("fbdev driver for lBook/Jinke eReader V3");
MODULE_AUTHOR("Yauhen Kharuzhy");
MODULE_LICENSE("GPL");
