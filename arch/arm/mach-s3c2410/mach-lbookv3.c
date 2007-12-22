/* linux/arch/arm/mach-s3c2410/mach-smdk2410.c
 *
 * linux/arch/arm/mach-s3c2410/mach-smdk2410.c
 *
 * Copyright (C) 2004 by FS Forth-Systeme GmbH
 * All rights reserved.
 *
 * $Id: mach-smdk2410.c,v 1.1 2004/05/11 14:15:38 mpietrek Exp $
 * @Author: Jonas Dietsche
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 * @History:
 * derived from linux/arch/arm/mach-s3c2410/mach-bast.c, written by
 * Ben Dooks <ben@simtec.co.uk>
 *
 ***********************************************************************/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/arch/leds-gpio.h>
#include <asm/arch/regs-gpio.h>

#include <asm/plat-s3c/regs-serial.h>
#include <asm/plat-s3c/nand.h>

#include <asm/plat-s3c24xx/devs.h>
#include <asm/plat-s3c24xx/cpu.h>

#include <asm/plat-s3c24xx/common-smdk.h>

static struct map_desc lbookv3_iodesc[] __initdata = {
  /* nothing here yet */
};

#define UCON S3C2410_UCON_DEFAULT
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg lbookv3_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
};

static struct s3c24xx_led_platdata lbookv3_pdata_led_red = {
	.gpio		= S3C2410_GPC5,
	.flags		= 0,
	.name		= "led-red",
//	.def_trigger	= "timer",
};

static struct s3c24xx_led_platdata lbookv3_pdata_led_green = {
	.gpio		= S3C2410_GPC6,
	.flags		= 0,
	.name		= "led-green",
//	.def_trigger	= "nand-disk",
};

static struct platform_device lbookv3_led_red = {
	.name		= "s3c24xx_led",
	.id		= 1,
	.dev		= {
		.platform_data = &lbookv3_pdata_led_red,
	},
};

static struct platform_device lbookv3_led_green = {
	.name		= "s3c24xx_led",
	.id		= 2,
	.dev		= {
		.platform_data = &lbookv3_pdata_led_green,
	},
};

static struct mtd_partition lbookv3_nand_part[] = {
	[0] = {
		.name	= "KERNEL",
		.offset = SZ_1M,
		.size	= SZ_1M,
	},
	[1] = {
		.name	= "BASEFS",
		.offset = SZ_2M,
		.size	= SZ_1M*6,
	},
	[2] = {
		.name	= "ROOTFS",
		.offset	= SZ_8M,
		.size	= SZ_1M*44,
	},
	[3] = {
		.name	= "LOGO",
		.offset = SZ_1M * 52,
		.size	= SZ_1M,
	},
	[4] = {
		.name	= "USERDATA",
		.offset	= SZ_1M * 0x35,
		.size	= SZ_2M,
	},
	[5] = {
		.name	= "STORAGE",
		.offset	= SZ_1M * 0x37,
		.size	= SZ_1M * 9,
	},
	[6] = {
		.name	= "UNKNOWN",
		.size	= SZ_1M,
		.offset	= 0,
	},
};

static struct s3c2410_nand_set lbookv3_nand_sets[] = {
	[0] = {
		.name		= "NAND",
		.nr_chips	= 1,
		.nr_partitions	= ARRAY_SIZE(lbookv3_nand_part),
		.partitions	= lbookv3_nand_part
	},
};

/* choose a set of timings which should suit most 512Mbit
 * chips and beyond.
*/

static struct s3c2410_platform_nand lbookv3_nand_info = {
	.tacls		= 30,
	.twrph0		= 60,
	.twrph1		= 30,
	.nr_sets	= ARRAY_SIZE(lbookv3_nand_sets),
	.sets		= lbookv3_nand_sets,
};

static struct resource lbookv3_nor_resource[] = {
	[0] = {
		.start = S3C2410_CS0,
		.end   = S3C2410_CS0 + SZ_2M - 1,
		.flags = IORESOURCE_MEM,
	}
};

static struct mtd_partition lbookv3_nor_part[] = {
	{
		.name		= "Bootloader",
		.size		= SZ_2M,
		.offset		= 0,
		.mask_flags	= MTD_WRITEABLE,
	}
};

static struct physmap_flash_data lbookv3_nor_flash_data = {
	.width		= 2,
	.parts		= lbookv3_nor_part,
	.nr_parts	= ARRAY_SIZE(lbookv3_nor_part),
};

static struct platform_device lbookv3_device_nor = {
	.name		= "physmap-flash",
	.id		= -1,
	.dev = {
		.platform_data = &lbookv3_nor_flash_data,
	},
	.num_resources	= ARRAY_SIZE(lbookv3_nor_resource),
	.resource	= lbookv3_nor_resource,
};


static struct platform_device *lbookv3_devices[] __initdata = {
//	&s3c_device_usb,
//	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
	&s3c_device_usbgadget,
	&s3c_device_nand,
	&s3c_device_rtc,
	&s3c_device_adc,
	&s3c_device_hsmmc,
	&s3c_device_spi0,
	&s3c_device_timer0,
	&s3c_device_sdi,
	&lbookv3_led_red,
	&lbookv3_led_green,
	&lbookv3_device_nor
};

static void __init lbookv3_map_io(void)
{
	s3c24xx_init_io(lbookv3_iodesc, ARRAY_SIZE(lbookv3_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(lbookv3_uartcfgs, ARRAY_SIZE(lbookv3_uartcfgs));
}

static void __init lbookv3_init(void)
{

	s3c2410_gpio_cfgpin(S3C2410_GPC5, S3C2410_GPC5_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPC6, S3C2410_GPC6_OUTP);

	s3c2410_gpio_setpin(S3C2410_GPC5, 0);
	s3c2410_gpio_setpin(S3C2410_GPC6, 0);

	s3c_device_nand.dev.platform_data = &lbookv3_nand_info;
	platform_add_devices(lbookv3_devices, ARRAY_SIZE(lbookv3_devices));
//	smdk_machine_init();
}

MACHINE_START(LBOOK_V3, "LBOOK_V3") /* @TODO: request a new identifier and switch
				    * to LBOOK_V3 */
	/* Maintainer: Yauhen Kharuzhy */
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= lbookv3_map_io,
	.init_irq	= s3c24xx_init_irq,
	.init_machine	= lbookv3_init,
	.timer		= &s3c24xx_timer,
MACHINE_END


