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
#include <linux/mmc/host.h>
#include <linux/eink_apollofb.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/delay.h>

#include <mach/leds-gpio.h>
#include <mach/regs-gpio.h>
#include <mach/regs-clock.h>

#include <asm/plat-s3c/regs-serial.h>
#include <asm/plat-s3c/nand.h>

#include <asm/plat-s3c24xx/devs.h>
#include <asm/plat-s3c24xx/cpu.h>
#include <asm/plat-s3c24xx/udc.h>
#include <asm/plat-s3c24xx/pm.h>
#include <asm/plat-s3c24xx/mci.h>


#include <asm/plat-s3c24xx/common-smdk.h>

static struct map_desc lbookv3_iodesc[] __initdata = {
	{0xe8000000, __phys_to_pfn(S3C2410_CS5), 0x100000, MT_DEVICE}
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
	.name		= "led:red",
	.def_trigger	= "mmc0",
};

static struct s3c24xx_led_platdata lbookv3_pdata_led_green = {
	.gpio		= S3C2410_GPC6,
	.flags		= 0,
	.name		= "led:green",
	.def_trigger	= "nand-disk",
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

#ifdef CONFIG_ARCH_LBOOK_V3_EXT

static struct mtd_partition lbookv3_nand_part[] = {
	[0] = {
		.name	= "KERNEL",
		.offset = SZ_1M * 8,
		.size	= SZ_1M,
	},
	[1] = {
		.name	= "ROOTFS",
		.offset = SZ_1M * 9,
		.size	= SZ_1M * 50,
	},
	[2] = {
		.name	= "LOGO",
		.offset = SZ_1M * 60,
		.size	= SZ_1M,
	},
	[3] = {
		.name	= "USERDATA",
		.offset	= SZ_1M * 0x3D,
		.size	= SZ_2M,
	},
	[4] = {
		.name	= "STORAGE",
		.offset	= SZ_1M * 0x3F,
		.size	= SZ_1M * 9,
	},
	[5] = {
		.name	= "SPARE",
		.size	= SZ_1M * 8,
		.offset	= 0,
	},
};

#else

static struct mtd_partition lbookv3_nand_part[] = {
	[0] = {
		.name	= "KERNEL",
		.offset = SZ_1M,
		.size	= SZ_1M,
	},
	[1] = {
		.name	= "ROOTFS",
		.offset = SZ_2M,
		.size	= SZ_1M*50,
	},
	[2] = {
		.name	= "LOGO",
		.offset = SZ_1M * 52,
		.size	= SZ_1M,
	},
	[3] = {
		.name	= "USERDATA",
		.offset	= SZ_1M * 0x35,
		.size	= SZ_2M,
	},
	[4] = {
		.name	= "STORAGE",
		.offset	= SZ_1M * 0x37,
		.size	= SZ_1M * 9,
	},
	[5] = {
		.name	= "SPARE",
		.size	= SZ_1M,
		.offset	= 0,
	},
};
#endif


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
#ifdef CONFIG_ARCH_LBOOK_V3_EXT
	.tacls		= 12,
	.twrph0		= 12,
	.twrph1		= 10,
#else
	.tacls		= 30,
	.twrph0		= 60,
	.twrph1		= 30,
#endif
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

static void lbookv3_mmc_set_power(unsigned char power_mode, unsigned short vdd)
{
	switch(power_mode) {
		case MMC_POWER_UP:
		case MMC_POWER_ON:
			s3c2410_gpio_cfgpin(S3C2410_GPB6, S3C2410_GPB6_OUTP);
			s3c2410_gpio_setpin(S3C2410_GPB6, 1);
			break;
		case MMC_POWER_OFF:
		default:
			s3c2410_gpio_cfgpin(S3C2410_GPB6, S3C2410_GPB6_OUTP);
			s3c2410_gpio_pullup(S3C2410_GPB6, 1);
			s3c2410_gpio_setpin(S3C2410_GPB6, 0);
	}
}

static struct s3c24xx_mci_pdata lbookv3_mmc_cfg = {
//	.gpio_wprotect	= S3C2410_GPH8,
	.gpio_detect	= S3C2410_GPF5,
	.set_power	= &lbookv3_mmc_set_power,
	.ocr_avail	= MMC_VDD_32_33,
};

static void lbookv3_udc_command(enum s3c2410_udc_cmd_e cmd)
{
	switch(cmd) {
		case S3C2410_UDC_P_DISABLE:
			s3c2410_gpio_setpin(S3C2410_GPC12, 0);
			break;
		case S3C2410_UDC_P_ENABLE:
			s3c2410_gpio_setpin(S3C2410_GPC12, 1);
			break;
		case S3C2410_UDC_P_RESET:
			break;
	}
}

static struct s3c2410_udc_mach_info lbookv3_udc_platform_data = {
	.udc_command	= lbookv3_udc_command,
	.vbus_pin	= S3C2410_GPF4,
};

const unsigned int apollo_pins[] = {S3C2410_GPD10, S3C2410_GPD12,
	S3C2410_GPD13, S3C2410_GPD11, S3C2410_GPD14, S3C2410_GPD15};

static int apollo_get_ctl_pin(unsigned int pin)
{
	return s3c2410_gpio_getpin(apollo_pins[pin]) ? 1 : 0;
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

static void apollo_set_ctl_pin(unsigned int pin, unsigned char val)
{
	s3c2410_gpio_setpin(apollo_pins[pin], val);
}


static void apollo_write_value(unsigned char val)
{
	writeb(val, 0xE8000000);
}

static unsigned char apollo_read_value(void)
{
	unsigned char res;

	apollo_set_gpa_14_15(1);
	res = readb(0xE8000000);
	apollo_set_gpa_14_15(0);

	return res;
}

static int apollo_setuphw(void)
{
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

	apollo_set_ctl_pin(H_CD, 0);
	return 0;
}

static void apollo_initialize(void)
{
	apollo_set_ctl_pin(H_RW, 0);
	s3c2410_gpio_setpin(S3C2410_GPA14, 1);
	s3c2410_gpio_setpin(S3C2410_GPA15, 0);
	s3c2410_gpio_cfgpin(S3C2410_GPE1, S3C2410_GPE1_OUTP);
	s3c2410_gpio_setpin(S3C2410_GPE1, 0);
	s3c2410_gpio_cfgpin(S3C2410_GPC13, S3C2410_GPC13_OUTP);
	s3c2410_gpio_setpin(S3C2410_GPC13, 1);
}

static int apollo_init(void)
{
	apollo_setuphw();
	apollo_initialize();

	return 0;
}

static struct eink_apollofb_platdata lbookv3_apollofb_platdata = {
	.ops = {
		.set_ctl_pin	= apollo_set_ctl_pin,
		.get_ctl_pin	= apollo_get_ctl_pin,
		.read_value	= apollo_read_value,
		.write_value	= apollo_write_value,
		.initialize = apollo_init,
	},
	.defio_delay = HZ / 2,
};

static struct platform_device lbookv3_apollo = {
	.name		= "eink-apollo",
	.id		= -1,
	.dev		= {
		.platform_data = &lbookv3_apollofb_platdata,
	},
};

static struct platform_device lbookv3_keys = {
	.name		= "lbookv3-keys",
	.id		= -1,
};

static struct platform_device *lbookv3_devices[] __initdata = {
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
	&s3c_device_usbgadget,
	&s3c_device_usb,
	&s3c_device_nand,
	&s3c_device_rtc,
	&s3c_device_adc,
	&s3c_device_hsmmc,
	&s3c_device_spi0,
	&s3c_device_sdi,
	&s3c_device_timer[0],
	&lbookv3_led_red,
	&lbookv3_led_green,
	&lbookv3_device_nor,
	&lbookv3_apollo,
	&lbookv3_keys,
};

static void lbookv3_power_off(void)
{
	/* Voodoo from original kernel */
	s3c2410_gpio_setpin(S3C2410_GPB8, 0);
	udelay(1000);
	s3c2410_gpio_setpin(S3C2410_GPB5, 0);
}

static void __init lbookv3_init_gpio(void)
{
	/* LEDs */
	s3c2410_gpio_cfgpin(S3C2410_GPC5, S3C2410_GPC5_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPC6, S3C2410_GPC6_OUTP);
	s3c2410_gpio_setpin(S3C2410_GPC5, 0);
	s3c2410_gpio_setpin(S3C2410_GPC6, 0);

	/* Voodoo from original kernel */
	s3c2410_gpio_cfgpin(S3C2410_GPB0, S3C2410_GPB0_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPB1, S3C2410_GPB1_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPB5, S3C2410_GPB5_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPB6, S3C2410_GPB6_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPB7, S3C2410_GPB7_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPB8, S3C2410_GPB8_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPB9, S3C2410_GPB9_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPC12, S3C2410_GPC12_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPC13, S3C2410_GPC13_OUTP);

	s3c2410_gpio_setpin(S3C2410_GPB0, 0);
	s3c2410_gpio_setpin(S3C2410_GPB1, 0);
	s3c2410_gpio_setpin(S3C2410_GPB5, 1);
	s3c2410_gpio_setpin(S3C2410_GPB6, 1);
	s3c2410_gpio_setpin(S3C2410_GPB7, 0);
	s3c2410_gpio_setpin(S3C2410_GPB8, 1);
	s3c2410_gpio_setpin(S3C2410_GPB9, 0);
	s3c2410_gpio_setpin(S3C2410_GPC13, 0);
	s3c2410_gpio_setpin(S3C2410_GPC12, 0);

	s3c2410_gpio_cfgpin(S3C2410_GPF4, S3C2410_GPF4_EINT4);

	/* SIM card  interface */
/*	s3c2410_gpio_cfgpin(S3C2410_GPB8, S3C2410_GPB8_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPB9, S3C2410_GPB9_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPC1, S3C2410_GPC1_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPC2, S3C2410_GPC2_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPH8, S3C2410_GPH8_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPH9, S3C2410_GPH9_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPH10, S3C2410_GPC10_OUTP);
	s3c2410_gpio_setpin(S3C2410_GPB8, 1);
	s3c2410_gpio_setpin(S3C2410_GPB9, 0);
	s3c2410_gpio_setpin(S3C2410_GPC2, 0);
	s3c2410_gpio_setpin(S3C2410_GPC1, 0);
	s3c2410_gpio_setpin(S3C2410_GPH10, 0);
*/

	/* MP3 decoder interface */
/*	s3c2410_gpio_cfgpin(S3C2410_GPC11, S3C2410_GPC11_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPD9, S3C2410_GPD9_OUTP);
	s3c2410_gpio_cfgpin(S3C2410_GPC10, S3C2410_GPC10_OUTP);
	s3c2410_gpio_setpin(S3C2410_GPC11, 0);
*/

	s3c2410_gpio_cfgpin(S3C2410_GPG12, S3C2410_GPG12_XMON);
	s3c2410_gpio_cfgpin(S3C2410_GPG13, S3C2410_GPG13_nXPON);
	s3c2410_gpio_cfgpin(S3C2410_GPG14, S3C2410_GPG14_YMON);
	s3c2410_gpio_cfgpin(S3C2410_GPG15, S3C2410_GPG15_nYPON);

	s3c2410_gpio_pullup(S3C2410_GPG12, 0);
	s3c2410_gpio_pullup(S3C2410_GPG13, 1);
	s3c2410_gpio_pullup(S3C2410_GPG14, 0);
	s3c2410_gpio_pullup(S3C2410_GPG15, 1);

}

static void __init lbookv3_map_io(void)
{
	s3c24xx_init_io(lbookv3_iodesc, ARRAY_SIZE(lbookv3_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(lbookv3_uartcfgs, ARRAY_SIZE(lbookv3_uartcfgs));
}

static void __init lbookv3_init(void)
{
	unsigned int tmp;

	lbookv3_init_gpio();


	tmp =	(0x78 << S3C2410_PLLCON_MDIVSHIFT)
		| (0x02 << S3C2410_PLLCON_PDIVSHIFT)
		| (0x03 << S3C2410_PLLCON_SDIVSHIFT);
	writel(tmp, S3C2410_UPLLCON);

	s3c_device_nand.dev.platform_data = &lbookv3_nand_info;
	s3c_device_sdi.dev.platform_data = &lbookv3_mmc_cfg;
	s3c_device_usbgadget.dev.platform_data = &lbookv3_udc_platform_data;

	platform_add_devices(lbookv3_devices, ARRAY_SIZE(lbookv3_devices));

	pm_power_off = &lbookv3_power_off;
	s3c2410_pm_init();
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


