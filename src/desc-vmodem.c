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

#include <glib.h>

#include <tcore.h>
#include <plugin.h>

#include <tcore.h>
#include <server.h>
#include <plugin.h>
#include <user_request.h>
#include <hal.h>

#include "vdpram.h"

struct custom_data {
	int vdpram_fd;
	guint watch_id_vdpram;
};

static TReturn hal_power(TcoreHal *hal, gboolean flag)
{
	struct custom_data *user_data;

	user_data = tcore_hal_ref_user_data(hal);
	if (!user_data)
		return TCORE_RETURN_FAILURE;

	/* power on */
	if (flag == TRUE) {
		if (FALSE == vdpram_poweron(user_data->vdpram_fd)) {
			err("vdpram_poweron failed");
			return TCORE_RETURN_FAILURE;
		}
		tcore_hal_set_power_state(hal, TRUE);
	}
	/* power off */
	else {
		if (FALSE == vdpram_poweroff(user_data->vdpram_fd)) {
			err("vdpram_poweroff failed");
			return TCORE_RETURN_FAILURE;
		}
		tcore_hal_set_power_state(hal, FALSE);
	}

	return TCORE_RETURN_SUCCESS;
}


static TReturn hal_send(TcoreHal *hal, unsigned int data_len, void *data)
{
	int ret;
	struct custom_data *user_data;

	if (tcore_hal_get_power_state(hal) == FALSE)
		return TCORE_RETURN_FAILURE;

	user_data = tcore_hal_ref_user_data(hal);
	if (!user_data)
		return TCORE_RETURN_FAILURE;

	ret = vdpram_tty_write(user_data->vdpram_fd, data, data_len);
	if(ret < 0)	{
		err("vdpram_tty_write failed");
		return TCORE_RETURN_FAILURE;
	}
	else {
		dbg("vdpram_tty_write success ret=%d (fd=%d, len=%d)", ret, user_data->vdpram_fd, data_len);
		return TCORE_RETURN_SUCCESS;
	}
}


static struct tcore_hal_operations hops =
{
	.power = hal_power,
	.send = hal_send,
};

static gboolean on_recv_vdpram_message(GIOChannel *channel, GIOCondition condition, gpointer data)
{
	TcorePlugin *plugin = data;
	TcoreHal *hal;
	struct custom_data *custom;

	#define BUF_LEN_MAX 512
	char buf[BUF_LEN_MAX];
	int n = 0;

	hal = tcore_plugin_ref_hal(plugin);
	custom = tcore_hal_ref_user_data(hal);
	memset(buf, 0, BUF_LEN_MAX);
	n = vdpram_tty_read(custom->vdpram_fd, buf, BUF_LEN_MAX);
	if (n < 0) {
		err("tty_read error. return_valute = %d", n);
		return TRUE;
	}

	dbg("vdpram recv (ret = %d)", n);
	tcore_hal_emit_recv_callback(hal, n, buf);

	return TRUE;
}

static guint register_gio_watch(TcorePlugin *plugin, int fd, void *callback)
{
	GIOChannel *channel = NULL;
	guint source;

	if (fd < 0 || !callback)
		return 0;

	channel = g_io_channel_unix_new(fd);
	source = g_io_add_watch(channel, G_IO_IN, (GIOFunc) callback, plugin);
	g_io_channel_unref(channel);
	channel = NULL;

	return source;
}


/*static int power_tx_pwr_on_exec(int nFd)
{
	 Not implement yet
	return 0;
}*/

static gboolean on_load()
{
	dbg("i'm load!");

	return TRUE;
}

static gboolean on_init(TcorePlugin *plugin)
{
	TcoreHal *hal;
	struct custom_data *data;

	if (!plugin)
		return FALSE;

	dbg("i'm init!");

	/*
	 * Phonet init
	 */
	data = calloc(sizeof(struct custom_data), 1);
	memset(data, 0, sizeof(struct custom_data));

	data->vdpram_fd = vdpram_open();

	/*
	 * HAL init
	 */
	hal = tcore_hal_new(plugin, "vmodem", &hops);
	tcore_hal_link_user_data(hal, data);

	data->watch_id_vdpram= register_gio_watch(plugin, data->vdpram_fd, on_recv_vdpram_message);

	dbg("vdpram_fd = %d, watch_id_vdpram=%d ", data->vdpram_fd, data->watch_id_vdpram);

	if (!vdpram_poweron(data->vdpram_fd))
		err("vdpram_poweron Failed");

//	power_tx_pwr_on_exec(data->vdpram_fd);

	return TRUE;
}

static void on_unload(TcorePlugin *plugin)
{
	if (!plugin)
		return;

	dbg("i'm unload");
}

struct tcore_plugin_define_desc plugin_define_desc =
{
	.name = "VMODEM",
	.priority = TCORE_PLUGIN_PRIORITY_HIGH,
	.version = 1,
	.load = on_load,
	.init = on_init,
	.unload = on_unload
};
