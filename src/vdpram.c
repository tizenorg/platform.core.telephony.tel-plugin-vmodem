/*
 * tel-plugin-vmodem
 *
 * Copyright (c) 2013 Samsung Electronics Co. Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <termios.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <glib.h>

#include <log.h>

#include "vdpram.h"

#ifndef TIOCMODG
#  ifdef TIOCMGET
#    define TIOCMODG TIOCMGET
#  else
#    ifdef MCGETA
#      define TIOCMODG MCGETA
#    endif
#  endif
#endif

#ifndef TIOCMODS
#  ifdef TIOCMSET
#    define TIOCMODS TIOCMSET
#  else
#    ifdef MCSETA
#      define TIOCMODS MCSETA
#    endif
#  endif
#endif

#define VDPRAM_OPEN_PATH		"/dev/vdpram0"

/* DPRAM ioctls for DPRAM tty devices */
#define IOC_MZ_MAGIC			('h')
#define HN_DPRAM_PHONE_ON		_IO (IOC_MZ_MAGIC, 0xd0)
#define HN_DPRAM_PHONE_OFF		_IO (IOC_MZ_MAGIC, 0xd1)
#define HN_DPRAM_PHONE_GETSTATUS	_IOR(IOC_MZ_MAGIC, 0xd2, guint)

/* Retry parameters */
#define SLEEP_TIME_IN_SEC		0
#define SLEEP_TIME_IN_MSEC		50

#define MAX_RETRY_COUNT		10

typedef struct tty_old_setting TtyOldSetting;

struct tty_old_setting {
	gint fd;
	struct termios termiosVal;
	TtyOldSetting *next;
	TtyOldSetting *prev;
};

static TtyOldSetting *ttyold_head = NULL;

/*
 * Insert TTY old settings
 */
static void __insert_tty_oldsetting(TtyOldSetting *me)
{
	dbg("Enter");

	if (me == NULL)
		return;

	if (ttyold_head)
		ttyold_head->prev = me;

	me->next = ttyold_head;
	me->prev = 0;
	ttyold_head = me;
}

/*
 * Search TTY old settings
 */
static TtyOldSetting *__search_tty_oldsetting(gint fd)
{
	TtyOldSetting *tty = ttyold_head;

	dbg("Enter");

	while (tty) {
		if (tty->fd == fd) {
			dbg("tty for fd [%d] found!!!", fd);
			break;
		}
		tty = tty->next;
	};

	return tty;
}

/*
 * Remove TTY old settings
 */
static void __remove_tty_oldsetting(TtyOldSetting *me)
{
	dbg("Enter");

	if (me == NULL)
		return;

	if (me->prev)
		me->prev->next = me->next;
	else
		ttyold_head = me->next;

	if (me->next)
		me->next->prev = me->prev;
}

/*
 * Set hardware flow control
 */
static void __tty_sethwf(gint fd, gint on)
{
	struct termios tty;

	dbg("Enter");

	if (tcgetattr(fd, &tty))
		err("__tty_sethwf: tcgetattr:");

	if (on == 1)
		tty.c_cflag |= CRTSCTS;
	else
		tty.c_cflag &= ~CRTSCTS;

	if (tcsetattr(fd, TCSANOW, &tty))
		err("__tty_sethwf: tcsetattr:");
}

/*
 * Set RTS line. Sometimes dropped. Linux specific?
 */
static gint __tty_setrts(gint fd)
{
	gint mcs;

	dbg("Enter");

	if (-1 ==  ioctl(fd, TIOCMODG, &mcs))
		err("icotl: TIOCMODG");

	mcs |= TIOCM_RTS;

	if (-1 == ioctl(fd, TIOCMODS, &mcs))
		err("icotl: TIOCMODS");

	return 0;
}

/*
 * Set baudrate, parity and number of bits
 */
static gboolean __tty_setparms(gint fd,
	gchar *baudr, gchar *par, gchar *bits, gchar *stop, gint hwf, gint swf)
{
	gint spd = -1;
	gint newbaud;
	gint bit = bits[0];
	gint stop_bit = stop[0];

	struct termios tty;
	TtyOldSetting *old_setting = NULL;

	dbg("Enter");

	old_setting = g_try_new0(TtyOldSetting, 1);

	if (old_setting == NULL)
		return FALSE;

	old_setting->fd = fd;

	if (tcgetattr(fd, &tty) < 0) {
		g_free(old_setting);
		return FALSE;
	}

	if (tcgetattr(fd, &old_setting->termiosVal) < 0) {
		g_free(old_setting);
		return FALSE;
	}

	__insert_tty_oldsetting(old_setting);

	fflush(stdout);

	/* We generate mark and space parity ourself. */
	if (bit == '7' && (par[0] == 'M' || par[0] == 'S'))
		bit = '8';

	/* Check if 'baudr' is really a number */
	if ((newbaud = (atol(baudr) / 100)) == 0 && baudr[0] != '0')
		newbaud = -1;

	switch (newbaud) {
	case 0:
		spd = 0;
	break;

	case 3:
		spd = B300;
	break;

	case 6:
		spd = B600;
	break;

	case 12:
		spd = B1200;
	break;

	case 24:
		spd = B2400;
	break;

	case 48:
		spd = B4800;
	break;

	case 96:
		spd = B9600;
	break;

	case 192:
		spd = B19200;
	break;

	case 384:
		spd = B38400;
	break;

	case 576:
		spd = B57600;
	break;

	case 1152:
		spd = B115200;
	break;

	default:
		err("invaid baud rate");
	break;
	}

	if (spd != -1) {
		cfsetospeed(&tty, (speed_t) spd);
		cfsetispeed(&tty, (speed_t) spd);
	}

	switch (bit) {
	case '5':
		tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS5;
	break;

	case '6':
		tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS6;
	break;

	case '7':
		tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS7;
	break;

	case '8':
	default:
		tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
	break;
	}

	switch (stop_bit) {
	case '1':
		tty.c_cflag &= ~CSTOPB;
	break;

	case '2':
	default:
		tty.c_cflag |= CSTOPB;
	break;
	}

	/* Set into raw, no echo mode */
	tty.c_iflag = IGNBRK;
	tty.c_lflag = 0;
	tty.c_oflag = 0;
	tty.c_cflag |= CLOCAL | CREAD;
	tty.c_cc[VMIN] = 1;
	tty.c_cc[VTIME] = 1;

	if (swf)
		tty.c_iflag |= IXON | IXOFF;
	else
		tty.c_iflag &= ~(IXON | IXOFF | IXANY);

	tty.c_cflag &= ~(PARENB | PARODD);

	if (par[0] == 'E')
		tty.c_cflag |= PARENB;
	else if (par[0] == 'O')
		tty.c_cflag |= (PARENB | PARODD);

	if (tcsetattr(fd, TCSANOW, &tty) < 0) {
		g_free(old_setting);
		return FALSE;
	}

	__tty_setrts(fd);
	__tty_sethwf(fd, hwf);

	return TRUE;
}

/*
 * Close TTY Device
 */
static gboolean __tty_close(gint fd)
{
	TtyOldSetting *old_setting = NULL;

	dbg("Enter");

	/* Get previous settings */
	old_setting = __search_tty_oldsetting(fd);
	if (old_setting == NULL) {
		dbg("No previous settings found!!!");
		return TRUE;
	}

	if (tcsetattr(fd, TCSAFLUSH, &old_setting->termiosVal) < 0)	{
		err("Close failed");
		return FALSE;
	}

	/* Remove the previous setting configured */
	__remove_tty_oldsetting(old_setting);

	/* Free memory */
	g_free(old_setting);

	/* Close fd */
	close(fd);

	return TRUE;
}

/*
 * Wait on select
 */
static void __sleep(gint sec, gint msec)
{
	struct timeval tv;

	tv.tv_sec = sec;
	tv.tv_usec = msec;

	select(0, NULL, NULL, NULL, &tv);
}

/*
 * Close the VDPRAM device
 */
gboolean vdpram_close(gint fd)
{
	gint ret;

	dbg("Enter");

	/* Close VDPRAM Device */
	ret = __tty_close(fd);

	return ret;
}

/*
 * Open the VDPRAM device
 */
gint vdpram_open (void)
{
	gint rv = -1;
	gint fd = -1;
	gint val = 0;
	guint cmd =0;

	dbg("Enter");

	/* Open DPRAM device */
	fd = open(VDPRAM_OPEN_PATH, O_RDWR);
	if (fd < 0) {
		err("Open VDPRAM file - [FAIL] Error: [%s]", strerror(errno));
		return rv;
	} else {
		dbg("Open VDPRAM file - [SUCCESS] fd: [%d] path: [%s]",
			fd, VDPRAM_OPEN_PATH);
	}

	/* Set device parameters */
	if (__tty_setparms(fd, "115200", "N", "8", "1", 0, 0) != TRUE) {
		err("Set TTY device parameters - [FAIL]");

		/* Close VDPRAM Device */
		(void)vdpram_close(fd);
		return rv;
	}
	else {
		dbg("Set TTY device parameters - [SUCCESS]");
	}

	/* TODO: No need to check Status. Delete */
	cmd = HN_DPRAM_PHONE_GETSTATUS;
	if (ioctl(fd, cmd, &val) < 0) {
		err("Get Phone status - [FAIL] fd: [d] cmd: [%d] val: [%d]",
			fd, cmd, val);

		/* Close Device */
		(void)vdpram_close(fd);

		return rv;
	} else {
		dbg("Get Phone status - [SUCCESS]");
	}

	return fd;
}

/*
 * Power ON the Phone
 */
gboolean vdpram_poweron(gint fd)
{
	if (ioctl(fd, HN_DPRAM_PHONE_ON, NULL) < 0) {
		err("Phone Power ON [FAIL] - fd: [%d] Error: [%s]", fd, strerror(errno));
		return FALSE;
	}
	else {
		dbg("Phone Power ON [SUCCESS] - fd: [%d]", fd);
		return TRUE;
	}
}

/*
 * Power OFF the Phone
 */
gboolean vdpram_poweroff(gint fd)
{
	if (ioctl(fd, HN_DPRAM_PHONE_OFF, NULL) < 0) {
		err("Phone Power OFF [FAIL] - fd: [%d] Error: [%s]", fd, strerror(errno));
		return FALSE;
	}
	else {
		dbg("Phone Power OFF [SUCCESS] - fd: [%d]", fd);
		return TRUE;
	}
}

/*
 * Read data from VDPRAM
 */
gint vdpram_tty_read(gint fd, void *buf, size_t buf_len)
{
	gint actual = 0;

	if ((actual = read(fd, buf, buf_len)) < 0) {
		err("Read [FAIL] - fd: [%d] Error: [%s]", fd, strerror(errno));
	}

	return actual;
}

/*
 * Write data to VDPRAM
 */
gint vdpram_tty_write(gint fd, void *buf, size_t buf_len)
{
	size_t actual = 0;
	guint retry = 0;
	gint ret;

	while(actual < buf_len) {
		/* Write to Device */
		ret = write(fd, (guchar *)buf, buf_len - actual);
		if (ret < 0) {
			err("Write [FAIL] - fd: [%d] Error: [%s]",
				fd, strerror(errno));

			if ((errno == EAGAIN) || (errno == EBUSY)) {
				/* Sleep for 50 msecs */
				__sleep(SLEEP_TIME_IN_SEC, SLEEP_TIME_IN_MSEC);

				if (retry == MAX_RETRY_COUNT) {
					err("Maximum retries completed!!!");
					return 0;
				}

				retry = retry + 1;
				continue;
			}

			if (actual != buf_len)
				err("Write [FAIL] - fd: [%d]", fd);

			err("Write [FAIL] - Error: [%s]", strerror(errno));
			return actual;
		}

		actual  += ret;
		buf     += ret;
		dbg("Write Actual bytes: [%d] Written bytes: [%d]", actual, ret);
	};

	return actual;
}
