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

#include <glib.h>

#include <tcore.h>
#include <util.h>
#include <log.h>

#include "vdpram_dump.h"

void vdpram_hex_dump(gboolean tx, gushort data_len, void *data)
{
	char *d;

	if(!data)
		return;

	if (tx == TRUE)
		d = "[TX]";
	else
		d = "[RX]";

	msg("\n====== Data DUMP ======\n");

	msg("  %s\tData length: [%d] -", d, data_len);
	tcore_util_hex_dump("        ", (gint)data_len, data);

	msg("\n====== Data DUMP ======\n");
}
