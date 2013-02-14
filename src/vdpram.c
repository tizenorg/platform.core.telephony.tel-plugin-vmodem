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

typedef struct _tty_old_setting_t{
	int		fd;
	struct	termios  termiosVal;
	struct	_tty_old_setting_t *next;
	struct	_tty_old_setting_t *prev;
} tty_old_setting_t;

#define VDPRAM_OPEN_PATH		"/dev/dpram/0"

/* DPRAM ioctls for DPRAM tty devices */
#define IOC_MZ_MAGIC		('h')
#define HN_DPRAM_PHONE_ON			_IO (IOC_MZ_MAGIC, 0xd0)
#define HN_DPRAM_PHONE_OFF			_IO (IOC_MZ_MAGIC, 0xd1)
#define HN_DPRAM_PHONE_GETSTATUS	_IOR(IOC_MZ_MAGIC, 0xd2, unsigned int)

static tty_old_setting_t *ttyold_head = NULL;

/* static functions */
static void __insert_tty_oldsetting(tty_old_setting_t *me)
{
	dbg("Function Enterence.");

	if (me == NULL)
		return;

	if (ttyold_head)
	    ttyold_head->prev = me;

	me->next = ttyold_head;
	me->prev = 0;
	ttyold_head = me;
}

static tty_old_setting_t *__search_tty_oldsetting(int fd)
{
	tty_old_setting_t *tty = NULL;

	dbg("Function Enterence.");

	if (ttyold_head == NULL)
		return NULL;

	tty = ttyold_head;

	do{
		if (tty->fd == fd) {
			dbg("oldsetting for inputted fd is found");
			break;
		}
		else {
			if (tty->next == NULL) {
				dbg("No oldsetting is found");
				tty = NULL;
				break;
			}
			tty = tty->next;
		}
	}while(1);

	return tty;
}

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

/* Set hardware flow control.
*/
static void __tty_sethwf(int fd, int on)
{
	struct termios tty;

	dbg("Function Enterence.");

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
* Set RTS line. Sometimes dropped. Linux specific?
*/
static int __tty_setrts(int fd)
{
	int mcs;

	dbg("Function Enterence.");

	if (-1 ==  ioctl(fd, TIOCMODG, &mcs))
		err("icotl: TIOCMODG");

	mcs |= TIOCM_RTS;

	if (-1 == ioctl(fd, TIOCMODS, &mcs))
		err("icotl: TIOCMODS");

	return 0;
}

/*
 * Set baudrate, parity and number of bits.
 */
static int __tty_setparms(int fd, char* baudr, char* par, char* bits, char* stop, int hwf, int swf)
{
	int spd = -1;
	int newbaud;
	int bit = bits[0];
	int stop_bit = stop[0];

	struct termios tty;
	tty_old_setting_t *old_setting = NULL;

	dbg("Function Enterence.");

	old_setting = calloc(sizeof(tty_old_setting_t), 1);

	if (old_setting == NULL)
		return TAPI_API_SYSTEM_OUT_OF_MEM;

	old_setting->fd = fd;

	if (tcgetattr(fd, &tty) < 0) {
		free(old_setting);
		return TAPI_API_TRANSPORT_LAYER_FAILURE;
	}

	if (tcgetattr(fd, &old_setting->termiosVal) < 0) {
		free(old_setting);
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

	switch(newbaud)
	{
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

	switch(bit)
	{
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

	switch(stop_bit)
	{
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
		free(old_setting);
	    return TAPI_API_TRANSPORT_LAYER_FAILURE;
	}

	__tty_setrts(fd);
	__tty_sethwf(fd, hwf);

	return TAPI_API_SUCCESS;

}

static int __tty_close(int fd)
{
	tty_old_setting_t *old_setting = NULL;

	dbg("Function Enterence.");

	old_setting = __search_tty_oldsetting(fd);
	if (old_setting == NULL)
		return TAPI_API_SUCCESS;

	if (tcsetattr(fd, TCSAFLUSH, &old_setting->termiosVal) < 0)	{
		err("close failed");
		return TAPI_API_TRANSPORT_LAYER_FAILURE;
	}

	__remove_tty_oldsetting(old_setting);

	free(old_setting);

	close(fd);

	return TAPI_API_SUCCESS;
}

/*
* restore the old settings before close.
*/
int vdpram_close(int fd)
{
	int ret = TAPI_API_SUCCESS;

	dbg("Function Enterence.");

	ret = __tty_close(fd);

	return ret;
}

/*
*	Open the vdpram fd.
*/
int vdpram_open (void)
{
	int rv = -1;
	int fd = -1;
	int val = 0;
	unsigned int cmd =0;

	fd = open(VDPRAM_OPEN_PATH, O_RDWR);

	if (fd < 0) {
		err("#### Failed to open vdpram file: error no hex %x", errno);
		return rv;
	}
	else
		dbg("#### Success to open vdpram file. fd:%d, path:%s", fd, VDPRAM_OPEN_PATH);


	if (__tty_setparms(fd, "115200", "N", "8", "1", 0, 0) != TAPI_API_SUCCESS) {
		vdpram_close(fd);
		return rv;
	}
	else
		dbg("#### Success set tty vdpram params. fd:%d", fd);

	/*TODO: No need to check Status. Delete*/
	cmd = HN_DPRAM_PHONE_GETSTATUS;

	if (ioctl(fd, cmd, &val) < 0) {
		err("#### ioctl failed fd:%d, cmd:%lu, val:%d", fd,cmd,val);
		vdpram_close(fd);
		return rv;
	}
	else
		dbg("#### ioctl Success fd:%d, cmd:%lu, val:%d", fd,cmd,val);

	return fd;

}

/*
*	power on the phone.
*/
int vdpram_poweron(int fd)
{
	int rv = -1;

	if (ioctl(fd, HN_DPRAM_PHONE_ON, NULL) < 0) {
		err("Phone Power On failed (fd:%d)", fd);
		rv = 0;
	}
	else {
		dbg("Phone Power On success (fd:%d)", fd);
		rv = 1;
	}
	return rv;
}

 /*
 *	Power Off the Phone.
 */
int vdpram_poweroff(int fd)
{
	int rv;

	if (ioctl(fd, HN_DPRAM_PHONE_OFF, NULL) < 0) {
		err("Phone Power Off failed.");
		rv = -1;
	}
	else {
		dbg("Phone Power Off success.");
		rv = 1;
	}

	return rv;
}

/*
*	Read data from vdpram.
*/

int vdpram_tty_read(int nFd, void* buf, size_t nbytes)
{
	int	actual = 0;

	if ((actual = read(nFd, buf, nbytes)) < 0) {
		dbg("[TRANSPORT DPRAM]read failed.");
	}
	vdpram_hex_dump(IPC_RX, actual, buf);

	return actual;
}

static void __selectsleep(int sec,int msec)
{
    struct timeval tv;
    tv.tv_sec=sec;
    tv.tv_usec=msec;
    select(0,NULL,NULL,NULL,&tv);
    return;
}

/*
*	Write data to vdpram.
*/
int vdpram_tty_write(int nFd, void* buf, size_t nbytes)
{
	int ret;
	size_t actual = 0;
	int	retry = 0;

	do {
		vdpram_hex_dump(IPC_TX, nbytes, buf);
		ret = write(nFd, (unsigned char* )buf, nbytes - actual);

		if ((ret < 0 && errno == EAGAIN) || (ret < 0 && errno == EBUSY)) {
			err("write failed. retry.. ret[%d] with errno[%d] ",ret, errno);
			__selectsleep(0,50);

			if (retry == 10)
				return 0;

			retry = retry + 1;
		    continue;
		}

		if (ret < 0) {
		    if (actual != nbytes)
				err("write failed.ret[%d]",ret);

			err("errno [%d]",errno);
			return actual;
		}

		actual  += ret;
		buf     += ret;

	} while(actual < nbytes);

	return actual;
}
/*	EOF	*/
