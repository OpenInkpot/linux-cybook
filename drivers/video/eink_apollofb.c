/*
 * linux/drivers/video/apollofb.c -- FB driver for lBook/Jinke eReader V3
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

/* #define DEBUG */

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
#include <linux/eink_apollofb.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/device.h>

#include <mach/regs-gpio.h>
#include <mach/hardware.h>

/* Display specific information */
#define DPY_W 600
#define DPY_H 800

#define is_portrait(var) (!(var.rotate % 180))

struct apollofb_options {
	unsigned int manual_refresh_thr;
	unsigned int use_sleep_mode:1;
};

struct apollofb_par {
	struct fb_info *info;
	struct mutex lock;
	struct delayed_work deferred_work;
	struct cdev cdev;
	struct apollofb_options options;
	int current_mode;
	struct eink_apollo_operations *ops;
	int standby;
};

static struct fb_fix_screeninfo apollofb_fix __devinitdata = {
	.id =		"apollofb",
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_STATIC_PSEUDOCOLOR,
	.xpanstep =	0,
	.ypanstep =	0,
	.ywrapstep =	0,
	.line_length =	DPY_W / (8 / 8),
	.accel =	FB_ACCEL_NONE,
};

static struct fb_var_screeninfo apollofb_var __devinitdata = {
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

static inline int apollo_wait_for_ack_value(struct apollofb_par *par,
		unsigned int value)
{
	unsigned long timeout = jiffies + 2 * HZ;

	while (par->ops->get_ctl_pin(H_ACK) != value)
		if (time_before(jiffies, timeout)) {
		} else {
			printk(KERN_ERR "%s: Wait for H_ACK == %u, timeout\n",
					__func__, value);
			return 1;
		}

	return 0;
}

#define apollo_wait_for_ack(par)	apollo_wait_for_ack_value(par, 0)
#define apollo_wait_for_ack_clear(par)	apollo_wait_for_ack_value(par, 1)

static int apollo_send_data(struct apollofb_par *par, unsigned char data)
{
	int res = 0;;

	res = apollo_wait_for_ack_clear(par);
	par->ops->write_value(data);
	par->ops->set_ctl_pin(H_DS, 0);
	if (!res)
		res = apollo_wait_for_ack(par);
	par->ops->set_ctl_pin(H_DS, 1);

	return res;
}


static int apollo_send_command(struct apollofb_par *par, unsigned char cmd)
{
	int res;

	par->ops->set_ctl_pin(H_CD, 1);
	res = apollo_send_data(par, cmd);
	par->ops->set_ctl_pin(H_CD, 0);
	return res;
}

static unsigned char apollo_read_data(struct apollofb_par *par)
{
	unsigned char res;

	par->ops->set_ctl_pin(H_RW, 1);
	par->ops->set_ctl_pin(H_DS, 0);
	apollo_wait_for_ack(par);
	res = par->ops->read_value();
	par->ops->set_ctl_pin(H_DS, 1);
	apollo_wait_for_ack_clear(par);
	par->ops->set_ctl_pin(H_RW, 0);

	return res;
}

static void apollo_set_sleep_mode(struct apollofb_par *par)
{
	apollo_send_command(par, APOLLO_SLEEP_MODE);
	par->current_mode = APOLLO_STATUS_MODE_SLEEP;
}

static void apollo_set_normal_mode(struct apollofb_par *par)
{
	par->ops->set_ctl_pin(H_CD, 0);
	par->ops->set_ctl_pin(H_RW, 0);

	apollo_send_command(par, APOLLO_NORMAL_MODE);
	apollo_send_command(par, APOLLO_ORIENTATION);
	apollo_send_data(par, ((par->info->var.rotate + 90) % 360) / 90);

	par->current_mode = APOLLO_STATUS_MODE_NORMAL;
}

static void apollo_wakeup(struct apollofb_par *par)
{
	udelay(600); /* in case we were just powered off */
	par->ops->set_ctl_pin(H_WUP, 1);
	udelay(100);
	par->ops->set_ctl_pin(H_DS, 0);
	apollo_wait_for_ack(par);
	par->ops->set_ctl_pin(H_WUP, 0);
	par->ops->set_ctl_pin(H_DS, 1);
	apollo_wait_for_ack_clear(par);
}

static void apollofb_apollo_update_part(struct apollofb_par *par,
		unsigned int x1, unsigned int y1,
		unsigned int x2, unsigned int y2,
		int last_fragment)
{
	int i, j, k;
	struct fb_info *info = par->info;
	unsigned int width = is_portrait(info->var) ? info->var.xres :
							info->var.yres;
	unsigned int bpp = info->var.green.length;
	unsigned int pixels_in_byte = 8 / bpp;
	unsigned char *buf = (unsigned char __force *)info->screen_base;
	unsigned char tmp, mask;

	dev_dbg(info->dev, "%s called\n", __FUNCTION__);
	y1 -= y1 % 4;

	if ((y2 + 1) % 4)
		y2 += 4 - ((y2 + 1) % 4);

	x1 -= x1 % 4;

	if ((x2 + 1) % 4)
		x2 += 4 - ((x2 + 1) % 4);

	mask = 0;
	for (i = 0; i < bpp; i++)
		mask = (mask << 1) | 1;

	if (par->current_mode == APOLLO_STATUS_MODE_SLEEP)
		apollo_set_normal_mode(par);

	apollo_send_command(par, APOLLO_LOAD_PARTIAL_PICTURE);
	apollo_send_data(par, (x1 >> 8) & 0xff);
	apollo_send_data(par, x1 & 0xff);
	apollo_send_data(par, (y1 >> 8) & 0xff);
	apollo_send_data(par, y1 & 0xff);
	apollo_send_data(par, (x2 >> 8) & 0xff);
	apollo_send_data(par, x2 & 0xff);
	apollo_send_data(par, (y2 >> 8) & 0xff);
	apollo_send_data(par, y2 & 0xff);

	k = 0;
	tmp = 0;
	for (i = y1; i <= y2; i++)
		for (j = x1; j <= x2; j++) {
			tmp = (tmp << bpp) | (buf[i * width + j] & mask);
			k++;
			if (k % pixels_in_byte == 0)
				apollo_send_data(par, tmp);
		}

	dev_dbg(info->dev, "%s: stop loading\n", __FUNCTION__);
	apollo_send_command(par, APOLLO_STOP_LOADING);
	apollo_send_command(par, APOLLO_DISPLAY_PARTIAL_PICTURE);

	if (last_fragment) {
		apollo_send_command(par, APOLLO_CANCEL_AUTO_REFRESH);

		if (par->options.use_sleep_mode)
			apollo_set_sleep_mode(par);
	}

	dev_dbg(info->dev, "%s finished\n", __FUNCTION__);
}

/* this is called back from the deferred io workqueue */
static void apollofb_dpy_deferred_io(struct fb_info *info,
				struct list_head *pagelist)
{

	struct apollofb_par *par = info->par;
	unsigned int width = is_portrait(info->var) ? info->var.xres :
							info->var.yres;
	unsigned int height = is_portrait(info->var) ? info->var.yres :
							info->var.xres;
	unsigned long int start_page = -1, end_page = -1;
	unsigned int y1 = 0, y2 = 0;
	struct page *cur;
	unsigned int pages_count = 0;

	dev_dbg(info->dev, "%s called\n", __FUNCTION__);

	list_for_each_entry(cur, pagelist, lru) {
		pages_count++;
	}

	mutex_lock(&par->lock);

	if (par->current_mode == APOLLO_STATUS_MODE_SLEEP)
		apollo_set_normal_mode(par);

	if (pages_count >= par->options.manual_refresh_thr) {
		apollo_send_command(par, APOLLO_AUTO_REFRESH);
		apollo_send_command(par, APOLLO_MANUAL_REFRESH);
		apollofb_apollo_update_part(par, 0, 0, width - 1, height - 1, 1);
	} else {

		list_for_each_entry(cur, pagelist, lru) {

			if (start_page == -1) {
				start_page = cur->index;
				end_page = cur->index;
				continue;
			}

			if (cur->index == end_page + 1) {
				end_page++;
			} else {
				y1 = start_page * PAGE_SIZE / width;
				y2 = ((end_page + 1) * PAGE_SIZE - 1) / width;
				if (y2 >= height)
					y2 = height - 1;

				apollofb_apollo_update_part(par, 0, y1, width - 1, y2, 0);

				start_page = cur->index;
				end_page = cur->index;
			}
		}

		y1 = start_page * PAGE_SIZE / width;
		y2 = ((end_page + 1) * PAGE_SIZE - 1) / width;
		if (y2 >= height)
			y2 = height - 1;

		apollofb_apollo_update_part(par, 0, y1,	width - 1, y2, 1);
	}
	mutex_unlock(&par->lock);

	dev_dbg(info->dev, "%s finished\n", __FUNCTION__);
}

static void apollofb_deferred_work(struct work_struct *work)
{
	struct apollofb_par *par = container_of(work, struct apollofb_par,
			deferred_work.work);
	struct fb_info *info = par->info;
	unsigned int width = is_portrait(info->var) ? info->var.xres :
							info->var.yres;
	unsigned int height = is_portrait(info->var) ? info->var.yres :
							info->var.xres;

	apollofb_apollo_update_part(par, 0, 0, width - 1, height - 1, 1);
}

static void apollofb_fillrect(struct fb_info *info,
				   const struct fb_fillrect *rect)
{
	struct apollofb_par *par = info->par;

	sys_fillrect(info, rect);

	schedule_delayed_work(&par->deferred_work, info->fbdefio->delay);
}

static void apollofb_copyarea(struct fb_info *info,
				   const struct fb_copyarea *area)
{
	struct apollofb_par *par = info->par;

	sys_copyarea(info, area);

	schedule_delayed_work(&par->deferred_work, info->fbdefio->delay);
}

static void apollofb_imageblit(struct fb_info *info,
				const struct fb_image *image)
{
	struct apollofb_par *par = info->par;

	sys_imageblit(info, image);

	schedule_delayed_work(&par->deferred_work, info->fbdefio->delay);
}

int apollofb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	return 0;
}

/*
 * this is the slow path from userspace. they can seek and write to
 * the fb. it's inefficient to do anything less than a full screen draw
 */
static ssize_t apollofb_write(struct fb_info *info, const char __user *buf,
				size_t count, loff_t *ppos)
{
	unsigned long p;
	int err = -EINVAL;
	struct apollofb_par *par;
	unsigned int xres;
	unsigned int fbmemlength;

	dev_dbg(info->dev, "%s started\n", __FUNCTION__);

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

	dev_dbg(info->dev, "%s finished\n", __FUNCTION__);

	if (count)
		return count;

	return err;
}

static int apollofb_sync(struct fb_info *info)
{
	return 0;
}

static int apollofb_blank(int blank, struct fb_info *info)
{
	return 0;
}

static int apollofb_check_var(struct fb_var_screeninfo *var,
			      struct fb_info *info)
{
	switch (var->bits_per_pixel) {
	case 1:
		var->red.length = 1;
		var->red.offset = 0;
		var->green.length = 1;
		var->green.offset = 0;
		var->blue.length = 1;
		var->blue.offset = 0;
		var->transp.length = 0;
		var->transp.offset = 0;
		break;
	default:
		var->bits_per_pixel = 8;
		var->grayscale = 1;
		var->red.length = 2;
		var->red.offset = 0;
		var->green.length = 2;
		var->green.offset = 0;
		var->blue.length = 2;
		var->blue.offset = 0;
		var->transp.length = 0;
		var->transp.offset = 0;
		break;
	}

	var->xres = DPY_W;
	var->xres_virtual = DPY_W;
	var->yres = DPY_H;
	var->yres_virtual = DPY_H;

	if (var->rotate % 90)
		var->rotate -= var->rotate % 90;

	return 0;
}

static int apollofb_set_par(struct fb_info *info)
{
	struct apollofb_par *par = info->par;

	switch (info->var.bits_per_pixel) {
	case 1:
		info->fix.visual = FB_VISUAL_MONO01;
		apollo_send_command(par, APOLLO_SET_DEPTH);
		apollo_send_data(par, 0x00);
		break;
	default:
		info->fix.visual = FB_VISUAL_STATIC_PSEUDOCOLOR;
		apollo_send_command(par, APOLLO_SET_DEPTH);
		apollo_send_data(par, 0x02);
		break;
	}

	mutex_lock(&par->lock);
	apollo_send_command(par, APOLLO_ORIENTATION);
	apollo_send_data(par, ((info->var.rotate + 90) % 360) / 90);
	mutex_unlock(&par->lock);

	return 0;
}

static ssize_t apollofb_wf_read(struct file *f, char __user *buf,
				size_t count, loff_t *f_pos)
{
	unsigned char data;
	char __user *p = buf;
	int i, ret = 0;
	struct apollofb_par *par = f->private_data;

	mutex_lock(&par->lock);
	if (par->current_mode == APOLLO_STATUS_MODE_SLEEP)
		apollo_set_normal_mode(par);

	if (*f_pos > APOLLO_WAVEFORMS_FLASH_SIZE - 1)
		goto err;

	if (*f_pos + count > APOLLO_WAVEFORMS_FLASH_SIZE)
		count = APOLLO_WAVEFORMS_FLASH_SIZE - *f_pos;

	for (i = *f_pos; i < *f_pos + count; i++) {
		apollo_send_command(par, APOLLO_READ_FROM_FLASH);

		apollo_send_data(par, (i >> 16) & 0xff);
		apollo_send_data(par, (i >> 8) & 0xff);
		apollo_send_data(par, i & 0xff);

		data = apollo_read_data(par);

		if (copy_to_user(p, &data, 1)) {
			ret = -EFAULT;
			goto err;
		}

		p++;
	}

	if (par->options.use_sleep_mode)
		apollo_set_sleep_mode(par);

	*f_pos += count;
	ret = count;
err:
	mutex_unlock(&par->lock);

	return ret;
}

static ssize_t apollofb_wf_write(struct file *f, const char __user *buf,
				size_t count, loff_t *f_pos)
{
	unsigned char data;
	const char __user *p = buf;
	int i;
	struct apollofb_par *par = f->private_data;
	int ret = 0;

	mutex_lock(&par->lock);

	if (par->current_mode == APOLLO_STATUS_MODE_SLEEP)
		apollo_set_normal_mode(par);

	if (*f_pos > APOLLO_WAVEFORMS_FLASH_SIZE - 1)
		goto err;

	if (*f_pos + count > APOLLO_WAVEFORMS_FLASH_SIZE)
		count = APOLLO_WAVEFORMS_FLASH_SIZE - *f_pos;

	printk(KERN_INFO "*f_pos = %lld, count = %lu\n", *f_pos, count);

	for (i = *f_pos; i < *f_pos + count; i++) {
		if (copy_from_user(&data, p, 1)) {
			ret = -EFAULT;
			goto err;
		}

		apollo_send_command(par, APOLLO_WRITE_TO_FLASH);
		apollo_send_data(par, (i >> 16) & 0xff);
		apollo_send_data(par, (i >> 8) & 0xff);
		apollo_send_data(par, i & 0xff);

		apollo_send_data(par, data);

		p++;
	}

	if (par->options.use_sleep_mode)
		apollo_set_sleep_mode(par);

	*f_pos += count;
	ret = count;

err:
	mutex_unlock(&par->lock);

	return ret;
}

static int apollofb_wf_open(struct inode *i, struct file *f)
{
	struct apollofb_par *par;

	par = container_of(i->i_cdev, struct apollofb_par, cdev);

	f->private_data = par;

	return 0;
}

static ssize_t apollofb_temperature_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct apollofb_par *par = info->par;
	char temp;

	mutex_lock(&par->lock);

	apollo_send_command(par, APOLLO_READ_TEMPERATURE);

	temp = apollo_read_data(par);

	mutex_unlock(&par->lock);

	sprintf(buf, "%d\n", temp);
	return strlen(buf) + 1;
}

static unsigned int apollofb_get_screenpages_count(struct fb_info *info)
{
	return info->fix.smem_len % PAGE_SIZE ?
		info->fix.smem_len / PAGE_SIZE +1 :
		info->fix.smem_len / PAGE_SIZE;
}

static ssize_t apollofb_manual_refresh_thr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct apollofb_par *par = info->par;

	return sprintf(buf, "%u\n", par->options.manual_refresh_thr);
}

static ssize_t apollofb_manual_refresh_thr_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct apollofb_par *par = info->par;
	unsigned int val;

	if ((sscanf(buf, "%u", &val) == 1) &&
			(val > 0) &&
			(val <= apollofb_get_screenpages_count(info))) {
		par->options.manual_refresh_thr = val;
		return size;
	}

	return -EINVAL;
}

static ssize_t apollofb_manual_refresh_thr_max_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct apollofb_par *par = info->par;

	return sprintf(buf, "%u\n", apollofb_get_screenpages_count(info));
}

static ssize_t apollofb_use_sleep_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct apollofb_par *par = info->par;

	sprintf(buf, "%d\n", par->options.use_sleep_mode);
	return strlen(buf) + 1;
}

static ssize_t apollofb_use_sleep_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct apollofb_par *par = info->par;
	char *after;
	unsigned long state = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;
	ssize_t ret = -EINVAL;

	if (*after && isspace(*after))
		count++;

	if ((count == size) && (state <= 1)) {
		ret = count;
		par->options.use_sleep_mode = state;

		mutex_lock(&par->lock);

		if (state)
			apollo_set_sleep_mode(par);
		else
			apollo_set_normal_mode(par);

		mutex_unlock(&par->lock);

	}

	return ret;
}

static ssize_t apollofb_defio_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);

	sprintf(buf, "%lu\n", info->fbdefio->delay * 1000 / HZ);
	return strlen(buf) + 1;
}

static ssize_t apollofb_defio_delay_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct fb_info *info = dev_get_drvdata(dev);
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

DEVICE_ATTR(manual_refresh_threshold_max, 0444,
		apollofb_manual_refresh_thr_max_show, NULL);
DEVICE_ATTR(manual_refresh_threshold, 0666,
		apollofb_manual_refresh_thr_show, apollofb_manual_refresh_thr_store);
DEVICE_ATTR(defio_delay, 0666,
		apollofb_defio_delay_show, apollofb_defio_delay_store);
DEVICE_ATTR(use_sleep_mode, 0666,
		apollofb_use_sleep_mode_show, apollofb_use_sleep_mode_store);

static struct file_operations apollofb_wf_fops = {
	.owner = THIS_MODULE,
	.open = apollofb_wf_open,
	.read = apollofb_wf_read,
	.write = apollofb_wf_write,
};


static struct fb_ops apollofb_ops = {
	.owner		= THIS_MODULE,
	.fb_read        = fb_sys_read,
	.fb_write	= apollofb_write,
	.fb_fillrect	= apollofb_fillrect,
	.fb_copyarea	= apollofb_copyarea,
	.fb_imageblit	= apollofb_imageblit,
	.fb_cursor	= apollofb_cursor,
	.fb_sync	= apollofb_sync,
	.fb_check_var	= apollofb_check_var,
	.fb_set_par	= apollofb_set_par,
	.fb_blank	= apollofb_blank,
};

static struct fb_deferred_io apollofb_defio = {
	.delay		= HZ / 2,
	.deferred_io	= apollofb_dpy_deferred_io,
};

DEVICE_ATTR(temperature, 0444, apollofb_temperature_show, NULL);


static int __devinit apollofb_setup_chrdev(struct apollofb_par *par)
{
	int res = 0;
	struct cdev *cdev = &par->cdev;
	dev_t devno;

	res = alloc_chrdev_region(&devno, 0, 1, "apollo");
	if (res)
		goto err_alloc_chrdev_region;


	cdev_init(cdev, &apollofb_wf_fops);

	res = cdev_add(cdev, devno, 1);
	if (res)
		goto err_cdev_add;

	return 0;

err_cdev_add:
	unregister_chrdev_region(devno, 1);
err_alloc_chrdev_region:

	return res;
}

static void apollofb_remove_chrdev(struct apollofb_par *par)
{
	cdev_del(&par->cdev);
	unregister_chrdev_region(par->cdev.dev, 1);
}

static u16 red4[] __read_mostly = {
    0x0000, 0x5555, 0xaaaa, 0xffff
};
static u16 green4[] __read_mostly = {
    0x0000, 0x5555, 0xaaaa, 0xffff
};
static u16 blue4[] __read_mostly = {
    0x0000, 0x5555, 0xaaaa, 0xffff
};

static const struct fb_cmap eink_apollofb_4_colors = {
	    .len = 4, .red = red4, .green = green4, .blue = blue4
};

static int __devinit apollofb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	int retval = -ENOMEM;
	int videomemorysize;
	unsigned char *videomemory;
	struct apollofb_par *par;
	struct eink_apollofb_platdata *pdata = dev->dev.platform_data;
	unsigned char apollo_display_size;

	videomemorysize = (DPY_W * DPY_H)/8 * apollofb_var.bits_per_pixel;

	videomemory = vmalloc(videomemorysize);
	if (!videomemory)
		return retval;

	memset(videomemory, 0xFF, videomemorysize);

	info = framebuffer_alloc(sizeof(struct apollofb_par), &dev->dev);
	if (!info)
		goto err;

	if (!pdata->ops.set_ctl_pin ||
			!pdata->ops.get_ctl_pin ||
			!pdata->ops.read_value ||
			!pdata->ops.write_value) {
		retval = -EINVAL;
		dev_err(&dev->dev,
				"Invalid platform data: missing operations\n");
		goto err1;
	}

	info->screen_base = (char __iomem *) videomemory;
	info->fbops = &apollofb_ops;

	info->var = apollofb_var;
	info->fix = apollofb_fix;
	info->fix.smem_len = videomemorysize;
	par = info->par;
	par->info = info;
	mutex_init(&par->lock);
	INIT_DELAYED_WORK(&par->deferred_work, apollofb_deferred_work);
	par->options.manual_refresh_thr = apollofb_get_screenpages_count(info) / 2;
	par->options.use_sleep_mode = 0;
	par->ops = &pdata->ops;

	info->flags = FBINFO_FLAG_DEFAULT;

	if (pdata->defio_delay)
		apollofb_defio.delay = pdata->defio_delay;
	info->fbdefio = &apollofb_defio;
	fb_deferred_io_init(info);

	fb_alloc_cmap(&info->cmap, 4, 0);
	fb_copy_cmap(&eink_apollofb_4_colors, &info->cmap);

	if (par->ops->initialize)
		par->ops->initialize();

	par->ops->set_ctl_pin(H_CD, 0);
	par->ops->set_ctl_pin(H_RW, 0);

	mutex_lock(&par->lock);
	if (apollo_send_command(par, APOLLO_DISPLAY_SIZE)) {
		dev_err(&dev->dev, "Apollo controller is not detected.\n");
		mutex_unlock(&par->lock);
		goto err1;
	}

	apollo_display_size = apollo_read_data(par);
	if (apollo_display_size != 0x22) {
		dev_err(&dev->dev, "Unknown or missing eInk controller, "
				"display size byte is 0x%02x\n",
				apollo_display_size);
		mutex_unlock(&par->lock);
		goto err1;
	}

	apollo_set_normal_mode(par);
	apollo_send_command(par, APOLLO_SET_DEPTH);
	apollo_send_data(par, 0x02);
	apollo_send_command(par, APOLLO_ERASE_DISPLAY);
	apollo_send_data(par, 0x01);
	apollo_send_command(par, APOLLO_CANCEL_AUTO_REFRESH);
	if (par->options.use_sleep_mode)
		apollo_set_sleep_mode(par);
	mutex_unlock(&par->lock);

	retval = register_framebuffer(info);
	if (retval < 0)
		goto err1;
	platform_set_drvdata(dev, info);

	printk(KERN_INFO
	       "fb%d: eInk Apollo frame buffer device,"
	       "using %dK of video memory (%p)\n",
	       info->node, videomemorysize >> 10, videomemory);


	retval = apollofb_setup_chrdev(par);
	if (retval)
		goto err2;

	retval = device_create_file(info->dev, &dev_attr_temperature);
	if (retval)
		goto err_devattr_temperature;

	retval = device_create_file(info->dev, &dev_attr_manual_refresh_threshold);
	if (retval)
		goto err_devattr_manref;

	retval = device_create_file(info->dev, &dev_attr_manual_refresh_threshold_max);
	if (retval)
		goto err_devattr_manref_max;

	retval = device_create_file(info->dev, &dev_attr_defio_delay);
	if (retval)
		goto err_devattr_defio_delay;

	retval = device_create_file(info->dev, &dev_attr_use_sleep_mode);
	if (retval)
		goto err_devattr_use_sleep_mode;

	return 0;


	device_remove_file(info->dev, &dev_attr_use_sleep_mode);
err_devattr_use_sleep_mode:
	device_remove_file(info->dev, &dev_attr_defio_delay);
err_devattr_defio_delay:
	device_remove_file(info->dev, &dev_attr_manual_refresh_threshold_max);
err_devattr_manref_max:
	device_remove_file(info->dev, &dev_attr_manual_refresh_threshold);
err_devattr_manref:
	device_remove_file(info->dev, &dev_attr_temperature);
err_devattr_temperature:
	apollofb_remove_chrdev(par);
err2:
	unregister_framebuffer(info);
err1:
	framebuffer_release(info);
err:
	vfree(videomemory);
	return retval;
}

static int __devexit apollofb_remove(struct platform_device *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct apollofb_par *par = info->par;

	if (info) {
		fb_deferred_io_cleanup(info);
		cancel_delayed_work(&par->deferred_work);
		flush_scheduled_work();

		device_remove_file(info->dev, &dev_attr_use_sleep_mode);
		device_remove_file(info->dev, &dev_attr_manual_refresh_threshold_max);
		device_remove_file(info->dev, &dev_attr_manual_refresh_threshold);
		device_remove_file(info->dev, &dev_attr_defio_delay);
		device_remove_file(info->dev, &dev_attr_temperature);
		unregister_framebuffer(info);
		vfree((void __force *)info->screen_base);
		apollofb_remove_chrdev(info->par);
		framebuffer_release(info);
	}
	return 0;
}

#ifdef CONFIG_PM
static int apollofb_suspend(struct platform_device *pdev, pm_message_t message)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	struct apollofb_par *par = info->par;

	mutex_lock(&par->lock);
	apollo_send_command(par, APOLLO_STANDBY_MODE);
	par->current_mode = APOLLO_STATUS_MODE_SLEEP;
	mutex_unlock(&par->lock);

	return 0;
}

static int apollofb_resume(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	struct apollofb_par *par = info->par;

	mutex_lock(&par->lock);
	apollo_wakeup(par);
	if (!par->options.use_sleep_mode)
		apollo_set_normal_mode(par);
	mutex_unlock(&par->lock);

	return 0;
}
#endif


static struct platform_driver apollofb_driver = {
	.probe	= apollofb_probe,
	.remove = apollofb_remove,
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "eink-apollo",
	},
#ifdef CONFIG_PM
	.suspend = apollofb_suspend,
	.resume = apollofb_resume,
#endif
};

static int __init apollofb_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&apollofb_driver);

	return ret;
}

static void __exit apollofb_exit(void)
{
	platform_driver_unregister(&apollofb_driver);
}


module_init(apollofb_init);
module_exit(apollofb_exit);

MODULE_DESCRIPTION("fbdev driver for Apollo eInk display controller");
MODULE_AUTHOR("Yauhen Kharuzhy");
MODULE_LICENSE("GPL");
