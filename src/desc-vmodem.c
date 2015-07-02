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

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>

#include <glib.h>

#include <tcore.h>
#include <plugin.h>
#include <server.h>
#include <user_request.h>
#include <hal.h>
#include <core_object.h>

#include "vdpram.h"

#define SERVER_INIT_WAIT_TIMEOUT		500

#define DEVICE_NAME_LEN_MAX			16
#define DEVICE_NAME_PREFIX			"pdp"

#define BUF_LEN_MAX				512

#define CORE_OBJECT_NAME_MAX			16

#define MODEM_PLUGIN_NAME			"atmodem-plugin.so"

#define BIT_SIZE(type) (sizeof(type) * 8)

#define COPY_MASK(type) ((0xffffffff) >> (32 - BIT_SIZE(type)))

#define MASK(width, offset, data) \
	(((width) == BIT_SIZE(data)) ? (data) :	 \
	 ((((COPY_MASK(data) << (BIT_SIZE(data) - ((width) % BIT_SIZE(data)))) & COPY_MASK(data)) >> (offset)) & (data))) \


#define MASK_AND_SHIFT(width, offset, shift, data)	\
	((((signed) (shift)) < 0) ?		  \
	 MASK((width), (offset), (data)) << -(shift) :	\
	 MASK((width), (offset), (data)) >> (((signed) (shift)))) \

struct custom_data {
	int vdpram_fd;
	guint watch_id_vdpram;
};

typedef struct {
	TcoreHal *hal;
	TcoreModem *modem;
} PluginData;

struct v_modules {
	unsigned int co_type;
	char co_name[CORE_OBJECT_NAME_MAX];
};

static char __util_unpackb(const char *src, int pos, int len)
{
	char result = 0;
	int rshift = 0;

	src += pos / 8;
	pos %= 8;

	rshift = MAX(8 - (pos + len), 0);

	if (rshift > 0) {
		result = MASK_AND_SHIFT(len, pos, rshift, (unsigned char)*src);
	} else {
		result = MASK(8 - pos, pos, (unsigned char)*src);
		src++;
		len -= 8 - pos;

		if (len > 0) result = (result << len) | (*src >> (8 - len));   /* if any bits left */
	}

	return result;
}

static char __util_convert_byte_hexchar(char val)
{
	char hex_char;

	if (val <= 9)
		hex_char = (char) (val + '0');
	else if (val >= 10 && val <= 15)
		hex_char = (char) (val - 10 + 'A');
	else
		hex_char = '0';

	return (hex_char);
}

static gboolean __util_byte_to_hex(const char *byte_pdu, char *hex_pdu, int num_bytes)
{
	int i;
	char nibble;
	int buf_pos = 0;

	for (i = 0; i < num_bytes * 2; i++) {
		nibble = __util_unpackb(byte_pdu, buf_pos, 4);
		buf_pos += 4;
		hex_pdu[i] = __util_convert_byte_hexchar(nibble);
	}

	return TRUE;
}

static TcoreModem *__get_modem(TcorePlugin *modem_iface_plugin)
{
	PluginData *user_data;

	if (modem_iface_plugin == NULL)
		return NULL;

	user_data = tcore_plugin_ref_user_data(modem_iface_plugin);
	if (user_data == NULL)
			return NULL;

	/* 'modem' corresponding to Modem Interface plug-in */
	return user_data->modem;
}

static guint __vmodem_reencode_mt_sms(gchar *mt_sms, guint mt_sms_len)
{
#define VMODEM_CR	0x0D
#define VMODEM_LF	0x0A
#define VMODEM_COLON	0x3A

	gchar sms_buf[BUF_LEN_MAX] = {0, };
	guint sca_len, pdu_len, tpdu_len;
	gushort tpdu_len_ptr = 0;
	gchar tpdu_len_str[8] = {0};
	guint i, local_index = 0;

	if (mt_sms_len > (BUF_LEN_MAX - 2))
		mt_sms_len = BUF_LEN_MAX - 2;

	for (i = 0; i < mt_sms_len; i++) {
		if ((mt_sms[i] == VMODEM_CR)
				&& (mt_sms[i+1] == VMODEM_LF)) {
			sms_buf[i] = mt_sms[i];
			i++;
			sms_buf[i] = mt_sms[i];
			i++;
			break;
		} else if (mt_sms[i] == VMODEM_COLON) {
			tpdu_len_ptr = i + 1;
		}

		/* Byte copy */
		sms_buf[i] = mt_sms[i];
	}
	sca_len = mt_sms[i];
	dbg("SCA length: [%d] TPDU length offset: [%d]", sca_len, tpdu_len_ptr);

	pdu_len = (mt_sms_len-i);
	tpdu_len = pdu_len - (sca_len+1);
	dbg("PDU length: [%d] Actual TPDU Length: [%d]", pdu_len, tpdu_len);

	if (pdu_len >= 100 && tpdu_len < 100) {
		/*
		 * Move back complete buffer by a Byte (to fill up the
		 * void created by hundreds place).
		 */
		if (i > 3) {
			sms_buf[i-3] = VMODEM_CR;
			sms_buf[i-2] = VMODEM_LF;

			__util_byte_to_hex(&mt_sms[i], &sms_buf[i - 1], pdu_len);
			i += (2*pdu_len - 1);
		}
	} else {
		__util_byte_to_hex(&mt_sms[i], &sms_buf[i], pdu_len);
		i += 2*pdu_len;
	}

	/* Update actual TPDU length */
	snprintf(tpdu_len_str, 8, "%d", tpdu_len);
	switch (strlen(tpdu_len_str)) {
	case 4:			/* 100s place */
		dbg("1000s : [%d]", tpdu_len_str[local_index]);

		sms_buf[tpdu_len_ptr] = tpdu_len_str[local_index];
		tpdu_len_ptr++;

		local_index++;
	case 3:			/* 100s place */
		dbg("100s : [%d]", tpdu_len_str[local_index]);

		sms_buf[tpdu_len_ptr] = tpdu_len_str[local_index];
		tpdu_len_ptr++;

		local_index++;
	case 2:			/* 10s place */
		dbg("10s : [%d]", tpdu_len_str[local_index]);

		sms_buf[tpdu_len_ptr] = tpdu_len_str[local_index];
		tpdu_len_ptr++;

		local_index++;
	case 1:			/* 1s place */
		dbg("1s : [%d]", tpdu_len_str[local_index]);

		sms_buf[tpdu_len_ptr] = tpdu_len_str[local_index];
		tpdu_len_ptr++;
	break;
	default:
		dbg("Unsupported length: [%d]", strlen(tpdu_len_str));
	break;
	}

	/*
	 * Greater than  (BUF_LEN_MAX - 2),
	 * restrict the length to ( BUF_LEN_MAX - 2).
	 *
	 * This is to accomadate <CR> & <LF>.
	 */
	if (i > (BUF_LEN_MAX - 2))
		i = BUF_LEN_MAX - 2;

	/* Append <CR> & <LF> */
	sms_buf[i++] = VMODEM_CR;
	sms_buf[i++] = VMODEM_LF;
	dbg("MT SMS: [%s]", sms_buf);

	tcore_util_hex_dump("        ", (int)i, sms_buf);

	/*
	 * Copy back
	 *
	 * 'data_len' is not accessed hence it need not be updated.
	 */
	g_strlcpy(mt_sms, sms_buf, i+1);
	dbg("Encoded MT SMS: [%d][%s]", i, mt_sms);

	return i;
}

static guint __register_gio_watch(TcoreHal *h, int fd, void *callback)
{
	GIOChannel *channel = NULL;
	guint source;

	dbg("Register to Watch list - fd: [%d]", fd);

	if ((fd < 0) || (callback == NULL))
		return 0;

	channel = g_io_channel_unix_new(fd);
	source = g_io_add_watch(channel, G_IO_IN, (GIOFunc) callback, h);
	g_io_channel_unref(channel);
	channel = NULL;

	return source;
}

static void __deregister_gio_watch(guint watch_id)
{
	dbg("Deregister Watch ID: [%d]", watch_id);

	/* Remove source */
	g_source_remove(watch_id);
}

static gboolean __load_modem_plugin(gpointer data)
{
	TcoreHal *hal;
	TcorePlugin *plugin;
	struct custom_data *user_data;
	TcoreModem *modem;
	unsigned int slot_count = 1;

	dbg("Entry");

	if (data == NULL) {
		err("data is NULL");
		return FALSE;
	}

	hal = data;
	plugin = tcore_hal_ref_plugin(hal);
	modem = __get_modem(plugin);

	/* Load Modem Plug-in */
	if (tcore_server_load_modem_plugin(tcore_plugin_ref_server(plugin),
			modem, MODEM_PLUGIN_NAME) == TCORE_RETURN_FAILURE) {
		err("Load Modem Plug-in - [FAIL]");

		goto EXIT;
	} else {
		dbg("Load Modem Plug-in - [SUCCESS]");
	}

	tcore_server_send_notification(tcore_plugin_ref_server(plugin),
		NULL, TNOTI_SERVER_ADDED_MODEM_PLUGIN_COMPLETED,
		sizeof(slot_count), &slot_count);

	/* To stop the cycle need to return FALSE */
	return FALSE;

EXIT:
	user_data = tcore_hal_ref_user_data(hal);
	if (user_data == NULL)
		return FALSE;

	/* Deregister from Watch list */
	__deregister_gio_watch(user_data->watch_id_vdpram);

	/* Free HAL */
	tcore_hal_free(hal);

	/* Close VDPRAM device */
	vdpram_close(user_data->vdpram_fd);

	/* Free custom data */
	g_free(user_data);

	return FALSE;
}

static TReturn _modem_power(TcoreHal *hal, gboolean enable)
{
	struct custom_data *user_data;

	user_data = tcore_hal_ref_user_data(hal);
	if (user_data == NULL) {
		err(" User data is NULL");
		return TCORE_RETURN_FAILURE;
	}

	if (enable == TRUE) {			/* POWER ON */
		if (FALSE == vdpram_poweron(user_data->vdpram_fd)) {
			err(" Power ON - [FAIL]");
			return TCORE_RETURN_FAILURE;
		}

		/* Set Power State - ON */
		tcore_hal_set_power_state(hal, TRUE);
	} else {					/* POWER OFF */
		if (vdpram_poweroff(user_data->vdpram_fd) == FALSE) {
			err(" Power OFF - [FAIL]");
			return TCORE_RETURN_FAILURE;
		}

		/* Set Power state - OFF */
		tcore_hal_set_power_state(hal, FALSE);
	}

	return TCORE_RETURN_SUCCESS;
}

static gboolean on_recv_vdpram_message(GIOChannel *channel,
	GIOCondition condition, gpointer data)
{
	TcoreHal *hal = data;
	struct custom_data *custom;
	char buf[BUF_LEN_MAX];
	int n = 0;
	TReturn ret;

	custom = tcore_hal_ref_user_data(hal);
	memset(buf, 0x0, BUF_LEN_MAX);

	/* Read from Device */
	n = vdpram_tty_read(custom->vdpram_fd, buf, BUF_LEN_MAX);
	if (n < 0) {
		err(" Read error - Data received: [%d]", n);
		return TRUE;
	}
	dbg(" DPRAM Receive - Data length: [%d]", n);

	msg("\n---------- [RECV] Length of received data: [%d] ----------\n", n);

	/* Emit receive callback */
	tcore_hal_emit_recv_callback(hal, n, buf);

	/*
	 * This is to ensure that the received MT SMS (+CMT:) is
	 * encoded according to 3GPP standard
	 */
	if (buf[0] == 0x2B && buf[1] == 0x43 && buf[2] == 0x4D
			&& buf[3] == 0x54 && buf[4] == 0x3A) {
		dbg("Received - [MT SMS]");
		n = __vmodem_reencode_mt_sms((gchar *)buf, n);
	} else if (buf[0] == 0x25) {
		dbg("Replaced % --> +");
		buf[0] = 0x2B;
	}

	/* Dispatch received data to response handler */
	dbg("Invoking tcore_hal_dispatch_response_data()");
	ret = tcore_hal_dispatch_response_data(hal, 0, n, buf);
	msg("\n---------- [RECV FINISH] Receive processing: [%d] ----------\n", ret);

	return TRUE;
}

static TReturn hal_power(TcoreHal *hal, gboolean flag)
{
	return _modem_power(hal, flag);
}

static TReturn hal_send(TcoreHal *hal, unsigned int data_len, void *data)
{
	int ret;
	struct custom_data *user_data;

	if (tcore_hal_get_power_state(hal) == FALSE) {
		err(" HAL Power state - OFF");
		return TCORE_RETURN_FAILURE;
	}

	user_data = tcore_hal_ref_user_data(hal);
	if (user_data == NULL) {
		err(" User data is NULL");
		return TCORE_RETURN_FAILURE;
	}

	ret = vdpram_tty_write(user_data->vdpram_fd, data, data_len);
	if (ret < 0) {
		err(" Write failed");
		return TCORE_RETURN_FAILURE;
	} else {
		dbg("vdpram_tty_write success ret=%d (fd=%d, len=%d)",
			ret, user_data->vdpram_fd, data_len);
		return TCORE_RETURN_SUCCESS;
	}
}

static TReturn hal_setup_netif(CoreObject *co,
	TcoreHalSetupNetifCallback func, void *user_data,
	unsigned int cid, gboolean enable)
{
	char ifname[DEVICE_NAME_LEN_MAX];
	int size = 0;
	int fd = 0;
	char buf[32];
	const char *control = NULL;

	if (cid > 3) {
		err(" Context ID: [%d]", cid);
		return TCORE_RETURN_EINVAL;
	}

	if (enable == TRUE) {
		dbg(" ACTIVATE - Context ID: [%d]", cid);
		control = "/sys/class/net/svnet0/pdp/activate";
	} else {
		dbg(" DEACTIVATE - Context ID: [%d]", cid);
		control = "/sys/class/net/svnet0/pdp/deactivate";
	}

	fd = open(control, O_WRONLY);
	if (fd < 0) {
		err(" Failed to Open interface: [%s]", control);

		/* Invoke callback function */
		if (func)
			func(co, -1, NULL, user_data);

		return TCORE_RETURN_FAILURE;
	}

	/* Context ID needs to be written to the Device */
	snprintf(buf, sizeof(buf), "%d", cid);
	size = write(fd, buf, strlen(buf));
	dbg(" SIZE [%d]", size);

	/* Close 'fd' */
	close(fd);

	/* Device name */
	snprintf(ifname, DEVICE_NAME_LEN_MAX, "%s%d", DEVICE_NAME_PREFIX, (cid - 1));
	dbg(" Interface Name: [%s]", ifname);

	/* Invoke callback function */
	if (func)
		func(co, 0, ifname, user_data);

	return TCORE_RETURN_SUCCESS;
}

/* HAL Operations */
static struct tcore_hal_operations hal_ops = {
	.power = hal_power,
	.send = hal_send,
	.setup_netif = hal_setup_netif,
};

static gboolean on_load()
{
	dbg(" Load!!!");

	return TRUE;
}

static gboolean on_init(TcorePlugin *plugin)
{
	TcoreHal *hal;
	PluginData *user_data;
	struct custom_data *data;

	dbg(" Init!!!");

	if (plugin == NULL) {
		err(" PLug-in is NULL");
		return FALSE;
	}

	/* User Data for Modem Interface Plug-in */
	user_data = g_try_new0(PluginData, 1);
	if (user_data == NULL) {
		err(" Failed to allocate memory for Plugin data");
		return FALSE;
	}

	/* Register to Server */
	user_data->modem = tcore_server_register_modem(tcore_plugin_ref_server(plugin), plugin);
	if (user_data->modem == NULL) {
		err(" Registration Failed");
		g_free(user_data);
		return FALSE;
	}
	dbg(" Registered from Server");


	data = g_try_new0(struct custom_data, 1);
	if (data == NULL) {
		err(" Failed to allocate memory for Custom data");

		/* Unregister from Server */
		tcore_server_unregister_modem(tcore_plugin_ref_server(plugin), user_data->modem);

		/* Free Plugin data */
		g_free(user_data);

		return FALSE;
	}

	/*
	 * Open DPRAM device
	 */
	data->vdpram_fd = vdpram_open();
	if (data->vdpram_fd < 0) {
		/* Fre custom data */
		g_free(data);

		/* Unregister from Server */
		tcore_server_unregister_modem(tcore_plugin_ref_server(plugin), user_data->modem);

		/* Free Plugin data */
		g_free(user_data);

		return FALSE;
	}
	/*
	 * Create and initialize HAL
	 */
	hal = tcore_hal_new(plugin, "vmodem", &hal_ops, TCORE_HAL_MODE_AT);
	if (hal == NULL) {
		/* Close VDPRAM device */
		vdpram_close(data->vdpram_fd);

		/* Fre custom data */
		g_free(data);

		/* Unregister from Server */
		tcore_server_unregister_modem(tcore_plugin_ref_server(plugin), user_data->modem);

		/* Fre Plugin data */
		g_free(user_data);

		return FALSE;
	}
	user_data->hal = hal;

	/* Link custom data to HAL user data */
	tcore_hal_link_user_data(hal, data);

	/* Set HAL as Modem Interface Plug-in's User data */
	tcore_plugin_link_user_data(plugin, user_data);

	/* Register to Watch list */
	data->watch_id_vdpram = __register_gio_watch(hal,
				data->vdpram_fd, on_recv_vdpram_message);
	dbg(" fd: [%d] Watch ID: [%d]",
				data->vdpram_fd, data->watch_id_vdpram);

	/* Power ON VDPRAM device */
	if (_modem_power(hal, TRUE) == TCORE_RETURN_SUCCESS) {
		dbg(" Power ON - [SUCCESS]");
	} else {
		err(" Power ON - [FAIL]");
		goto EXIT;
	}

	/* Check CP Power ON */
	g_timeout_add_full(G_PRIORITY_HIGH, SERVER_INIT_WAIT_TIMEOUT, __load_modem_plugin, hal, 0);

	dbg("[VMMODEM] Exit");
	return TRUE;

EXIT:
	/* Deregister from Watch list */
	__deregister_gio_watch(data->watch_id_vdpram);

	/* Free HAL */
	tcore_hal_free(hal);

	/* Close VDPRAM device */
	vdpram_close(data->vdpram_fd);

	/* Free custom data */
	g_free(data);

	/* Unregister from Server */
	tcore_server_unregister_modem(tcore_plugin_ref_server(plugin), user_data->modem);

	/*Free Plugin Data*/
	g_free(user_data);

	return FALSE;
}

static void on_unload(TcorePlugin *plugin)
{
	TcoreHal *hal;
	struct custom_data *data;
	PluginData *user_data;

	dbg(" Unload!!!");

	if (plugin == NULL)
		return;

	user_data = tcore_plugin_ref_user_data(plugin);
	if (user_data == NULL)
		return;

	hal = user_data->hal;

	/* Unload Modem Plug-in */
#if 0	/* TODO - Open the code below */
	tcore_server_unload_modem_plugin(tcore_plugin_ref_server(plugin), plugin);
#endif
	data = tcore_hal_ref_user_data(hal);
	if (data == NULL)
		return;

	/* Deregister from Watch list */
	__deregister_gio_watch(data->watch_id_vdpram);
	dbg(" Deregistered Watch ID");

	/* Free HAL */
	tcore_hal_free(hal);
	dbg(" Freed HAL");

	/* Close VDPRAM device */
	vdpram_close(data->vdpram_fd);
	dbg(" Closed VDPRAM device");

	/* Free custom data */
	g_free(data);

	tcore_server_unregister_modem(tcore_plugin_ref_server(plugin), user_data->modem);
	dbg(" Unregistered from Server");

	dbg(" Unloaded MODEM");
	g_free(user_data);
}

/* VMODEM Descriptor Structure */
EXPORT_API struct tcore_plugin_define_desc plugin_define_desc = {
	.name = "VMODEM",
	.priority = TCORE_PLUGIN_PRIORITY_HIGH,
	.version = 1,
	.load = on_load,
	.init = on_init,
	.unload = on_unload
};
