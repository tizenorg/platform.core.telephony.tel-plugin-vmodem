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
#include <stdlib.h>
#include <time.h>

#include <glib.h>

#include <tcore.h>
#include <server.h>
#include <plugin.h>
#include <storage.h>
#include <core_object.h>
#include <hal.h>
#include <at.h>

#include "config.h"

#define AT_MODEM_PLUGIN_NAME		"atmodem-plugin.so"

/* Maximum Core objects per Logical HAL (indirectly per Channel) */
#define MAX_CO_PER_CHANNEL		2

/* CP States */
#define AT_CPAS_RESULT_READY		0
#define AT_CPAS_RESULT_UNAVAIL		1
#define AT_CPAS_RESULT_UNKNOWN		2
#define AT_CPAS_RESULT_RINGING		3
#define AT_CPAS_RESULT_CALL_PROGRESS	4
#define AT_CPAS_RESULT_ASLEEP		5

typedef struct {
	guint type;
	gchar *name;
} VmodemSupportedCo;

/*
 * List of supported Core Object types
 */
static VmodemSupportedCo supported_modules[] = {
	{CORE_OBJECT_TYPE_MODEM, "Modem"},
	{CORE_OBJECT_TYPE_NETWORK, "Network"},
	{CORE_OBJECT_TYPE_CALL, "Call"},
	{CORE_OBJECT_TYPE_SIM, "Sim"},
	{CORE_OBJECT_TYPE_SMS, "Sms"},
	{CORE_OBJECT_TYPE_SS, "SS"},
	{CORE_OBJECT_TYPE_PS, "PS"},
	{0, ""},
};

static gboolean __check_cp_poweron(TcoreHal *hal);

static void __assign_objects_to_hal(TcoreHal *hal)
{
	TcorePlugin *plugin;
	gboolean ret;
	guint i = 0;

	plugin = tcore_hal_ref_plugin(hal);

	while (supported_modules[i].type != 0) {
		/* Add Core Object type for specific 'hal' */
		ret = tcore_server_add_cp_mapping_tbl_entry(plugin,
				supported_modules[i].type, hal);
		if (ret == TRUE) {
			dbg("Core Object Type: [0x%x] - Success",
				supported_modules[i].name);
		} else {
			err("Core Object Type: [0x%x] - Fail",
				supported_modules[i].name);
		}

		i++;
	};
}

static void __deassign_objects_from_hal(TcoreHal *hal)
{
	TcorePlugin *plugin;

	plugin = tcore_hal_ref_plugin(hal);

	/* Remove mapping table entry */
	tcore_server_remove_cp_mapping_tbl_entry(plugin, hal);
}

static gboolean __load_modem_plugin(gpointer data)
{
	TcoreHal *hal = (TcoreHal *)data;
	TcorePlugin *plugin;

	dbg("Entry");

	if (hal == NULL) {
		err("hal is NULL");
		return FALSE;
	}

	plugin = tcore_hal_ref_plugin(hal);

	/* Load Modem Plug-in */
	if (tcore_server_load_modem_plugin(tcore_plugin_ref_server(plugin),
			plugin, AT_MODEM_PLUGIN_NAME) != TEL_RETURN_SUCCESS) {
		err("Load Modem Plug-in - [FAIL]");

		/* Clean-up */
		__deassign_objects_from_hal(hal);

		goto EXIT;
	} else {
		dbg("Load Modem Plug-in - [SUCCESS]");
	}

	return TRUE;

EXIT:
	/* TODO: Handle Deregister */

	return FALSE;
}

static void __on_confirmation_send_message(TcorePending *p,
	TelReturn send_status, void *user_data)
{
	dbg("Message send confirmation - [%s]",
		((send_status != TEL_RETURN_SUCCESS) ? "FAIL" : "OK"));
}

static void __on_timeout_check_cp_poweron(TcorePending *p, void *user_data)
{
	TcoreHal *hal = user_data;
	guint data_len = 0;
	char *data = "AT+CPAS";

	data_len = sizeof(data);

	dbg("Resending Command: [%s] Command Length: [%d]", data, data_len);

	/*
	 * Retransmit 1st AT command (AT+CPAS) directly via HAL without disturbing
	 * pending queue.
	 * HAL was passed as user_data, re-using it
	 */
	tcore_hal_send_data(hal, data_len, (void *)data);
}

static void __on_response_check_cp_poweron(TcorePending *pending,
	guint data_len, const void *data, void *user_data)
{
	const TcoreAtResponse *resp = data;
	TcoreHal *hal = user_data;

	GSList *tokens = NULL;
	const char *line;
	gboolean bpoweron = FALSE;
	int response = 0;

	if (resp && resp->success) {
		dbg("Check CP POWER - [OK]");

		/* Parse AT Response */
		if (resp->lines) {
			dbg("Check CP POWER - [OK]");
			line = (const char *) resp->lines->data;
			dbg("line: %s", line);
			tokens = tcore_at_tok_new(line);
			dbg("tokens: %p", tokens);
			if (g_slist_length(tokens) != 1) {
				err("Invalid message");
				goto ERROR;
			}

			dbg("Check CP POWER - [OK]");

			response = atoi(g_slist_nth_data(tokens, 0));
			dbg("CPAS State: [%d]", response);

			switch (response) {
			case AT_CPAS_RESULT_READY:
			case AT_CPAS_RESULT_RINGING:
			case AT_CPAS_RESULT_CALL_PROGRESS:
			case AT_CPAS_RESULT_ASLEEP:
				dbg("CP Power ON!!!");
				bpoweron = TRUE;
			break;

			case AT_CPAS_RESULT_UNAVAIL:
			case AT_CPAS_RESULT_UNKNOWN:
			default:
				err("Value is Unvailable/Unknown - but CP responded - proceed with Power ON!!!");
				bpoweron = TRUE;
			break;
			}
		}
		else {
			err("Check CP POWER - [NOK] - lines NULL");
		}
	} else {
		err("Check CP POWER - [NOK]");
	}

ERROR:
	/* Free tokens */
	tcore_at_tok_free(tokens);

	if (bpoweron == TRUE) {
		dbg("CP Power ON received");

		/* Load Modem Plug-in */
		if(__load_modem_plugin(hal) == FALSE) {
			/* TODO: Handle Deregistration */
		}
		else {
			dbg("Modem Plug-in loaded successfully");
		}
	} else {
		err("CP is not ready, send CPAS again");
		__check_cp_poweron(hal);
	}
}

static gboolean __check_cp_poweron(TcoreHal *hal)
{
	TcoreAtRequest *at_req;
	TcorePending *pending = NULL;

	/* Create Pending request */
	pending = tcore_pending_new(NULL, 0);

	/* Create AT Request */
	at_req = tcore_at_request_new("AT+CPAS",
		"+CPAS:", TCORE_AT_COMMAND_TYPE_SINGLELINE);

	dbg("AT-Command: [%s] Prefix(if any): [%s] Command length: [%d]",
		at_req->cmd, at_req->prefix, strlen(at_req->cmd));

	tcore_pending_set_priority(pending, TCORE_PENDING_PRIORITY_DEFAULT);

	/* Set timeout value and timeout callback */
	tcore_pending_set_timeout(pending, 10);
	tcore_pending_set_timeout_callback(pending,
		__on_timeout_check_cp_poweron, hal);

	/* Set request data and register Response and Send callbacks */
	tcore_pending_set_request_data(pending, 0, at_req);
	tcore_pending_set_response_callback(pending,
		__on_response_check_cp_poweron, hal);
	tcore_pending_set_send_callback(pending,
		__on_confirmation_send_message, NULL);

	/* Send command to CP */
	if (tcore_hal_send_request(hal, pending) != TEL_RETURN_SUCCESS) {
		err("Failed to send CPAS");

		/* Free resource */
		tcore_at_request_free(at_req);
		tcore_pending_free(pending);

		return FALSE;
	}

	dbg("Successfully sent CPAS");
	return TRUE;
}

void vmodem_config_check_cp_power(TcoreHal *hal)
{
	gboolean ret;
	dbg("Entry");

	tcore_check_return(hal != NULL);

	ret = __check_cp_poweron(hal);
	if (ret == TRUE) {
		dbg("Successfully sent check CP Power ON command");

		/* Add Core Objects list to HAL */
		__assign_objects_to_hal(hal);
	} else {
		err("Failed to send check CP Power ON command");
		/* TODO: Handle Deregister */
	}
}
