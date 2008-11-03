/* linux/arch/arm/mach-s3c2410/mach-eb600.c
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
 * derived from linux/arch/arm/mach-s3c2410/mach-smdk2410.c, written by
 * Jonas Dietsche
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
#include <linux/reboot.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/eink_apollofb.h>
#include <linux/delay.h>
#include <linux/s3c_battery.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/plat-s3c/regs-serial.h>
#include <asm/plat-s3c/nand.h>
#include <asm/plat-s3c24xx/devs.h>
#include <asm/plat-s3c24xx/cpu.h>
#include <asm/plat-s3c24xx/pm.h>

#include <asm/arch/mci.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/usb-control.h>
#include <asm/arch/regs-clock.h>
#include <asm/arch/leds-gpio.h>

#include <asm/delay.h>


static struct map_desc eb600_iodesc[] __initdata = {
	{0xe8000000, __phys_to_pfn(S3C2410_CS5), 0x100000, MT_DEVICE}
/* nothing here yet */
};

#define UCON S3C2410_UCON_DEFAULT
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg eb600_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
	},
};

#define _CD S3C2410_GPD0
#define _RW S3C2410_GPD3
#define _DS S3C2410_GPD4
#define _ACK S3C2410_GPD5
#define _WUP S3C2410_GPD1
#define _NRST S3C2410_GPD7

const unsigned int apollo_pins[] = {_CD, _RW, _DS, _ACK, _WUP, _NRST};

static int apollo_get_ctl_pin(unsigned int pin)
{
        return s3c2410_gpio_getpin(apollo_pins[pin]) ? 1 : 0;
}

static void apollo_set_ctl_pin(unsigned int pin, unsigned char val)
{
        s3c2410_gpio_setpin(apollo_pins[pin], val);
}

static void apollo_write_value(unsigned char val)
{
	writeb (val, S3C2410_GPCDAT+1);
}

static unsigned char apollo_read_value(void)
{
	unsigned char ret;

	writeb (0, S3C2410_GPCCON+2);
	writeb (0, S3C2410_GPCCON+3);
	ret = readb(S3C2410_GPCDAT+1);
	writeb (0x55, S3C2410_GPCCON+2);
	writeb (0x55, S3C2410_GPCCON+3);

	return ret;
}


static int apollo_setuphw(void)
{
	
	s3c2410_gpio_cfgpin(S3C2410_GPD2, S3C2410_GPIO_OUTPUT);
	s3c2410_gpio_setpin(S3C2410_GPD2, 1);
	
	s3c2410_gpio_cfgpin(S3C2410_GPD6, S3C2410_GPIO_OUTPUT);
	s3c2410_gpio_setpin(S3C2410_GPD6, 1);

	writeb (0x55, S3C2410_GPCCON+2);
	writeb (0x55, S3C2410_GPCCON+3);

	s3c2410_gpio_cfgpin(_CD, S3C2410_GPIO_OUTPUT);
        s3c2410_gpio_cfgpin(_DS, S3C2410_GPIO_OUTPUT);
        s3c2410_gpio_cfgpin(_RW, S3C2410_GPIO_OUTPUT);
        s3c2410_gpio_cfgpin(_ACK, S3C2410_GPIO_INPUT);
        s3c2410_gpio_cfgpin(_WUP, S3C2410_GPIO_OUTPUT);
        s3c2410_gpio_cfgpin(_NRST, S3C2410_GPIO_OUTPUT);
        s3c2410_gpio_setpin(_CD, 0);
        s3c2410_gpio_setpin(_DS, 1); 
        s3c2410_gpio_setpin(_RW, 0);
        s3c2410_gpio_setpin(_WUP, 0);
        s3c2410_gpio_setpin(_NRST, 1); 
        apollo_set_ctl_pin(H_CD, 0);
        return 0;
}

static void apollo_initialize(void)
{
        apollo_set_ctl_pin(H_RW, 0);
}

static int apollo_init(void)
{
        apollo_setuphw();
        apollo_initialize();

        return 0;
}


static struct eink_apollofb_platdata eb600_apollofb_platdata = {
        .ops = {
                .set_ctl_pin    = apollo_set_ctl_pin,
                .get_ctl_pin    = apollo_get_ctl_pin,
                .read_value     = apollo_read_value,
                .write_value    = apollo_write_value,
                .initialize = apollo_init,
        },
        .defio_delay = HZ / 2,
};

static struct platform_device eb600_apollo = {
        .name           = "eink-apollo",
        .id             = -1,
        .dev            = {
	        .platform_data = &eb600_apollofb_platdata,
	},
};


static struct s3c2410_hcd_info eb600_hcd_info = {
	.port[0]	= {
		.flags	= S3C_HCDFLG_USED,
	},
	.port[1]	= {
		.flags	= 0,
		.power	= 0,
	},
};

static struct mtd_partition eb600_flash_partitions[] = {
	[0] = {
		.name	= "Kernel (896k)",
		.offset	= SZ_128K + SZ_64K,
		.size	= SZ_1M,
	},
	[1] = {
		.name	= "root",
		.offset	= SZ_1M + SZ_128K + SZ_64K,
		.size	= SZ_1M + SZ_512K,
	},
	[2] = {
		.name	= "BMP",
		.offset	= 0x007e0000,
		.size	= SZ_128K,
	},
	[3] = {
		.name	= "vivi",
		.offset	= 0,
		.size	= SZ_128K,
	},
	[4] = {
		.name   = "ebr",
		.offset	= SZ_2M + 10 * SZ_64K,
		.size	= 0x00540000,
	},
};


static struct physmap_flash_data eb600_flash_data = {
	.width		= 2,
	.parts		= eb600_flash_partitions,
	.nr_parts	= ARRAY_SIZE(eb600_flash_partitions),
};

static struct resource eb600_flash_resources[] = {
	[0] = {
		.start	= 0,
		.end	= 0x00800000,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device eb600_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
			.platform_data = &eb600_flash_data,
		},
	.resource	= eb600_flash_resources,
	.num_resources	= ARRAY_SIZE(eb600_flash_resources),
};


#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button eb600_buttons[] = {
	{
		.code		= KEY_UP,
		.gpio		= S3C2410_GPF0,
		.active_low	= 0,
		.desc		= "GPF0",
		.wakeup		= 1,
	},
	{
		.code		= KEY_DOWN,
		.gpio		= S3C2410_GPF2,
		.active_low	= 0,
		.desc		= "GPF2",
		.wakeup		= 1,
	},
	{
		.code		= KEY_F1,
		.gpio		= S3C2410_GPF3,
		.active_low	= 0,
		.desc		= "GPF3",
		.wakeup		= 1,
	},
	{
		.code		= KEY_F2,
		.gpio		= S3C2410_GPF4,
		.active_low	= 0,
		.desc		= "GPF4",
		.wakeup		= 1,
	},
	{
		.code		= KEY_ESC,
		.gpio		= S3C2410_GPF5,
		.active_low	= 0,
		.desc		= "GPF5",
		.wakeup		= 1,
	},
	{
		.code		= KEY_ENTER,
		.gpio		= S3C2410_GPF6,
		.active_low	= 0,
		.desc		= "GPF6",
		.wakeup		= 1,
	},
	{
		.code		= KEY_POWER,
		.gpio		= S3C2410_GPF7,
		.active_low	= 0,
		.desc		= "GPF7",
		.wakeup		= 1,
	},
	{
		.code		= KEY_LEFT,
		.gpio		= S3C2410_GPG3,
		.active_low	= 0,
		.desc		= "GPG3",
		.wakeup		= 1,
	},
	{
		.code		= KEY_DELETE,
		.gpio		= S3C2410_GPG6,
		.active_low	= 0,
		.desc		= "GPG6",
		.wakeup		= 1,
	},
	{
		.code		= KEY_RIGHT,
		.gpio		= S3C2410_GPG7,
		.active_low	= 0,
		.desc		= "GPG7",
		.wakeup		= 1,
	},
	{
		.code		= KEY_KPPLUS,
		.gpio		= S3C2410_GPG8,
		.active_low	= 0,
		.desc		= "GPG8",
		.wakeup		= 1,
	},
	{
		.code		= KEY_KPMINUS,
		.gpio		= S3C2410_GPG9,
		.active_low	= 0,
		.desc		= "GPG9",
		.wakeup		= 1,
	},
};

static struct gpio_keys_platform_data eb600_button_data = {
	.buttons	= eb600_buttons,
	.nbuttons	= ARRAY_SIZE(eb600_buttons),
};

static struct platform_device eb600_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data = &eb600_button_data,
	},
};

#ifdef CONFIG_EB600_EXTRAS
static struct platform_device eb600_control_device = {
	.name		= "eb600pf",
	.id		= -1,
};
#endif

static void __init eb600_init_buttons()
{
	__raw_writel(0xff, S3C2410_GPFUP);
	__raw_writel(0xffff, S3C2410_GPGUP);

	platform_device_register(&eb600_button_device);
};
#else
static void __init eb600_init_buttons() {};
#endif

static struct s3c_battery_platform_data eb600_battery_platdata = {
	.min_voltage		= 3020, // 3022 - dies
	.max_voltage		= 3615, // 3727 - original firmware max on cable
	.adc_channel		= 7,
	.powered_gpio		= S3C2410_GPF1,
	.gpio_active_low	= true,
};

static struct platform_device eb600_battery = {
	.name	= "s3c_battery",
	.id	= -1,
	.num_resources = 0,
	.dev = {
		.platform_data = &eb600_battery_platdata,
	}
};

static struct s3c24xx_led_platdata eb600_led_green_platdata = {
	.gpio	= S3C2410_GPB8,
	.flags	= S3C24XX_LEDF_ACTLOW,
	.name	= "led:green",
};

static struct platform_device eb600_led_green = {
	.name	= "s3c24xx_led",
	.id	= -1,
	.dev	= {
		.platform_data = &eb600_led_green_platdata,
	},
};


static struct platform_device *eb600_devices[] __initdata = {
	&eb600_battery,
	&eb600_led_green,
	&eb600_apollo,
	&eb600_flash,

	&s3c_device_timer0,
	&s3c_device_wdt,
	&s3c_device_usb,
#ifdef CONFIG_EB600_EXTRAS
	&eb600_control_device,
#endif
};

static void eb600_power_off(void)
{
	kernel_restart(0);	
}

static void __init eb600_map_io(void)
{
	s3c24xx_init_io (eb600_iodesc, ARRAY_SIZE(eb600_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(eb600_uartcfgs, ARRAY_SIZE(eb600_uartcfgs));
}

static void __init eb600_init(void)
{
	int tmp;

	s3c_device_usb.dev.platform_data = &eb600_hcd_info;

	platform_add_devices(eb600_devices, ARRAY_SIZE(eb600_devices));

	s3c2410_modify_misccr(S3C2410_MISCCR_USBHOST, 0); // USB device mode
	s3c2410_modify_misccr(S3C2410_MISCCR_USBSUSPND1, S3C2410_MISCCR_USBSUSPND1);

        tmp =   (0x78 << S3C2410_PLLCON_MDIVSHIFT)
		| (0x02 << S3C2410_PLLCON_PDIVSHIFT)
		| (0x03 << S3C2410_PLLCON_SDIVSHIFT);
	writel(tmp, S3C2410_UPLLCON);

	s3c2410_modify_misccr(S3C2410_MISCCR_CLK0_MASK, S3C2410_MISCCR_CLK0_UPLL);

	eb600_init_buttons();

	pm_power_off = &eb600_power_off;
	s3c2410_pm_init();
}



MACHINE_START(EB600, "EB600")
	/* Maintainer: Ondrej Herman */
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= eb600_map_io,
	.init_irq	= s3c24xx_init_irq,
	.init_machine	= eb600_init,
	.timer		= &s3c24xx_timer,
MACHINE_END


