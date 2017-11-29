/*
 *  Driver for the MSTAR 3367 HDMI Receiver
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

#ifndef MST3367_H
#define MST3367_H

/* Platform dependent definitions */
struct mst3367_platform_data {
	/* TODO: DO we even need this platform data for such a limited device? */
	u8 some_value;
};

/* Routing */
#define MST3367_ROUTING_HDMI_PORT_A 1

/* notify events */
#define MST3367_SOURCE_DETECT 0

struct mst3367_source_detect {
	int present;
};

#endif /* MST3367_H */
