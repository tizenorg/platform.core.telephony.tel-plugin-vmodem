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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

#include <glib.h>

#include <tcore.h>
#include <server.h>
#include <plugin.h>
#include <hal.h>

#include "config.h"
#include "vdpram.h"
#include "vdpram_dump.h"

#define VMODEM_HAL_NAME		"vmodem"

#define DEVICE_NAME_LEN_MAX		16
#define DEVICE_NAME_PREFIX		"pdp"

#define BUF_LEN_MAX			512

#define AT_CP_POWER_ON_TIMEOUT	500

static guint __vmodem_reencode_mt_sms(gchar *mt_sms, guint mt_sms_len)
{
#define VMODEM_CR	0x0D
#define VMODEM_LF	0x0A
#define VMODEM_COLON	0x3A

	gchar sms_buf[BUF_LEN_MAX] = {0, };
	guint sca_len, pdu_len, tpdu_len;
	gushort tpdu_len_ptr;
	gchar data;
	guint i;

	for (i = 0; i < mt_sms_len; i++) {
		if ((mt_sms[i] == VMODEM_CR)
				&& (mt_sms[i+1] == VMODEM_LF)) {
			sms_buf[i] = mt_sms[i];
			i++;
			sms_buf[i] = mt_sms[i];
			i++;
			break;
		}
		else if (mt_sms[i] == VMODEM_COLON)
			tpdu_len_ptr = i+1;

		/* Byte copy */
		sms_buf[i] = mt_sms[i];
	}
	sca_len = mt_sms[i];
	dbg("SCA length: [%d] TPDU length offset: [%d]", sca_len, tpdu_len_ptr);

	pdu_len = (mt_sms_len-i);
	tpdu_len = pdu_len - (sca_len+1);
	dbg("PDU length: [%d] Actual TPDU Length: [%d]", pdu_len, tpdu_len);

	tcore_util_byte_to_hex(&mt_sms[i], &sms_buf[i], pdu_len);
	dbg("MT SMS: [%s]", sms_buf);

	/* Append <CR> & <LF> */
	i += 2*pdu_len;
	sms_buf[i++] = VMODEM_CR;
	sms_buf[i++] = VMODEM_LF;

	/* Update actual TPDU length */
	data = (tpdu_len/10) + '0';
	sms_buf[tpdu_len_ptr] = data;
	tpdu_len_ptr++;

	data = (tpdu_len%10) + '0';
	sms_buf[tpdu_len_ptr] = data;

	tcore_util_hex_dump("        ", (gint)i, sms_buf);

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

static TcoreHookReturn __on_hal_send(TcoreHal *hal,
		guint data_len, void *data, void *user_data)
{
	/* Dumping Send (Write) data */
	vdpram_hex_dump(TRUE, data_len, data);

	return TCORE_HOOK_RETURN_CONTINUE;
}

static void __on_hal_recv(TcoreHal *hal,
	guint data_len, const void *data, void *user_data)
{
	/* Dumping Receive (Read) data */
	vdpram_hex_dump(FALSE, data_len, (void *)data);
}

static gboolean __modem_power(TcoreHal *hal, gboolean enable)
{
	CustomData *user_data;

	user_data = tcore_hal_ref_user_data(hal);
	if (user_data == NULL) {
		err("User data is NULL");
		return FALSE;
	}

	if (enable == TRUE) {		/* POWER ON */
		if (FALSE == vdpram_poweron(user_data->vdpram_fd)) {
			err("Power ON - [FAIL]");
			return FALSE;
		}

		/* Set Power State - ON */
		tcore_hal_set_power_state(hal, TRUE);
	} else {				/* POWER OFF */
		if (vdpram_poweroff(user_data->vdpram_fd) == FALSE) {
			err("Power OFF - [FAIL]");
			return FALSE;
		}

		/* Set Power state - OFF */
		tcore_hal_set_power_state(hal, FALSE);
	}

	return TRUE;
}

static gboolean __on_recv_vdpram_message(GIOChannel *channel,
	GIOCondition condition, gpointer data)
{
	TcoreHal *hal = data;
	CustomData *custom;
	char buf[BUF_LEN_MAX] = {0, };
	int n = 0;
	TelReturn ret;

	custom = tcore_hal_ref_user_data(hal);
	memset(buf, 0x0, BUF_LEN_MAX);

	/* Read from Device */
	n = vdpram_tty_read(custom->vdpram_fd, buf, BUF_LEN_MAX);
	if (n < 0) {
		err("Read error - Data received: [%d]", n);
		return TRUE;
	}
	dbg("DPRAM Receive - Data length: [%d]", n);

	/* Emit receive callback */


	msg("\n---------- [RECV] Length of received data: [%d] ----------\n", n);

	/* Emit response callback */
	tcore_hal_emit_recv_callback(hal, n, buf);

	/*
	 * This is to ensure that the received MT SMS (+CMT:) is
	 * encoded according to 3GPP standard
	 */
	if (buf[0] == 0x2B && buf[1] == 0x43 && buf[2] == 0x4D
			&& buf[3] == 0x54 && buf[4] == 0x3A) {
		dbg("Received - [MT SMS]");
		n = __vmodem_reencode_mt_sms((gchar *)buf, n);
	}

	/* Dispatch received data to response handler */
	ret = tcore_hal_dispatch_response_data(hal, 0, n, buf);
	msg("\n---------- [RECV FINISH] Receive processing: [%d] ----------\n", ret);

	return TRUE;
}

static gboolean __power_on(gpointer data)
{
	CustomData *user_data;
	TcoreHal *hal = (TcoreHal*)data;

	dbg("Entry");

	user_data = tcore_hal_ref_user_data(hal);
	tcore_check_return_value_assert(user_data != NULL, TRUE);

	/*
	 * Open DPRAM device: Create and Open interface to CP
	 */
	user_data->vdpram_fd = vdpram_open();
	if (user_data->vdpram_fd < 1) {
		TcorePlugin *plugin = tcore_hal_ref_plugin(hal);
		Server *server = tcore_plugin_ref_server(plugin);

		err("Failed to Create/Open CP interface");

		/* Notify server a modem error occured */
		tcore_server_send_server_notification(server,
			TCORE_SERVER_NOTIFICATION_MODEM_ERR, 0, NULL);

		goto EXIT;
	}
	dbg("Created AP-CP interface");

	/* Register to Watch llist */
	user_data->vdpram_watch_id = __register_gio_watch(hal,
				user_data->vdpram_fd, __on_recv_vdpram_message);
	dbg("fd: [%d] Watch ID: [%d]", user_data->vdpram_fd, user_data->vdpram_watch_id);

	/* Power ON VDPRAM device */
	if (__modem_power(hal, TRUE)) {
		dbg("Power ON - [SUCCESS]");
	} else {
		err("Power ON - [FAIL]");
		goto EXIT;
	}

	/* CP is ONLINE, send AT+CPAS */
	vmodem_config_check_cp_power(hal);

	/* To stop the cycle need to return FALSE */
	return FALSE;

EXIT:
	/* TODO: Handle Deregister */

	/* To stop the cycle need to return FALSE */
	return FALSE;
}

/* HAL Operations */
static TelReturn _hal_power(TcoreHal *hal, gboolean flag)
{
	return __modem_power(hal, flag);
}

static TelReturn _hal_send(TcoreHal *hal,
	guint data_len, void *data)
{
	CustomData *user_data;
	gint ret;

	if (tcore_hal_get_power_state(hal) == FALSE) {
		err("HAL Power state - OFF");
		return TEL_RETURN_FAILURE;
	}

	user_data = tcore_hal_ref_user_data(hal);
	if (user_data == NULL) {
		err("User data is NULL");
		return TEL_RETURN_FAILURE;
	}

	ret = vdpram_tty_write(user_data->vdpram_fd, data, data_len);
	if(ret < 0) {
		err("Write failed");
		return TEL_RETURN_FAILURE;
	}
	dbg("vdpram_tty_write success ret=%d (fd=%d, len=%d)",
		ret, user_data->vdpram_fd, data_len);

	return TEL_RETURN_SUCCESS;
}

static TelReturn _hal_setup_netif(CoreObject *co,
	TcoreHalSetupNetifCallback func, void *user_data,
	guint cid, gboolean enable)
{
	char ifname[DEVICE_NAME_LEN_MAX];
	int size = 0;
	int fd = 0;
	char buf[32];
	char *control = NULL;

	if (cid > 3) {
		err("Context ID: [%d]", cid);
		return TEL_RETURN_INVALID_PARAMETER;
	}

	if (enable == TRUE) {
		dbg("ACTIVATE - Context ID: [%d]", cid);
		control = "/sys/class/net/svnet0/pdp/activate";
	} else {
		dbg("DEACTIVATE - Context ID: [%d]", cid);
		control = "/sys/class/net/svnet0/pdp/deactivate";
	}

	fd = open(control, O_WRONLY);
	if (fd < 0) {
		err("Failed to Open interface: [%s]", control);

		/* Invoke callback function */
		if (func)
			func(co, -1, NULL, user_data);

		return TEL_RETURN_FAILURE;
	}

	/* Context ID needs to be written to the Device */
	snprintf(buf, sizeof(buf), "%d", cid);
	size = write(fd, buf, strlen(buf));

	/* Close 'fd' */
	close(fd);

	/* Device name */
	snprintf(ifname, DEVICE_NAME_LEN_MAX, "%s%d", DEVICE_NAME_PREFIX, (cid - 1));
	dbg("Interface Name: [%s]", ifname);

	/* Invoke callback function */
	if (func)
		func(co, 0, ifname, user_data);

	return TEL_RETURN_SUCCESS;
}

/* HAL Operations */
static TcoreHalOperations hal_ops = {
	.power = _hal_power,
	.send = _hal_send,
	.setup_netif = _hal_setup_netif,
};

static gboolean on_load()
{
	dbg("Load!!!");

	return TRUE;
}

static gboolean on_init(TcorePlugin *plugin)
{
	TcoreHal *hal;
	CustomData *data;
	dbg("Init!!!");

	tcore_check_return_value_assert(plugin != NULL, FALSE);

	/* Custom data for Modem Interface Plug-in */
	data = tcore_malloc0(sizeof(CustomData));
	dbg("Created custom data memory");

	/* Create Physical HAL */
	hal = tcore_hal_new(plugin, VMODEM_HAL_NAME,
			&hal_ops, TCORE_HAL_MODE_AT);
	if (hal == NULL) {
		err("Failed to Create Physical HAL");
		tcore_free(data);
		return FALSE;
	}
	dbg("HAL [0x%x] created", hal);

	/* Set HAL as Modem Interface Plug-in's User data */
	tcore_plugin_link_user_data(plugin, hal);

	/* Link Custom data to HAL's 'user_data' */
	tcore_hal_link_user_data(hal, data);

	/* Add callbacks for Send/Receive Hooks */
	tcore_hal_add_send_hook(hal, __on_hal_send, NULL);
	tcore_hal_add_recv_callback(hal, __on_hal_recv, NULL);
	dbg("Added Send hook and Receive callback");

	/* Set HAL state to Power OFF (FALSE) */
	(void)tcore_hal_set_power_state(hal, FALSE);
	dbg("HAL Power State: Power OFF");

	/* Resgister to Server */
	if (tcore_server_register_modem(tcore_plugin_ref_server(plugin),
		plugin) == FALSE) {
		err("Registration Failed");

		tcore_hal_free(hal);
		tcore_free(data);
		return FALSE;
	}
	dbg("Registered from Server");

	/* Check CP Power ON */
	g_timeout_add_full(G_PRIORITY_HIGH,
		AT_CP_POWER_ON_TIMEOUT, __power_on, hal, NULL);

	return TRUE;
}

static void on_unload(TcorePlugin *plugin)
{
	TcoreHal *hal;
	CustomData *user_data;
	dbg("Unload!!!");

	tcore_check_return_assert(plugin != NULL);

	/* Unload Modem Plug-in */
	tcore_server_unload_modem_plugin(tcore_plugin_ref_server(plugin), plugin);

	/* Unregister Modem Interface Plug-in from Server */
	tcore_server_unregister_modem(tcore_plugin_ref_server(plugin), plugin);
	dbg("Unregistered from Server");

	/* HAL cleanup */
	hal = tcore_plugin_ref_user_data(plugin);
	if (hal == NULL) {
		err("HAL is NULL");
		return;
	}

	user_data = tcore_hal_ref_user_data(hal);
	if (user_data == NULL)
		return;

	/* Deregister from Watch list */
	__deregister_gio_watch(user_data->vdpram_watch_id);
	dbg("Deregistered Watch ID");

	/* Close VDPRAM device */
	(void)vdpram_close(user_data->vdpram_fd);
	dbg("Closed VDPRAM device");

	/* Free custom data */
	g_free(user_data);

	/* Free HAL */
	tcore_hal_free(hal);
	dbg("Freed HAL");

	dbg("Unloaded MODEM Interface Plug-in");
}

/* VMODEM (Modem Interface Plug-in) descriptor */
EXPORT_API struct tcore_plugin_define_desc plugin_define_desc = {
	.name = "vmodem",
	.priority = TCORE_PLUGIN_PRIORITY_HIGH,
	.version = 1,
	.load = on_load,
	.init = on_init,
	.unload = on_unload
};
