/*
 * tel-plugin-vmodem
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact: Junhwan An <jh48.an@samsung.com>
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
#include "legacy/TelUtility.h"
#include "vdpram.h"
#include "vdpram_dump.h"

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

/* Retry parameters */
#define SLEEP_TIME_IN_SEC		0
#define SLEEP_TIME_IN_MSEC		50

#define MAX_RETRY_COUNT			10

typedef struct _tty_old_setting_t {
	int		fd;
	struct	termios  termiosVal;
	struct	_tty_old_setting_t *next;
	struct	_tty_old_setting_t *prev;
} tty_old_setting_t;

#define VDPRAM_OPEN_PATH		"/dev/vdpram0"

/* DPRAM ioctls for DPRAM tty devices */
#define IOC_MZ_MAGIC		('h')
#define HN_DPRAM_PHONE_ON			_IO (IOC_MZ_MAGIC, 0xd0)
#define HN_DPRAM_PHONE_OFF			_IO (IOC_MZ_MAGIC, 0xd1)
#define HN_DPRAM_PHONE_GETSTATUS	_IOR(IOC_MZ_MAGIC, 0xd2, unsigned int)

static tty_old_setting_t *ttyold_head = NULL;

/*
 *	Insert TTY old settings.
 */
static void __insert_tty_oldsetting(tty_old_setting_t *me)
{
	dbg("Function Entrance");

	if (me == NULL)
		return;

	if (ttyold_head)
		ttyold_head->prev = me;

	me->next = ttyold_head;
	me->prev = 0;
	ttyold_head = me;
}

/*
 *	Search TTY old settings.
 */
static tty_old_setting_t *__search_tty_oldsetting(int fd)
{
	tty_old_setting_t *tty = NULL;

	dbg("Function Entrance");

	if (ttyold_head == NULL)
		return NULL;

	tty = ttyold_head;

	do {
		if (tty->fd == fd) {
			dbg("oldsetting for inputted fd [%d] is found", fd);
			break;
		} else {
			if (tty->next == NULL) {
				err("No oldsetting found!!!");
				tty = NULL;
				break;
			}
			tty = tty->next;
		}
	} while (1);

	return tty;
}

/*
 *	Remove TTY old settings.
 */
static void __remove_tty_oldsetting(tty_old_setting_t *me)
{
	dbg( "Function Enterence.");

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
 *	Set hardware flow control.
 */
static void __tty_sethwf(int fd, int on)
{
	struct termios tty;

	dbg("Function Entrance");

	if (tcgetattr(fd, &tty))
		err("__tty_sethwf: tcgetattr:");

	if (on)
		tty.c_cflag |= CRTSCTS;
	else
		tty.c_cflag &= ~CRTSCTS;

	if (tcsetattr(fd, TCSANOW, &tty))
		err("__tty_sethwf: tcsetattr:");
}

/*
 *	Set RTS line. Sometimes dropped. Linux specific?
 */
static int __tty_setrts(int fd)
{
	int mcs;

	dbg("Function Entrance");

	if (-1 ==  ioctl(fd, TIOCMODG, &mcs))
		err("icotl: TIOCMODG");

	mcs |= TIOCM_RTS;

	if (-1 == ioctl(fd, TIOCMODS, &mcs))
		err("icotl: TIOCMODS");

	return 0;
}

/*
 *	Set baudrate, parity and number of bits.
 */
static int __tty_setparms(int fd, char* baudr, char* par, char* bits, char* stop, int hwf, int swf)
{
	int spd = -1;
	int newbaud;
	int bit = bits[0];
	int stop_bit = stop[0];

	struct termios tty;
	tty_old_setting_t *old_setting = NULL;

	dbg("Function Entrance");

	old_setting = g_try_new0(tty_old_setting_t, 1);

	if (old_setting == NULL)
		return TAPI_API_SYSTEM_OUT_OF_MEM;

	old_setting->fd = fd;

	if (tcgetattr(fd, &tty) < 0) {
		g_free(old_setting);
		return TAPI_API_TRANSPORT_LAYER_FAILURE;
	}

	if (tcgetattr(fd, &old_setting->termiosVal) < 0) {
		g_free(old_setting);
		return TAPI_API_TRANSPORT_LAYER_FAILURE;
	}

	__insert_tty_oldsetting(old_setting);

	fflush(stdout);

	/* We generate mark and space parity ourself. */
	if (bit == '7' && (par[0] == 'M' || par[0] == 'S'))
		bit = '8';

	/* Check if 'baudr' is really a number */
	if ((newbaud = (atol(baudr) / 100)) == 0 && baudr[0] != '0')
		newbaud = -1;

	switch(newbaud) {
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

	switch(bit) {
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

	switch(stop_bit) {
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
		return TAPI_API_TRANSPORT_LAYER_FAILURE;
	}

	__tty_setrts(fd);
	__tty_sethwf(fd, hwf);

	return TAPI_API_SUCCESS;
}

/*
 *	Close TTY Device.
 */
static int __tty_close(int fd)
{
	tty_old_setting_t *old_setting = NULL;

	dbg("Function Entrance");

	/* Get previous settings */
	old_setting = __search_tty_oldsetting(fd);
	if (old_setting == NULL) {
		dbg("[VDPRAM] No previous settings found!!!");
		return TAPI_API_SUCCESS;
	}

	if (tcsetattr(fd, TCSAFLUSH, &old_setting->termiosVal) < 0)	{
		err("[VDPRAM] Close failed");
		return TAPI_API_TRANSPORT_LAYER_FAILURE;
	}

	/* Remove the previous setting configured */
	__remove_tty_oldsetting(old_setting);

	/* Free memory */
	g_free(old_setting);

	/* Close fd */
	close(fd);

	return TAPI_API_SUCCESS;
}

/*
 *	Wait on select.
 */
static void __sleep(int sec, int msec)
{
    struct timeval tv;

    tv.tv_sec = sec;
    tv.tv_usec = msec;

    select(0, NULL, NULL, NULL, &tv);
}

/*
 * Close the VDPRAM device
 */
int vdpram_close(int fd)
{
	int ret = TAPI_API_SUCCESS;

	dbg("Function Entrance");

	/* Close VDPRAM Device */
	ret = __tty_close(fd);

	return ret;
}

/*
 * Open the VDPRAM device
 */
int vdpram_open (void)
{
	int rv = -1;
	int fd = -1;
	int val = 0;
	unsigned int cmd =0;

	dbg("Function Enterence.");

	/* Open DPRAM device */
	fd = open(VDPRAM_OPEN_PATH, O_RDWR);
	if (fd < 0) {
		err("[VDPRAM] Open VDPRAM file - [FAIL] Error: [%s]", strerror(errno));
		return rv;
	} else {
		dbg("[VDPRAM] Open VDPRAM file - [SUCCESS] fd: [%d] path: [%s]",
										fd, VDPRAM_OPEN_PATH);
	}

	/* Set device parameters */
	if (__tty_setparms(fd, "115200", "N", "8", "1", 0, 0) != TAPI_API_SUCCESS) {
		err("[VDPRAM] Set TTY device parameters - [FAIL]");

		/* Close VDPRAM Device */
		vdpram_close(fd);
		return rv;
	}
	else {
		dbg("[VDPRAM] Set TTY device parameters - [SUCCESS]");
	}

	/* TODO: No need to check Status. Delete */
	cmd = HN_DPRAM_PHONE_GETSTATUS;
	if (ioctl(fd, cmd, &val) < 0) {
		err("[VDPRAM] Get Phone status - [FAIL] fd: [d] cmd: [%d] val: [%d]",
											fd, cmd, val);

		/* Close Device */
		vdpram_close(fd);

		return rv;
	} else {
		dbg("[VDPRAM] Get Phone status - [SUCCESS]");
	}

	return fd;
}

/*
 *	Power ON the Phone.
 */
gboolean vdpram_poweron(int fd)
{
	if (ioctl(fd, HN_DPRAM_PHONE_ON, NULL) < 0) {
		err("[VDPRAM] Phone Power ON [FAIL] - fd: [%d] Error: [%s]", fd, strerror(errno));
		return FALSE;
	}
	else {
		dbg("[VDPRAM] Phone Power ON [SUCCESS] - fd: [%d]", fd);
		return TRUE;
	}
}

/*
 *	Power OFF the Phone.
 */
gboolean vdpram_poweroff(int fd)
{
	if (ioctl(fd, HN_DPRAM_PHONE_OFF, NULL) < 0) {
		err("[VDPRAM] Phone Power OFF [FAIL] - fd: [%d] Error: [%s]", fd, strerror(errno));
		return FALSE;
	}
	else {
		dbg("[VDPRAM] Phone Power OFF [SUCCESS] - fd: [%d]", fd);
		return TRUE;
	}
}

/*
 *	Read data from VDPRAM.
 */
int vdpram_tty_read(int nFd, void* buf, size_t nbytes)
{
	int	actual = 0;

	if ((actual = read(nFd, buf, nbytes)) < 0) {
		err("[VDPRAM] Read [FAIL] - fd: [%d] Error: [%s]", nFd, strerror(errno));
	}

	/* Dumping Read data */
	vdpram_hex_dump(RX, actual, buf);

	return actual;
}

/*
 *	Write data to VDPRAM.
 */
int vdpram_tty_write(int nFd, void* buf, size_t nbytes)
{
	int ret;
	size_t actual = 0;
	int	retry = 0;

	do {
		vdpram_hex_dump(TX, nbytes, buf);

		/* Write to Device */
		ret = write(nFd, (unsigned char* )buf, nbytes - actual);
		if (ret < 0) {
			err("[VDPRAM] Write [FAIL] - fd: [%d] Error: [%s]",
												nFd, strerror(errno));

			if ((errno == EAGAIN) || (errno == EBUSY)) {
				/* Sleep for 50 msecs */
				__sleep(SLEEP_TIME_IN_SEC, SLEEP_TIME_IN_MSEC);

				if (retry == MAX_RETRY_COUNT) {
					err("[VDPRAM] Maximum retries completed!!!");
					return 0;
				}

				retry = retry + 1;
				continue;
			}

			if (actual != nbytes)
				err("[VDPRAM] Write [FAIL] - fd: [%d]", nFd);

			err("[VDPRAM] Write [FAIL] - Error: [%s]", strerror(errno));
			return actual;
		}

		actual  += ret;
		buf     += ret;
		dbg("[VDPRAM] Write Actual bytes: [%d] Written bytes: [%d]", actual, ret);
	} while(actual < nbytes);

	return actual;
}
