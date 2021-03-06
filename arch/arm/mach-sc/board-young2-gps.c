#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include "devices.h"
#include <mach/board.h>
#include <linux/regulator/consumer.h>
#include <mach/regulator.h>
#include <linux/errno.h>
#include <linux/types.h>
#if defined(CONFIG_SEC_GPIO_DVS)
#include <linux/secgpio_dvs.h>
#include <mach/pinmap.h>
#endif

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#ifdef CONFIG_PROC_FS

#if defined(CONFIG_GPS_BCM4752)
static struct device *gps_dev;
extern struct class *sec_class;
#endif

static int gps_enable_control(int flag)
{
        static struct regulator *gps_regulator = NULL;
        static int f_enabled = 0;

        pr_info("%s\n", __func__);
        printk("[GPS] LDO control : %s\n", flag ? "ON" : "OFF");

        if (flag && (!f_enabled)) {
                      gps_regulator = regulator_get(NULL, "vddsim2");
                      if (IS_ERR(gps_regulator)){
                                   gps_regulator = NULL;
                                   return EIO;
                      } else {
                                   regulator_set_voltage(gps_regulator, 1800000, 1800000);
                                   regulator_enable(gps_regulator);
                      }
                      f_enabled = 1;
        }
        if (f_enabled && (!flag))
        {
                      if (gps_regulator) {
                                   regulator_disable(gps_regulator);
                                   regulator_put(gps_regulator);
                                   gps_regulator = NULL;
                      }
                      f_enabled = 0;
        }

        return 0;
}

#if defined(CONFIG_GPS_MRVL88L2000) || defined(CONFIG_GPS_CSR5T)
static void gps_eclk_ctrl(int on)
{
	/* SPRD Platform can not support GPS ECLK */
}

static void gps_power_on(void)
{
	unsigned int gps_rst_n,gps_on;

	gps_rst_n = GPIO_GPS_RESET;

	if (gpio_request(gps_rst_n, "gpio_gps_rst")) {
		pr_err("Request GPIO failed, gpio: %d\n", gps_rst_n);
		return;
	}
	gps_on = GPIO_GPS_ONOFF;

	if (gpio_request(gps_on, "gpio_gps_on")) {
		pr_err("Request GPIO failed,gpio: %d\n", gps_on);
		goto out;
	}


	gpio_direction_output(gps_rst_n, 0);
	gpio_direction_output(gps_on, 0);

	gps_enable_control(1);
	
	mdelay(10);
	mdelay(10);

	pr_info("gps chip powered on\n");

	gpio_free(gps_on);
out:
	gpio_free(gps_rst_n);

#ifdef CONFIG_SEC_GPIO_DVS
	/************************ Caution !!! ****************************/
	/* This function must be located in appropriate INIT position
	 * in accordance with the specification of each BB vendor.
	 */
	/************************ Caution !!! ****************************/
#if 0//defined(CONFIG_MACH_STAR2) || defined(CONFIG_MACH_YOUNG2)
	gpio_request(GPIO_NC_100_WA,   NULL);
	gpio_direction_input(GPIO_NC_100_WA);
	__raw_writel( (BIT_PIN_SLP_AP|BIT_PIN_NULL|BITS_PIN_DS(1)|BITS_PIN_AF(3)|BIT_PIN_NUL|BIT_PIN_SLP_NUL|BIT_PIN_SLP_IE), (CTL_PIN_BASE + REG_PIN_CP_RFCTL14));	
	__raw_writel( (BIT_PIN_SLP_AP|BIT_PIN_NULL|BITS_PIN_DS(1)|BITS_PIN_AF(3)|BIT_PIN_WPD|BIT_PIN_SLP_WPD|BIT_PIN_SLP_IE), (CTL_PIN_BASE + REG_PIN_LCD_D13));	
#endif

	gpio_dvs_check_initgpio();
#endif

	return;
}

static void gps_power_off(void)
{
	unsigned int gps_rst_n, gps_on;

	gps_on = GPIO_GPS_ONOFF;
	if (gpio_request(gps_on, "gpio_gps_on\n")) {
		pr_err("Request GPIO failed,gpio: %d\n", gps_on);
		return;
	}
	gps_rst_n = GPIO_GPS_RESET;
	if (gpio_request(gps_rst_n, "gpio_gps_rst")) {
		pr_debug("Request GPIO failed, gpio: %d\n", gps_rst_n);
		goto out2;
	}
	
	gpio_direction_output(gps_rst_n, 0);
	gpio_direction_output(gps_on, 0);

	gps_enable_control(0);

	pr_info("gps chip powered off\n");

	gpio_free(gps_rst_n);
out2:
	gpio_free(gps_on);
	return;
}

static void gps_reset(int flag)
{
	unsigned int gps_rst_n;
	
	
	gps_rst_n = GPIO_GPS_RESET;

	if (gpio_request(gps_rst_n, "gpio_gps_rst")) {
		pr_err("Request GPIO failed, gpio: %d\n", gps_rst_n);
		return;
	}

	gpio_direction_output(gps_rst_n, flag);
	gpio_free(gps_rst_n);
	printk(KERN_INFO "gps chip reset with %s\n", flag ? "ON" : "OFF");
}

static void gps_on_off(int flag)
{
	unsigned int gps_on;

	gps_on = GPIO_GPS_ONOFF;

	if (gpio_request(gps_on, "gpio_gps_on")) {
		pr_err("Request GPIO failed, gpio: %d\n", gps_on);
		return;
	}

	gpio_direction_output(gps_on, flag);
	gpio_free(gps_on);
	printk(KERN_INFO "gps chip onoff with %s\n", flag ? "ON" : "OFF");
}

#define SIRF_STATUS_LEN	16
static char sirf_status[SIRF_STATUS_LEN] = "off";

static ssize_t sirf_read_proc(struct file *filp,
		 char *buff, size_t len1, loff_t *off)
{
	char page[SIRF_STATUS_LEN]= {0};
	int len = strlen(sirf_status);
	sprintf(page, "%s\n", sirf_status);
	copy_to_user(buff,page,len + 1);
	return len + 1;
}

static ssize_t sirf_write_proc(struct file *filp,
		const char *buff, size_t len, loff_t *off)
{

	char messages[256];
	int flag, ret;
	char buffer[7];

	
	if (len > 255)
		len = 255;

	memset(messages, 0, sizeof(messages));
	
	if (!buff || copy_from_user(messages, buff, len))
		return -EFAULT;

	if (strlen(messages) > (SIRF_STATUS_LEN - 1)) {
		pr_warning("[ERROR] messages too long! (%d) %s\n",
			strlen(messages), messages);
		return -EFAULT;
	}

	if (strncmp(messages, "off", 3) == 0) {
		strcpy(sirf_status, "off");

		gps_power_off();
	} else if (strncmp(messages, "on", 2) == 0) {
		strcpy(sirf_status, "on");

		gps_power_on();
	} else if (strncmp(messages, "reset", 5) == 0) {
		strcpy(sirf_status, messages);

		ret = sscanf(messages, "%s %d", buffer, &flag);
		if (ret == 2){
			gps_reset(flag);
		}
	} else if (strncmp(messages, "sirfon", 6) == 0) {
		strcpy(sirf_status, messages);

		ret = sscanf(messages, "%s %d", buffer, &flag);
		if (ret == 2){
			gps_on_off(flag);
		}
	} else
		pr_info("usage: echo {on/off} > /proc/driver/sirf\n");

	return len;
}

static const struct file_operations gps_proc_fops = {
	.owner = THIS_MODULE,
	.read = sirf_read_proc,
	.write = sirf_write_proc,
};
	
static int __init create_sirf_proc_file(void)
{

	struct proc_dir_entry *sirf_proc_file = NULL;

	sirf_proc_file = proc_create("driver/sirf", 0644, NULL, &gps_proc_fops);
	if (!sirf_proc_file) {
		pr_err("sirf proc file create failed!\n");
		return -ENOMEM;
	}

	return 0;
}


device_initcall(create_sirf_proc_file);
#endif

#if defined(CONFIG_GPS_BCM4752)
static int __init gps_bcm4752_init(void)
{
	BUG_ON(!sec_class);
	gps_dev = device_create(sec_class, NULL, 0, NULL, "gps");
	BUG_ON(!gps_dev);

	mdelay(500);
	gps_enable_control(1);

	if (gpio_request(GPIO_GPS_PWR_EN, "GPS_PWR_EN")) {
		WARN(1, "fail to request gpio (GPS_PWR_EN)\n");
		return 1;
	}
	gpio_direction_output(GPIO_GPS_PWR_EN, 0);

	gpio_export(GPIO_GPS_PWR_EN, 1);

	gpio_export_link(gps_dev, "GPS_PWR_EN", GPIO_GPS_PWR_EN);

	return 0;
}

device_initcall(gps_bcm4752_init);
#endif
#endif
