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

/*
 idle-no-hdmi-connected.tdc -- Nothing else of consequence in the file.
 It's worth noting that when you run the graphedit tool under windows,
 open up the Analog Capture property page, switch to the Driver Properties view,
 and enable "PRINT DEBUG" option, debug view shows the following driver activity:
 "MST3367_HDMI_MODE_DETECT(0x55 = 0x03)".

 Record 4:

 EP4 Host -> 01 01 01 00 04 05 00 00 55 00 00 00    usbwrite(REG_504, 0x55)
 EP4 Host -> 01 01 01 00 00 05 00 00 09 27 00 80    usbwrite(REG_500, 0x80002709);
 EP4 Host -> 01 00 01 00 00 05 00 00                2709 = usbread(REG_0500);
 EP3 Host <- 09 27 00 00
 EP4 Host -> 01 00 01 00 0c 05 00 00                03 = usbread(REG_050c);
 EP3 Host <- 03 00 00 00

 In light of the debug view findings, I conclude:
 Writes to 504 establist a I2C write to device 0x55.
 Writes to 500 are...... what?
 reads for 50c are reads from the i2c bus answer.

 80 00 27 09 =
 1000 0000 | 0000 0000 | 0010 0111 | 0000 1001

 001 = 1     rx/tx length?
 001 = 1
 01001110 = 0x4e  device address or register of MST3367?
 10011100 = 0x9c  device address or register of MST3367? .... tv  schematic suggests this is likely correct.

 */

#define REG_0000 0x000
#define REG_0038 0x038
#define REG_0050 0x050

#define REG_I2C_XACT  0x500
#define REG_I2C_W_BUF 0x504
#define REG_I2C_R_BUF 0x50c

/* driver-install.csv shows toggling of register between values:
        Bits 15..           .. 0
   19 0E  -- 0001 1001 0000 1110
   59 0E  -- 0101 1001 0000 1110
   99 0E  -- 1001 1001 0000 1110
   D9 0E  -- 1101 1001 0000 1110
   Suggesting bits 15/14 are a bitbanged I2C bus.
   We'll assume 15: SDA, 14: SCL
 */
#define REG_GPIO_OE      0x610
#define REG_GPIO_DATA_WR 0x614
#define REG_GPIO_DATA_RD 0x618

#define REG_06B0  0x6b0

#define REG_FW_CMD_BUSY  0x6cc

/* Valid args are 0 - 10 */
#define REG_FW_CMD_ARG(n) (0x6f8 - ((n) * 4))

/* A command 'type' or identifier is written to this register,
 * after the type specifics args have already been written.
 */
#define REG_FW_CMD_EXECUTE 0x6fc

#define REG_081C 0x81c
#define REG_0820 0x820
#define REG_0824 0x824
#define REG_0828 0x828
#define REG_082C 0x82c
#define REG_0830 0x830
#define REG_0834 0x834
#define REG_0838 0x838
#define REG_083C 0x83c
#define REG_0840 0x840

#define REG_0B78 0xb78
