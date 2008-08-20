/*
 * linux/drivers/video/hecubafb.c -- FB driver for Hecuba/Apollo controller
 *
 * Copyright (C) 2006, Jaya Kumar
 * This work was sponsored by CIS(M) Sdn Bhd
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Layout is based on skeletonfb.c by James Simmons and Geert Uytterhoeven.
 * This work was possible because of apollo display code from E-Ink's website
 * http://support.eink.com/community
 * All information used to write this code is from public material made
 * available by E-Ink on its support site. Some commands such as 0xA4
 * were found by looping through cmd=0x00 thru 0xFF and supplying random
 * values. There are other commands that the display is capable of,
 * beyond the 5 used here but they are more complex.
 *
 * This driver is written to be used with the Hecuba display architecture.
 * The actual display chip is called Apollo and the interface electronics
 * it needs is called Hecuba.
 *
 * It is intended to be architecture independent. A board specific driver
 * must be used to perform all the physical IO interactions. An example
 * is provided as n411.c
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
#include <linux/uaccess.h>

#include <video/hecubafb.h>

static unsigned int panel_mode;
static void (*hecubafb_do_display)(struct hecubafb_par *par,
					unsigned char *buf, u16 y1, u16 y2);

/* Display specific information */
#define DPY_W 800
#define DPY_H 600

static struct fb_fix_screeninfo hecubafb_fix __devinitdata = {
	.id =		"hecubafb",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_STATIC_PSEUDOCOLOR,
	.xpanstep =	0,
	.ypanstep =	0,
	.ywrapstep =	0,
	.line_length =	DPY_W,
	.accel =	FB_ACCEL_NONE,
};

static struct fb_var_screeninfo hecubafb_var __devinitdata = {
	.xres		= DPY_W,
	.yres		= DPY_H,
	.xres_virtual	= DPY_W,
	.yres_virtual	= DPY_H,
	.bits_per_pixel	= 8,
	.grayscale	= 1,
	.nonstd		= 1,
	.red =		{ 0, 1, 0 },
	.green =	{ 0, 1, 0 },
	.blue =		{ 0, 1, 0 },
	.transp =	{ 0, 0, 0 },
};

/* main hecubafb functions */

static void apollo_send_data(struct hecubafb_par *par, unsigned char data)
{
	/* set data */
	par->board->set_data(par, data);

	/* set DS low */
	par->board->set_ctl(par, HCB_DS_BIT, 0);

	/* wait for ack */
	par->board->wait_for_ack(par, 0);

	/* set DS hi */
	par->board->set_ctl(par, HCB_DS_BIT, 1);

	/* wait for ack to clear */
	par->board->wait_for_ack(par, 1);
}

static void apollo_send_command(struct hecubafb_par *par, unsigned char data)
{
	/* command so set CD to high */
	par->board->set_ctl(par, HCB_CD_BIT, 1);

	/* actually strobe with command */
	apollo_send_data(par, data);

	/* clear CD back to low */
	par->board->set_ctl(par, HCB_CD_BIT, 0);
}

static void hecubafb_1bit_raw_displayer(struct hecubafb_par *par,
					unsigned char *buf, u16 j, u16 y2)
{
	int i;

	buf += j * par->info->var.xres;
	for (; j < y2; j++) {
		for (i = 0; i < par->info->var.xres/8; i++)
			apollo_send_data(par, *(buf++));
	}
}

static void hecubafb_1bit_displayer(struct hecubafb_par *par,
					unsigned char *buf, u16 j, u16 y2)
{
	int i, k;

	buf += j * par->info->var.xres;
	for (; j < y2; j++) {
		for (i = 0; i < par->info->var.xres/8; i++) {
			u8 output = 0;

			/* input=1bpp, output byte has 8 pixels */
			for (k = 0; k < 8; k++)
				output |= (((*(buf++) >> 1) & 0x01) << (7 - k));

			output = ~output;
			apollo_send_data(par, output);
		}
	}
}

static void hecubafb_2bit_displayer(struct hecubafb_par *par,
					unsigned char *buf, u16 j, u16 y2)
{
	int i, k;

	buf += j * par->info->var.xres;
	for (; j < y2; j++) {
		for (i = 0; i < par->info->var.xres/4; i++) {
			u8 output = 0;

			/* input=2bpp, output byte has 4 pixels */
			for (k = 0; k < 4; k++)
				output |= (((*(buf++)) & 0x03) << (6 - (2*k)));

			apollo_send_data(par, output);
		}
	}
}

static void hecubafb_dpy_update(struct hecubafb_par *par, int j, int y2)
{
	unsigned char *buf = (unsigned char __force *)par->info->screen_base;

	apollo_send_command(par, APOLLO_START_NEW_IMG);

	hecubafb_do_display(par, buf, j, y2);

	apollo_send_command(par, APOLLO_STOP_IMG_DATA);
	apollo_send_command(par, APOLLO_DISPLAY_IMG);
}

static void hecubafb_dpy_update_pages(struct hecubafb_par *par, u16 y1, u16 y2)
{
	u16 x2;
	unsigned char *buf = (unsigned char __force *)par->info->screen_base;

	/* y1 must be a multiple of 4 so drop the lower bits */
	y1 &= 0xFFFC;
	/* y2 must be a multiple of 4 , but - 1 so up the lower bits */
	y2 |= 0x0003;
	apollo_send_command(par, APOLLO_START_PART_IMG);
	apollo_send_data(par, 0);
	apollo_send_data(par, 0);
	apollo_send_data(par, cpu_to_le16(y1) >> 8);
	apollo_send_data(par, cpu_to_le16(y1));
	x2 = par->info->var.xres - 1;
	apollo_send_data(par, cpu_to_le16(x2) >> 8);
	apollo_send_data(par, cpu_to_le16(x2));
	apollo_send_data(par, cpu_to_le16(y2) >> 8);
	apollo_send_data(par, cpu_to_le16(y2));
	hecubafb_do_display(par, buf, y1, y2 + 1);
	apollo_send_command(par, APOLLO_STOP_IMG_DATA);
	apollo_send_command(par, APOLLO_DISPLAY_PART_IMG);
}

/* this is called back from the deferred io workqueue */
static void hecubafb_dpy_deferred_io(struct fb_info *info,
				struct list_head *pagelist)
{
	u16 y1 = 0, h = 0;
	int prev_index = -1;
	struct page *cur;
	struct fb_deferred_io *fbdefio = info->fbdefio;
	int h_inc;
	u16 yres = info->var.yres;
	u16 xres = info->var.xres;

	/* height increment is fixed per page */
	h_inc = DIV_ROUND_UP(PAGE_SIZE , xres);

	/* walk the written page list and swizzle the data */
	list_for_each_entry(cur, &fbdefio->pagelist, lru) {
		if (prev_index < 0) {
			/* just starting so assign first page */
			y1 = (cur->index << PAGE_SHIFT) / xres;
			h = h_inc;
		} else if ((prev_index + 1) == cur->index) {
			/* this page is consecutive so increase our height */
			h += h_inc;
		} else {
			/* page not consecutive, issue previous update first */
			hecubafb_dpy_update_pages(info->par, y1, y1 + h);
			/* start over with our non consecutive page */
			y1 = (cur->index << PAGE_SHIFT) / xres;
			h = h_inc;
		}
		prev_index = cur->index;
	}

	/* if we still have any pages to update we do so now */
	if (h >= yres) {
		/* its a full screen update, just do it */
		hecubafb_dpy_update(info->par, 0, yres);
	} else {
		hecubafb_dpy_update_pages(info->par, y1,
						min((u16) (y1 + h), yres));
	}
}

static void hecubafb_fillrect(struct fb_info *info,
				   const struct fb_fillrect *rect)
{
	struct hecubafb_par *par = info->par;

	sys_fillrect(info, rect);

	hecubafb_dpy_update(par, 0, info->var.yres);
}

static void hecubafb_copyarea(struct fb_info *info,
				   const struct fb_copyarea *area)
{
	struct hecubafb_par *par = info->par;

	sys_copyarea(info, area);

	hecubafb_dpy_update(par, 0, info->var.yres);
}

static void hecubafb_imageblit(struct fb_info *info,
				const struct fb_image *image)
{
	struct hecubafb_par *par = info->par;

	sys_imageblit(info, image);

	hecubafb_dpy_update(par, 0, info->var.yres);
}

/*
 * this is the slow path from userspace. they can seek and write to
 * the fb. it's inefficient to do anything less than a full screen draw
 */
static ssize_t hecubafb_write(struct fb_info *info, const char __user *buf,
				size_t count, loff_t *ppos)
{
	struct hecubafb_par *par = info->par;
	unsigned long p = *ppos;
	void *dst;
	int err = 0;
	unsigned long total_size;

	if (info->state != FBINFO_STATE_RUNNING)
		return -EPERM;

	total_size = info->fix.smem_len;

	if (p > total_size)
		return -EFBIG;

	if (count > total_size) {
		err = -EFBIG;
		count = total_size;
	}

	if (count + p > total_size) {
		if (!err)
			err = -ENOSPC;

		count = total_size - p;
	}

	dst = (void __force *) (info->screen_base + p);

	if (copy_from_user(dst, buf, count))
		err = -EFAULT;

	if  (!err)
		*ppos += count;

	hecubafb_dpy_update(par, 0, info->var.yres);

	return (err) ? err : count;
}

static int hecubafb_check_var(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	switch (info->var.bits_per_pixel) {
	case 1:
		info->fix.visual = FB_VISUAL_MONO01;
		var->red.offset = 0; var->red.length = 1;
		var->green.offset = 0; var->green.length = 1;
		var->blue.offset = 0; var->blue.length = 1;
		var->transp.offset = var->transp.length = 0;
		var->bits_per_pixel = 1;
		break;
	default:
		info->fix.visual = FB_VISUAL_STATIC_PSEUDOCOLOR;
		var->red.offset = 0; var->red.length = 2;
		var->green.offset = 0; var->green.length = 2;
		var->blue.offset = 0; var->blue.length = 2;
		var->transp.offset = var->transp.length = 0;
		var->bits_per_pixel = 8;
		break;
	}

	var->xres = DPY_W; var->yres = DPY_H;

	return 0;
}

static int hecubafb_set_par(struct fb_info *info)
{
	struct hecubafb_par *par = info->par;

	switch (panel_mode) {
	case 2:
		apollo_send_command(par, APOLLO_SET_DEPTH);
		apollo_send_data(par, 0x02);
		info->fix.visual = FB_VISUAL_STATIC_PSEUDOCOLOR;
		hecubafb_do_display = hecubafb_2bit_displayer;
		break;
	case 1:
	default:
		apollo_send_command(par, APOLLO_SET_DEPTH);
		apollo_send_data(par, 0x00);
		switch (info->var.bits_per_pixel) {
		case 1:
			/* if the app asks 1bit, provide mono */
			info->fix.visual = FB_VISUAL_MONO01;
			hecubafb_do_display = hecubafb_1bit_raw_displayer;
			break;
		default:
			/* otherwise stick with 8bpp */
			info->var.bits_per_pixel = 8;
			info->fix.visual = FB_VISUAL_STATIC_PSEUDOCOLOR;
			hecubafb_do_display = hecubafb_1bit_displayer;
			break;
		}
		break;
	}

	return 0;
}

static struct fb_ops hecubafb_ops = {
	.owner		= THIS_MODULE,
	.fb_read        = fb_sys_read,
	.fb_write	= hecubafb_write,
	.fb_fillrect	= hecubafb_fillrect,
	.fb_copyarea	= hecubafb_copyarea,
	.fb_imageblit	= hecubafb_imageblit,
	.fb_check_var	= hecubafb_check_var,
	.fb_set_par	= hecubafb_set_par,
};

static struct fb_deferred_io hecubafb_defio = {
	.delay		= HZ,
	.deferred_io	= hecubafb_dpy_deferred_io,
};

static int __devinit handle_cmap(struct fb_info *info, int mode)
{
	int i;
	int retval;
	int cmap_size;

	switch (mode) {
	case 2:
		cmap_size = 4;
		break;
	case 1:
	default:
		cmap_size = 2;
		break;
	}

	retval = fb_alloc_cmap(&info->cmap, cmap_size, 0);
	if (retval < 0) {
		printk(KERN_ERR "Failed to allocate colormap\n");
		return retval;
	}

	/* find mid points for cmap */
	for (i = 0; i < cmap_size; i++)
		info->cmap.red[i] = (((2*i)+1)*(0xFFFF))/(2*cmap_size);

	memcpy(info->cmap.green, info->cmap.red, sizeof(u16)*cmap_size);
	memcpy(info->cmap.blue, info->cmap.red, sizeof(u16)*cmap_size);

	return 0;
}

static int __devinit hecubafb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	struct hecuba_board *board;
	int retval = -ENOMEM;
	int videomemorysize;
	unsigned char *videomemory;
	struct hecubafb_par *par;

	/* pick up board specific routines */
	board = dev->dev.platform_data;
	if (!board)
		return -EINVAL;

	/* try to count device specific driver, if can't, platform recalls */
	if (!try_module_get(board->owner))
		return -ENODEV;

	info = framebuffer_alloc(sizeof(struct hecubafb_par), &dev->dev);
	if (!info)
		goto err;

	videomemorysize = (DPY_W*DPY_H);
	videomemory = vmalloc(videomemorysize);
	if (!videomemory)
		goto err_fb_rel;

	memset(videomemory, 0, videomemorysize);

	info->screen_base = (char __force __iomem *)videomemory;
	info->fbops = &hecubafb_ops;

	info->var = hecubafb_var;
	info->fix = hecubafb_fix;
	info->fix.smem_len = videomemorysize;
	par = info->par;
	par->info = info;
	par->board = board;
	par->send_command = apollo_send_command;
	par->send_data = apollo_send_data;

	info->flags = FBINFO_FLAG_DEFAULT;

	info->fbdefio = &hecubafb_defio;
	fb_deferred_io_init(info);

	retval = handle_cmap(info, panel_mode);
	if (retval < 0)
		goto err_vfree;

	/* this inits the dpy */
	retval = par->board->init(par);
	if (retval < 0)
		goto err_cmap;

	hecubafb_set_par(info);

	retval = register_framebuffer(info);
	if (retval < 0)
		goto err_board;

	platform_set_drvdata(dev, info);

	printk(KERN_INFO
	       "fb%d: Hecuba frame buffer device, using %dK of video memory\n",
	       info->node, videomemorysize >> 10);

	return 0;

err_cmap:
	fb_dealloc_cmap(&info->cmap);
err_board:
	if (par->board->remove)
		par->board->remove(par);
err_vfree:
	vfree(videomemory);
err_fb_rel:
	framebuffer_release(info);
err:
	module_put(board->owner);
	return retval;
}

static int __devexit hecubafb_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);

	if (info) {
		struct hecubafb_par *par = info->par;

		unregister_framebuffer(info);
		fb_deferred_io_cleanup(info);
		fb_dealloc_cmap(&info->cmap);

		if (par->board->remove)
			par->board->remove(par);

		vfree((void __force *)info->screen_base);
		module_put(par->board->owner);
		framebuffer_release(info);
	}
	return 0;
}

static struct platform_driver hecubafb_driver = {
	.probe	= hecubafb_probe,
	.remove = hecubafb_remove,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "hecubafb",
	},
};

static int __init hecubafb_init(void)
{
	return platform_driver_register(&hecubafb_driver);
}

static void __exit hecubafb_exit(void)
{
	platform_driver_unregister(&hecubafb_driver);
}

module_param(panel_mode, uint, 0);
MODULE_PARM_DESC(panel_mode, "Panel mode: 1 for 1 bit, 2 for 2 bit");

module_init(hecubafb_init);
module_exit(hecubafb_exit);

MODULE_DESCRIPTION("fbdev driver for Hecuba/Apollo controller");
MODULE_AUTHOR("Jaya Kumar");
MODULE_LICENSE("GPL");
