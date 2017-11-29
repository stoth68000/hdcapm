/*
 *  Driver for the Startech USB2HDCAPM USB capture device
 *
 *  Copyright (c) 2017 Steven Toth <stoth@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

/* The USB encoder has one internal I2C bus master, and a secondary
 * bitbanged I2C bus. Implement functions to control both.
 */

#include "hdcapm.h"

#define GPIO_SCL (1 << 14)
#define GPIO_SDA (1 << 15)

static unsigned int i2c_udelay = 5;
module_param(i2c_udelay, int, 0644);
MODULE_PARM_DESC(i2c_udelay, "i2c delay at insmod time, in usecs "
		"(should be 5 or higher). Lower value means higher bus speed.");

/* GPIO bit-banged bus */
static void hdcapm_bit_setscl(void *data, int state)
{
	struct hdcapm_i2c_bus *bus = data;
	struct hdcapm_dev *dev = bus->dev;

	if (state)
		hdcapm_clr32(dev, REG_GPIO_OE, GPIO_SCL);
	else
		hdcapm_set32(dev, REG_GPIO_OE, GPIO_SCL);
}

static void hdcapm_bit_setsda(void *data, int state)
{
	struct hdcapm_i2c_bus *bus = data;
	struct hdcapm_dev *dev = bus->dev;

	if (state)
		hdcapm_clr32(dev, REG_GPIO_OE, GPIO_SDA);
	else
		hdcapm_set32(dev, REG_GPIO_OE, GPIO_SDA);
}

static int hdcapm_bit_getscl(void *data)
{
	struct hdcapm_i2c_bus *bus = data;
	struct hdcapm_dev *dev = bus->dev;
	u32 val;

	hdcapm_read32(dev, REG_GPIO_DATA_RD, &val);

	return val & GPIO_SCL ? 1 : 0;
}

static int hdcapm_bit_getsda(void *data)
{
	struct hdcapm_i2c_bus *bus = data;
	struct hdcapm_dev *dev = bus->dev;
	u32 val;

	hdcapm_read32(dev, REG_GPIO_DATA_RD, &val);

	return val & GPIO_SDA ? 1 : 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_algo_bit_data hdcapm_i2c1_algo_template = {
	.setsda  = hdcapm_bit_setsda,
	.setscl  = hdcapm_bit_setscl,
	.getsda  = hdcapm_bit_getsda,
	.getscl  = hdcapm_bit_getscl,
	.udelay  = 16,
	.timeout = 200,
};

/* Internal I2C Bus */
static int i2c_writeread(struct i2c_adapter *i2c_adap, const struct i2c_msg *msg, int joined_rlen)
{
/*
 EP4 Host -> 01 01 01 00 04 05 00 00 55 00 00 00    usbwrite(REG_504, 0x55)
 EP4 Host -> 01 01 01 00 00 05 00 00 09 27 00 80    usbwrite(REG_500, 0x80002709);
 EP4 Host -> 01 00 01 00 00 05 00 00                2709 = usbread(REG_0500);
 EP3 Host <- 09 27 00 00
 EP4 Host -> 01 00 01 00 0c 05 00 00                03 = usbread(REG_050c);
 EP3 Host <- 03 00 00 00
 */
	struct hdcapm_i2c_bus *bus = i2c_adap->algo_data;
	struct hdcapm_dev *dev = bus->dev;
	struct i2c_msg *nextmsg = (struct i2c_msg *)(msg + 1);
	u32 val;
	int ret;
	int safety = 32;

	dprintk(2, "%s(addr=0x%x, reg=0x%x, len=%d)\n", __func__, msg->addr, msg->buf[0], msg->len);

	ret = hdcapm_write32(dev, REG_I2C_W_BUF, msg->buf[0]); /* Register */

	val  = (1 << 31);
	val |= 9; /* Write one and read one byte? */
	val |= (msg->addr << 7);
	ret = hdcapm_write32(dev, REG_I2C_XACT, val);   /* Write one byte read one byte? */

	/* I2C engine busy? */
	val = (1 << 31);
	while (val & 0x80000000) {
		ret = hdcapm_read32(dev, REG_I2C_XACT, &val); /* Check bit31 has cleared? */
		if (safety-- == 0)
			break;
	}
	if (safety == 0) {
		pr_err(KBUILD_MODNAME ": stuck i2c bit, aborting.\n");
		return 0;
	}

	ret = hdcapm_read32(dev, REG_I2C_R_BUF, &val); /* Read i2c result */
	nextmsg->buf[0] = val & 0x000000ff;

	dprintk(2, "%s(addr=0x%x, reg = 0x%x) = 0x%02x\n", __func__, msg->addr, msg->buf[0], nextmsg->buf[0]);
	return 1;
}

static int i2c_write(struct i2c_adapter *i2c_adap, const struct i2c_msg *msg, int joined)
{
	struct hdcapm_i2c_bus *bus = i2c_adap->algo_data;
	struct hdcapm_dev *dev = bus->dev;
	u32 val;
	int ret, i;

	/* Position each data byte into the u32, for a single strobe into the write buffer. */
	val = 0;
	for (i = msg->len; i > 0; i--) {
		val <<= 8;
		val |= msg->buf[i - 1];
	}
	
	ret = hdcapm_write32(dev, REG_I2C_W_BUF, val);

	val  = (1 << 31);
	val |= msg->len; /* Write N bytes, no read */
	val |= (msg->addr << 7);
	ret = hdcapm_write32(dev, REG_I2C_XACT, val);

	return 1;
}

static int i2c_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg *msgs, int num)
{
	//struct hdcapm_i2c_bus *bus = i2c_adap->algo_data;
	//struct hdcapm_dev *dev = bus->dev;
	int ret = 0;
	int i;

	dprintk(2, "%s(num = %d)\n", __func__, num);

	for (i = 0; i < num; i++) {
		dprintk(4, "%s(num = %d) addr = 0x%02x  len = 0x%x\n",
			__func__, num, msgs[i].addr, msgs[i].len);
		if (msgs[i].flags & I2C_M_RD) {

		} else if (i + 1 < num && (msgs[i + 1].flags & I2C_M_RD) && msgs[i].addr == msgs[i + 1].addr) {

			/* write then read from same address */
			ret = i2c_writeread(i2c_adap, &msgs[i], msgs[i + 1].len);
			if (ret < 0)
				goto error;
			i++;

		} else {
			/* Write */
			ret = i2c_write(i2c_adap, &msgs[i], 0);
		}
		if (ret < 0)
			goto error;
	}
	return num;

error:
	return ret;
}

/* Internal I2C Master controller */
static u32 hdcapm0_functionality(struct i2c_adapter *adap)
{
	//return I2C_FUNC_I2C;
	return I2C_FUNC_SMBUS_BYTE_DATA;
}

static const struct i2c_algorithm hdcapm_i2c0_algo_template = {
	.master_xfer	= i2c_xfer,
	.functionality	= hdcapm0_functionality,
};

static const struct i2c_adapter hdcapm_i2c0_adap_template = {
	.name              = "hdcapm internal",
	.owner             = THIS_MODULE,
	.algo              = &hdcapm_i2c0_algo_template,
};

static struct i2c_client hdcapm_i2c0_client_template = {
	.name	= "hdcapm internal",
};

static int i2c_readreg8(struct hdcapm_i2c_bus *bus, u8 addr, u8 reg, u8 *val)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };

	struct i2c_msg msg[] = {
		{ .addr = addr, .flags = 0, .buf = b0, .len = 1 },
		{ .addr = addr, .flags = I2C_M_RD, .buf = b1, .len = 1 } };

	ret = i2c_transfer(&bus->i2c_adap, msg, 2);
	if (ret != 2) {
		return 0;
	}

	*val = b1[0];

	return 2;
}

#if 0
static int i2c_readreg16(struct hdcapm_i2c_bus *bus, u8 addr, u16 reg, u8 *val)
{
	int ret;
	u8 b0[] = { reg >> 8, reg };
	u8 b1[] = { 0 };

	struct i2c_msg msg[] = {
		{ .addr = addr, .flags = 0, .buf = b0, .len = sizeof(b0) },
		{ .addr = addr, .flags = I2C_M_RD, .buf = b1, .len = sizeof(b1) } };

	ret = i2c_transfer(&bus->i2c_adap, msg, 2);
	if (ret != 2) {
		return 0;
	}

	*val = b1[0];

	return 2;
}
#endif

static char *i2c_devs[128] = {
	[0x66 >> 1] = "MST3367?",
	[0x88 >> 1] = "MST3367?",
	[0x94 >> 1] = "MST3367?",
	[0x9c >> 1] = "MST3367?",
	[0xa2 >> 1] = "EEPROM",
};

#if 0
static void i2c_eeprom_dump(struct hdcapm_i2c_bus *bus, u8 addr)
{
	int i;
	u8 dat[32];
	memset(dat, 0xdd, sizeof(dat));

	for (i = 0; i < sizeof(dat); i++) {
		i2c_readreg16(bus, addr, i, &dat[i]);
	}
	for (i = 0; i < sizeof(dat); i += 16) {
		dprintk(1, "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
			dat[i + 0], dat[i + 1], dat[i + 2], dat[i + 3],
			dat[i + 4], dat[i + 5], dat[i + 6], dat[i + 7],
			dat[i + 8], dat[i + 9], dat[i + 10], dat[i + 11],
			dat[i + 12], dat[i + 13], dat[i + 14], dat[i + 15]);
	}
}
#endif

static void do_i2c_scan(struct hdcapm_i2c_bus *bus)
{
	int a, ret;
	u8 val;

	for (a = 0; a < 128; a++) {
		ret = i2c_readreg8(bus, a, 0x00, &val);
		if (ret == 2) {
			pr_info("%s: i2c scan: found device @ 0x%x  [%s]\n",
				__func__, a << 1, i2c_devs[a] ? i2c_devs[a] : "???");
		}

	}
}

int hdcapm_i2c_register(struct hdcapm_dev *dev, struct hdcapm_i2c_bus *bus, int nr)
{
	bus->nr = nr;
	bus->dev = dev;

	dprintk(1, "%s() registering I2C Bus#%d\n", __func__, bus->nr);

	if (bus->nr == 0) {
		bus->i2c_adap = hdcapm_i2c0_adap_template;
		bus->i2c_client = hdcapm_i2c0_client_template;

		bus->i2c_adap.dev.parent = &dev->udev->dev;
		strlcpy(bus->i2c_adap.name, KBUILD_MODNAME, sizeof(bus->i2c_adap.name));

		bus->i2c_adap.algo_data = bus;
		i2c_set_adapdata(&bus->i2c_adap, bus);
		i2c_add_adapter(&bus->i2c_adap);

		bus->i2c_client.adapter = &bus->i2c_adap;

	} else
	if (bus->nr == 1) {

		bus->i2c_algo = hdcapm_i2c1_algo_template;

		bus->i2c_adap.dev.parent = &dev->udev->dev;
		strlcpy(bus->i2c_adap.name, KBUILD_MODNAME, sizeof(bus->i2c_adap.name));
		bus->i2c_adap.owner = THIS_MODULE;
		bus->i2c_algo.udelay = i2c_udelay;
		bus->i2c_algo.data = bus;
		i2c_set_adapdata(&bus->i2c_adap, bus);
		bus->i2c_adap.algo_data = &bus->i2c_algo;
		bus->i2c_client.adapter = &bus->i2c_adap;
		strlcpy(bus->i2c_client.name, "hdcapm gpio", I2C_NAME_SIZE);

		hdcapm_bit_setscl(bus, 1);
		hdcapm_bit_setsda(bus, 1);

		i2c_bit_add_bus(&bus->i2c_adap);

	} else
		BUG();


	if (hdcapm_i2c_scan && bus->nr == 1)
		do_i2c_scan(bus);

#if 0
	if (bus->nr == 1)
		i2c_eeprom_dump(bus, 0xa2 >> 1);
#endif

	return 0;
}

void hdcapm_i2c_unregister(struct hdcapm_dev *dev, struct hdcapm_i2c_bus *bus)
{
	dprintk(1, "%s() unregistering I2C Bus#%d\n", __func__, bus->nr);

	i2c_del_adapter(&bus->i2c_adap);
}
