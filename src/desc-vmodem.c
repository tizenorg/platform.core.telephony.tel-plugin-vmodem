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

#define DEVICE_NAME_LEN_MAX				16
#define DEVICE_NAME_PREFIX				"pdp"

#define BUF_LEN_MAX						512

#define CORE_OBJECT_NAME_MAX			16

#define MODEM_PLUGIN_NAME				"atmodem-plugin.so"

struct custom_data {
	int vdpram_fd;
	guint watch_id_vdpram;
};

struct v_modules {
	unsigned int co_type;
	char co_name[CORE_OBJECT_NAME_MAX];
};

/* Supported Modules */
static struct v_modules supported_modules[] = {
	{CORE_OBJECT_TYPE_MODEM,	"Modem"},
	{CORE_OBJECT_TYPE_CALL,		"Call"},
	{CORE_OBJECT_TYPE_SS,		"SS"},
	{CORE_OBJECT_TYPE_NETWORK,	"Network"},
	{CORE_OBJECT_TYPE_PS,		"PS"},
	{CORE_OBJECT_TYPE_SIM,		"SIM"},
	{CORE_OBJECT_TYPE_SMS,		"SMS"},
	{0,							""}
};

static void _assign_objects_to_hal(TcoreHal *hal)
{
	TcorePlugin *plugin;
	int i;
	gboolean ret;

	plugin = tcore_hal_ref_plugin(hal);

	/* Add Core Object type for specific 'hal' */
	for (i = 0 ; supported_modules[i].co_type != 0 ; i++) {
		ret = tcore_server_add_cp_mapping_tbl_entry(plugin,
									supported_modules[i].co_type, hal);
		if (ret == TRUE) {
			dbg("[VMODEM] Core Object: [%s] - [Success]",
									supported_modules[i].co_name);
		} else {
			err("[VMODEM] Core Object: [%s] - [Fail]",
									supported_modules[i].co_name);
		}
	}
}

static void _deassign_objects_from_hal(TcoreHal *hal)
{
	TcorePlugin *plugin;

	plugin = tcore_hal_ref_plugin(hal);

	/* Remove mapping table entry */
	tcore_server_remove_cp_mapping_tbl_entry(plugin, hal);
}

static guint _register_gio_watch(TcoreHal *h, int fd, void *callback)
{
	GIOChannel *channel = NULL;
	guint source;

	dbg("[VMODEM] Register to Watch list - fd: [%d]", fd);

	if ((fd < 0) || (callback == NULL))
		return 0;

	channel = g_io_channel_unix_new(fd);
	source = g_io_add_watch(channel, G_IO_IN, (GIOFunc) callback, h);
	g_io_channel_unref(channel);
	channel = NULL;

	return source;
}

static void _deregister_gio_watch(guint watch_id)
{
	dbg("[VMODEM] Deregister Watch ID: [%d]", watch_id);

	/* Remove source */
	g_source_remove(watch_id);
}

static gboolean _load_modem_plugin(gpointer data)
{
	TcoreHal *hal;
	TcorePlugin *plugin;
	struct custom_data *user_data;

	dbg("[VMMODEM] Entry");

	if (data == NULL) {
		err("[VMMODEM] data is NULL");
		return FALSE;
	}

	hal = data;
	plugin = tcore_hal_ref_plugin(hal);

	/* Load Modem Plug-in */
	if (tcore_server_load_modem_plugin(tcore_plugin_ref_server(plugin),
					plugin, MODEM_PLUGIN_NAME) == TCORE_RETURN_FAILURE) {
		err("[VMMODEM] Load Modem Plug-in - [FAIL]");

		/* Clean-up */
		_deassign_objects_from_hal(hal);

		goto EXIT;
	} else {
		dbg("[VMMODEM] Load Modem Plug-in - [SUCCESS]");
	}

	/* To stop the cycle need to return FALSE */
	return FALSE;

EXIT:
	user_data = tcore_hal_ref_user_data(hal);
	if (user_data == NULL)
		return FALSE;

	/* Deregister from Watch list */
	_deregister_gio_watch(user_data->watch_id_vdpram);

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
		err("[VMODEM] User data is NULL");
		return TCORE_RETURN_FAILURE;
	}

	if (enable == TRUE) {							/* POWER ON */
		if (FALSE == vdpram_poweron(user_data->vdpram_fd)) {
			err("[VMODEM] Power ON - [FAIL]");
			return TCORE_RETURN_FAILURE;
		}

		/* Set Power State - ON */
		tcore_hal_set_power_state(hal, TRUE);
	} else {										/* POWER OFF */
		if (vdpram_poweroff(user_data->vdpram_fd) == FALSE) {
			err("[VMODEM] Power OFF - [FAIL]");
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

	custom = tcore_hal_ref_user_data(hal);
	memset(buf, 0x0, BUF_LEN_MAX);

	/* Read from Device */
	n = vdpram_tty_read(custom->vdpram_fd, buf, BUF_LEN_MAX);
	if (n < 0) {
		err("[VMODEM] Read error - Data received: [%d]", n);
		return TRUE;
	}
	dbg("[VMODEM] DPRAM Receive - Data length: [%d]", n);

	/* Emit receive callback */
	tcore_hal_emit_recv_callback(hal, n, buf);

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
		err("[VMODEM] HAL Power state - OFF");
		return TCORE_RETURN_FAILURE;
	}

	user_data = tcore_hal_ref_user_data(hal);
	if (user_data == NULL) {
		err("[VMODEM] User data is NULL");
		return TCORE_RETURN_FAILURE;
	}

	ret = vdpram_tty_write(user_data->vdpram_fd, data, data_len);
	if(ret < 0)	{
		err("[VMODEM] Write failed");
		return TCORE_RETURN_FAILURE;
	}
	else {
		dbg("vdpram_tty_write success ret=%d (fd=%d, len=%d)", ret, user_data->vdpram_fd, data_len);
		return TCORE_RETURN_SUCCESS;
	}
}

static TReturn hal_setup_netif(CoreObject *co,
				TcoreHalSetupNetifCallback func,
				void *user_data, unsigned int cid,
				gboolean enable)
{
	char ifname[DEVICE_NAME_LEN_MAX];
	int size = 0;
	int fd = 0;
	char buf[32];
	char *control = NULL;

	if (cid > 3) {
		err("[VMODEM] Context ID: [%d]", cid);
		return TCORE_RETURN_EINVAL;
	}

	if (enable == TRUE) {
		dbg("[VMODEM] ACTIVATE - Context ID: [%d]", cid);
		control = "/sys/class/net/svnet0/pdp/activate";
	} else {
		dbg("[VMODEM] DEACTIVATE - Context ID: [%d]", cid);
		control = "/sys/class/net/svnet0/pdp/deactivate";
	}

	fd = open(control, O_WRONLY);
	if (fd < 0) {
		err("[VMODEM] Failed to Open interface: [%s]", control);

		/* Invoke callback function */
		if (func)
			func(co, -1, NULL, user_data);

		return TCORE_RETURN_FAILURE;
	}

	/* Context ID needs to be written to the Device */
	snprintf(buf, sizeof(buf), "%d", cid);
	size = write(fd, buf, strlen(buf));

	/* Close 'fd' */
	close(fd);

	/* Device name */
	snprintf(ifname, DEVICE_NAME_LEN_MAX, "%s%d", DEVICE_NAME_PREFIX, (cid - 1));
	dbg("[VMODEM] Interface Name: [%s]", ifname);

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
	dbg("[VMODEM] Load!!!");

	return TRUE;
}

static gboolean on_init(TcorePlugin *plugin)
{
	TcoreHal *hal;
	struct custom_data *data;

	dbg("[VMODEM] Init!!!");

	if (plugin == NULL) {
		err("[VMODEM] PLug-in is NULL");
		return FALSE;
	}

	/* Register Modem Interface Plug-in */
	if (tcore_server_register_modem(tcore_plugin_ref_server(plugin), plugin)
								== FALSE) {
		err("[VMODEM] Registration Failed");
		return FALSE;
	}
	dbg("[VMODEM] Registered from Server");

	data = g_try_new0(struct custom_data, 1);
	if (data == NULL) {
		err("[VMODEM] Failed to allocate memory for Custom data");

		/* Unregister from Server */
		tcore_server_unregister_modem(tcore_plugin_ref_server(plugin), plugin);
		return FALSE;
	}

	/*
	 * Open DPRAM device
	 */
	data->vdpram_fd = vdpram_open();

	/*
	 * Create and initialize HAL
	 */
	hal = tcore_hal_new(plugin, "vmodem", &hal_ops, TCORE_HAL_MODE_CUSTOM);
	if (hal == NULL) {
		/* Close VDPRAM device */
		vdpram_close(data->vdpram_fd);

		/* Fre custom data */
		g_free(data);

		/* Unregister from Server */
		tcore_server_unregister_modem(tcore_plugin_ref_server(plugin), plugin);

		return FALSE;
	}

	/* Set HAL as Modem Interface Plug-in's User data */
	tcore_plugin_link_user_data(plugin, hal);

	/* Link custom data to HAL user data */
	tcore_hal_link_user_data(hal, data);

	/* Register to Watch llist */
	data->watch_id_vdpram = _register_gio_watch(hal,
								data->vdpram_fd, on_recv_vdpram_message);
	dbg("[VMODEM] fd: [%d] Watch ID: [%d]",
								data->vdpram_fd, data->watch_id_vdpram);

	/* Power ON VDPRAM device */
	if (_modem_power(hal, TRUE) == TCORE_RETURN_SUCCESS) {
		dbg("[VMODEM] Power ON - [SUCCESS]");
	} else {
		err("[VMODEM] Power ON - [FAIL]");
		goto EXIT;
	}

	/* Add Core Objects list to HAL */
	_assign_objects_to_hal(hal);

	/* Check CP Power ON */
	g_timeout_add_full(G_PRIORITY_HIGH, SERVER_INIT_WAIT_TIMEOUT, _load_modem_plugin, hal, 0);

	dbg("[VMMODEM] Exit");
	return TRUE;

EXIT:
	/* Deregister from Watch list */
	_deregister_gio_watch(data->watch_id_vdpram);

	/* Free HAL */
	tcore_hal_free(hal);

	/* Close VDPRAM device */
	vdpram_close(data->vdpram_fd);

	/* Free custom data */
	g_free(data);

	/* Unregister from Server */
	tcore_server_unregister_modem(tcore_plugin_ref_server(plugin), plugin);

	return FALSE;
}

static void on_unload(TcorePlugin *plugin)
{
	TcoreHal *hal;
	struct custom_data *user_data;

	dbg("[VMODEM] Unload!!!");

	if (plugin == NULL)
		return;

	hal = tcore_plugin_ref_user_data(plugin);
	if (hal == NULL)
		return;

	/* Unload Modem Plug-in */
#if 0	/* TODO - Open the code below */
	tcore_server_unload_modem_plugin(tcore_plugin_ref_server(plugin), plugin);
#endif
	user_data = tcore_hal_ref_user_data(hal);
	if (user_data == NULL)
		return;

	/* Deregister from Watch list */
	_deregister_gio_watch(user_data->watch_id_vdpram);
	dbg("[VMODEM] Deregistered Watch ID");

	/* Free HAL */
	tcore_hal_free(hal);
	dbg("[VMODEM] Freed HAL");

	/* Close VDPRAM device */
	vdpram_close(user_data->vdpram_fd);
	dbg("[VMODEM] Closed VDPRAM device");

	/* Free custom data */
	g_free(user_data);

	tcore_server_unregister_modem(tcore_plugin_ref_server(plugin), plugin);
	dbg("[VMODEM] Unregistered from Server");

	dbg("[VMODEM] Unloaded MODEM");
}

/* VMODEM Descriptor Structure */
struct tcore_plugin_define_desc plugin_define_desc = {
	.name = "VMODEM",
	.priority = TCORE_PLUGIN_PRIORITY_HIGH,
	.version = 1,
	.load = on_load,
	.init = on_init,
	.unload = on_unload
};
