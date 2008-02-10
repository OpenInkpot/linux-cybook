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
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/ctype.h>

#include <asm/uaccess.h>

#include <asm/arch/regs-gpio.h>
#include <asm/hardware.h>
#include <asm/io.h>

//#define debug(fmt, args...) printk(KERN_INFO "%s: " fmt, __FUNCTION__, args) 
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

struct lbookv3fb_options {
	unsigned int manual_refresh:1;
	unsigned int partial_update:1;
};

struct lbookv3fb_par {
	struct fb_info *info;
	struct mutex lock;
	struct delayed_work deferred_work;
	struct cdev cdev;
	struct lbookv3fb_options options;
};

static struct fb_fix_screeninfo lbookv3fb_fix __devinitdata = {
	.id =		"lbookv3fb",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_STATIC_PSEUDOCOLOR,
	.xpanstep =	0,
	.ypanstep =	0,
	.ywrapstep =	0,
	.line_length =	DPY_W / (8 / 8),
	.accel =	FB_ACCEL_NONE,
};

static struct fb_var_screeninfo lbookv3fb_var __devinitdata = {
	.xres		= DPY_W,
	.yres		= DPY_H,
	.xres_virtual	= DPY_W,
	.yres_virtual	= DPY_H,
	.red		= {0, 2, 0},
	.blue		= {0, 2, 0},
	.green		= {0, 2, 0},
	.grayscale	= 1,
	.bits_per_pixel	= 8,
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

static inline int apollo_wait_for_ack(void)
{
	int i=500000;

	while (s3c2410_gpio_getpin(H_ACK)) {
		if (!i) {
			printk(KERN_ERR "%s: H_ACK timeout\n", __FUNCTION__);
			return 0;
		}
		i--;
		schedule();
	}

	return 1;
}

static inline int apollo_wait_for_ack_clear(void)
{
	int i=500000;

	while (!s3c2410_gpio_getpin(H_ACK)) {
		if (!i) {
			printk(KERN_ERR "%s: H_ACK timeout\n", __FUNCTION__);
			return 0;
		}
		i--;
		schedule();
	}

	return 1;
}
static inline void apollo_send_data(unsigned char data)
{
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
	debug("%s: status = 0x%02x\n", __FUNCTION__, res);
	return res;
}

static void apollo_reinitialize(void)
{
	int timeout = 100;

	apollo_set_ctlpin(H_RW, 0);
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

#define pack_4pixels(a, b, c, d)	(((a) << 6) | ((b) << 4) |  ((c) << 2) | (d))

static void lbookv3fb_dpy_update(struct lbookv3fb_par *par)
{
	int i;
	unsigned char *buf = (unsigned char __force *)par->info->screen_base;
	int count = par->info->fix.smem_len;
	unsigned char tmp;

	cancel_delayed_work(&par->deferred_work);
	mutex_lock(&par->lock);

	if (par->options.manual_refresh)
		apollo_send_command(APOLLO_MANUAL_REFRESH);

	apollo_send_command(APOLLO_LOAD_PICTURE);

	for (i=0; i < count; i += 4) {
		tmp = pack_4pixels(buf[i], buf[i+1], buf[i+2], buf[i+3]);
		apollo_send_data(tmp);
	}

	apollo_send_command(APOLLO_STOP_LOADING);
	apollo_send_command(APOLLO_DISPLAY_PICTURE);

	mutex_unlock(&par->lock);
}

/*
 * x1 must be less than x2, y1 < y2
 */
static void lbookv3fb_apollo_update_part(struct lbookv3fb_par *par, 
		unsigned int x1, unsigned int y1, unsigned int x2, unsigned int y2)
{
	int i, j, k;
	unsigned char *buf = (unsigned char __force *)par->info->screen_base;
	unsigned char t;

	debug("called %p\n", par);
	debug("x1 = %d, y1 = %d, x2 = %d, y2 = %d, y2 * 600 + x2 = %d\n", x1, y1, x2, y2, y2 * 600 + x2);

	y1 -= y1 % 4;

	if ((y2 + 1) % 4)
		y2 += 4 - ((y2 + 1) % 4);

	x1 -= x1 % 4;

	if ((x2 + 1) % 4)
		x2 += 4 - ((x2 + 1) % 4);

	mutex_lock(&par->lock);

	if (par->options.manual_refresh)
		apollo_send_command(APOLLO_MANUAL_REFRESH);

	apollo_send_command(APOLLO_LOAD_PARTIAL_PICTURE);

	apollo_send_data((x1 >> 8) & 0xff);
	apollo_send_data(x1 & 0xff);

	apollo_send_data((y1 >> 8) & 0xff);
	apollo_send_data(y1 & 0xff);

	apollo_send_data((x2 >> 8) & 0xff);
	apollo_send_data(x2 & 0xff);

	apollo_send_data((y2 >> 8) & 0xff);
	apollo_send_data(y2 & 0xff);

	k = 0;
	t = 0;
	for (i = y1; i <= y2; i++)
		for (j = x1; j <= x2; j++) {
			t |= (buf[i * DPY_W + j] << (6 - (k % 4) * 2));
			k++;
			if (k % 4 == 0) {
				apollo_send_data(t);
				t = 0;
			}
		}

	apollo_send_command(APOLLO_STOP_LOADING);
	apollo_send_command(APOLLO_DISPLAY_PARTIAL_PICTURE);

	mutex_unlock(&par->lock);
}

/* this is called back from the deferred io workqueue */
static void lbookv3fb_dpy_deferred_io(struct fb_info *info,
				struct list_head *pagelist)
{

	int y1=0, y2=0;
	struct page *cur;
	int prev_index = -1;
	struct lbookv3fb_par *par = info->par;

	if (par->options.partial_update) {
		list_for_each_entry(cur, pagelist, lru) {
			debug("cur->index = %d\n", cur->index);
			if (prev_index == -1) {
				y1 = cur->index * PAGE_SIZE / DPY_W;
				y2 = ((cur->index + 1) * PAGE_SIZE - 1) / DPY_W;
				if (y2 >= DPY_H)
					y2 = DPY_H - 1;
				prev_index = cur->index;
				continue;
			}

			if (cur->index == prev_index + 1) {
				y2 = ((cur->index + 1) * PAGE_SIZE - 1) / DPY_W;
				if (y2 >= DPY_H)
					y2 = DPY_H - 1;
				prev_index = cur->index;
				continue;
			}

			if (cur->index == prev_index - 1) {
				y1 = cur->index * PAGE_SIZE / DPY_W;
				prev_index = cur->index;
				continue;
			}

			lbookv3fb_apollo_update_part(info->par, 0, y1, DPY_W - 1, y2);
			y1 = cur->index * PAGE_SIZE / DPY_W;
			y2 = ((cur->index + 1) * PAGE_SIZE - 1) / DPY_W;
			if (y2 >= DPY_H)
				y2 = DPY_H - 1;

			prev_index = cur->index;
		}
		lbookv3fb_apollo_update_part(info->par, 0, y1, DPY_W - 1, y2);
	} else
		lbookv3fb_dpy_update(info->par);
}

static void lbookv3fb_deferred_work(struct work_struct *work)
{
	struct lbookv3fb_par *par = container_of(work, struct lbookv3fb_par, deferred_work.work);

	debug("jiffies= %lu\n", jiffies);

	lbookv3fb_dpy_update(par);
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

	schedule_delayed_work(&par->deferred_work, info->fbdefio->delay);
}

static void lbookv3fb_copyarea(struct fb_info *info,
				   const struct fb_copyarea *area)
{
	struct lbookv3fb_par *par = info->par;

	debug("called\n", area);

	sys_copyarea(info, area);

	schedule_delayed_work(&par->deferred_work, info->fbdefio->delay);
}

static void lbookv3fb_imageblit(struct fb_info *info,
				const struct fb_image *image)
{
	struct lbookv3fb_par *par = info->par;

	debug("called, fb_info = %p\n", info);

	sys_imageblit(info, image);

	schedule_delayed_work(&par->deferred_work, info->fbdefio->delay);
}

int lbookv3fb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	return 0;
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

	schedule_delayed_work(&par->deferred_work, info->fbdefio->delay);

	if (count)
		return count;

	return err;
}

static int lbookv3fb_sync(struct fb_info *info)
{
	struct lbookv3fb_par *par = info->par;

//	lbookv3fb_dpy_update(par, 0);
	return 0;
}

static ssize_t lbookv3fb_wf_read(struct file *f, char __user *buf, size_t count, loff_t *f_pos)
{
	unsigned char data;
	char __user *p = buf;
	int i;
	struct lbookv3fb_par *par = f->private_data;

	mutex_lock(&par->lock);

	if (*f_pos > 1024*1024*2 - 1)
		return 0;

	if (*f_pos + count > 1024*1024*2)
		count = 1024*1024*2 - *f_pos;

	for (i = *f_pos; i < *f_pos + count; i++) {
		apollo_send_command(APOLLO_READ_FROM_FLASH);

		apollo_send_data((i >> 16) & 0xff);
		apollo_send_data((i >> 8) & 0xff);
		apollo_send_data(i & 0xff);

		apollo_set_gpa_14_15(1);
		data = apollo_read_data();
		apollo_set_gpa_14_15(0);

		if (copy_to_user(p, &data, 1)) {
			return -EFAULT;
		}
		p++;
	}

	mutex_unlock(&par->lock);

	*f_pos += count;
	return count;
}

static ssize_t lbookv3fb_wf_write(struct file *f, const char __user *buf, size_t count, loff_t *f_pos)
{
	unsigned char data;
	const char __user *p = buf;
	int i;
	struct lbookv3fb_par *par = f->private_data;

	mutex_lock(&par->lock);

	if (*f_pos > 1024*1024*2 - 1)
		return 0;

	if (*f_pos + count > 1024*1024*2)
		count = 1024*1024*2 - *f_pos;

	for (i = *f_pos; i < *f_pos + count; i++) {
		apollo_send_command(APOLLO_WRITE_TO_FLASH);

		apollo_send_data((i >> 16) & 0xff);
		apollo_send_data((i >> 8) & 0xff);
		apollo_send_data(i & 0xff);

		if (copy_to_user(&data, p, 1)) {
			return -EFAULT;
		}

		apollo_send_data(data);

		p++;
	}

	mutex_unlock(&par->lock);

	*f_pos += count;
	return count;
}

static int lbookv3fb_wf_open(struct inode *i, struct file *f)
{
	struct lbookv3fb_par *par;

	par = container_of(i->i_cdev, struct lbookv3fb_par, cdev);

	f->private_data = par;

	return 0;
}

static ssize_t lbookv3fb_temperature_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct lbookv3fb_par *par = info->par;
	char temp;

	mutex_lock(&par->lock);

	apollo_send_command(APOLLO_READ_TEMPERATURE);

	apollo_set_gpa_14_15(1);
	temp = apollo_read_data();
	apollo_set_gpa_14_15(0);

	mutex_unlock(&par->lock);

	sprintf(buf, "%d\n", temp);
	return strlen(buf) + 1;
}

static ssize_t lbookv3fb_manual_refresh_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct lbookv3fb_par *par = info->par;

	sprintf(buf, "%d\n", par->options.manual_refresh);
	return strlen(buf) + 1;
}

static ssize_t lbookv3fb_manual_refresh_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct lbookv3fb_par *par = info->par;
	char *after;
	unsigned long state = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;
	ssize_t ret = -EINVAL;

	if (*after && isspace(*after))
		count++;

	if ((count == size) && (state <= 1)) {
		ret = count;
		par->options.manual_refresh = state;
	}

	return ret;
}

static ssize_t lbookv3fb_partial_update_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct lbookv3fb_par *par = info->par;

	sprintf(buf, "%d\n", par->options.partial_update);
	return strlen(buf) + 1;
}

static ssize_t lbookv3fb_partial_update_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct lbookv3fb_par *par = info->par;
	char *after;
	unsigned long state = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;
	ssize_t ret = -EINVAL;

	if (*after && isspace(*after))
		count++;

	if ((count == size) && (state <= 1)) {
		ret = count;
		par->options.partial_update = state;
	}

	return ret;
}

static ssize_t lbookv3fb_defio_delay_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct lbookv3fb_par *par = info->par;

	sprintf(buf, "%lu\n", info->fbdefio->delay * 1000 / HZ);
	return strlen(buf) + 1;
}

static ssize_t lbookv3fb_defio_delay_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct lbookv3fb_par *par = info->par;
	char *after;
	unsigned long state = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;
	ssize_t ret = -EINVAL;

	if (*after && isspace(*after))
		count++;

	state = state * HZ / 1000;

	if (!state)
		state = 1;

	if (count == size) {
		ret = count;
		info->fbdefio->delay = state;
	}

	return ret;
}

DEVICE_ATTR(manual_refresh, 0666, lbookv3fb_manual_refresh_show, lbookv3fb_manual_refresh_store);
DEVICE_ATTR(partial_update, 0666, lbookv3fb_partial_update_show, lbookv3fb_partial_update_store);
DEVICE_ATTR(defio_delay, 0666, lbookv3fb_defio_delay_show, lbookv3fb_defio_delay_store);

static struct file_operations lbookv3fb_wf_fops = {
	.owner = THIS_MODULE,
	.open = lbookv3fb_wf_open,
	.read = lbookv3fb_wf_read,
	.write = lbookv3fb_wf_write,
};


static struct fb_ops lbookv3fb_ops = {
	.owner		= THIS_MODULE,
	.fb_read        = fb_sys_read,
	.fb_write	= lbookv3fb_write,
	.fb_fillrect	= lbookv3fb_fillrect,
	.fb_copyarea	= lbookv3fb_copyarea,
	.fb_imageblit	= lbookv3fb_imageblit,
	.fb_cursor	= lbookv3fb_cursor,
	.fb_sync	= lbookv3fb_sync,
};

static struct fb_deferred_io lbookv3fb_defio = {
	.delay		= HZ / 2,
	.deferred_io	= lbookv3fb_dpy_deferred_io,
};

DEVICE_ATTR(temperature, 0444, lbookv3fb_temperature_show, NULL);


static int __devinit lbookv3fb_setup_chrdev(struct lbookv3fb_par *par)
{
	int res = 0;
	struct cdev *cdev = &par->cdev;
	dev_t devno;

	res = alloc_chrdev_region(&devno, 0, 1, "apollo");
	if (res)
		goto err_alloc_chrdev_region;


	cdev_init(cdev, &lbookv3fb_wf_fops);

	res = cdev_add(cdev, devno, 1);
	if(res)
		goto err_cdev_add;

	return 0;

err_cdev_add:
	unregister_chrdev_region(devno, 1);
err_alloc_chrdev_region:

	return res;
}

static void lbookv3fb_remove_chrdev(struct lbookv3fb_par *par)
{
	cdev_del(&par->cdev);
	unregister_chrdev_region(par->cdev.dev, 1);
}

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
	mutex_init(&par->lock);
	INIT_DELAYED_WORK(&par->deferred_work, lbookv3fb_deferred_work);
	par->options.manual_refresh = 0;
	par->options.partial_update = 1;

	info->flags = FBINFO_FLAG_DEFAULT;

	info->fbdefio = &lbookv3fb_defio;
	fb_deferred_io_init(info);

	fb_alloc_cmap(&info->cmap, 4, 0);

	apollo_setuphw();
	apollo_reinitialize();

	apollo_send_command(APOLLO_SET_DEPTH);
	apollo_send_data(0x02);
	apollo_send_command(APOLLO_ERASE_DISPLAY);
	apollo_send_data(0x01);

	retval = register_framebuffer(info);
	if (retval < 0)
		goto err1;
	platform_set_drvdata(dev, info);

	printk(KERN_INFO
	       "fb%d: lbookv3 frame buffer device, using %dK of video memory\n",
	       info->node, videomemorysize >> 10);


	if ((retval = lbookv3fb_setup_chrdev(par)))
		goto err2;

	retval = device_create_file(info->dev, &dev_attr_temperature);
	if(retval)
		goto err_devattr_temperature;

	retval = device_create_file(info->dev, &dev_attr_manual_refresh);
	if(retval)
		goto err_devattr_manref;

	retval = device_create_file(info->dev, &dev_attr_partial_update);
	if(retval)
		goto err_devattr_partupd;

	retval = device_create_file(info->dev, &dev_attr_defio_delay);
	if(retval)
		goto err_devattr_defio_delay;

	return 0;


	device_remove_file(info->dev, &dev_attr_defio_delay);
err_devattr_defio_delay:
	device_remove_file(info->dev, &dev_attr_partial_update);
err_devattr_partupd:
	device_remove_file(info->dev, &dev_attr_manual_refresh);
err_devattr_manref:
	device_remove_file(info->dev, &dev_attr_temperature);
err_devattr_temperature:
	lbookv3fb_remove_chrdev(par);
err2:
	unregister_framebuffer(info);
err1:
	framebuffer_release(info);
err:
	vfree(videomemory);
	return retval;
}

static int __devexit lbookv3fb_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct lbookv3fb_par *par = info->par;

	if (info) {
		fb_deferred_io_cleanup(info);
		cancel_delayed_work(&par->deferred_work);
		flush_scheduled_work();

		device_remove_file(info->dev, &dev_attr_manual_refresh);
		device_remove_file(info->dev, &dev_attr_partial_update);
		device_remove_file(info->dev, &dev_attr_defio_delay);
		device_remove_file(info->dev, &dev_attr_temperature);
		unregister_framebuffer(info);
		vfree((void __force *)info->screen_base);
		lbookv3fb_remove_chrdev(info->par);
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
