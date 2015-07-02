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
#include <log.h>

#include "vdpram_dump.h"

#define TAB_SPACE	"  "

static void hex_dump(const char *pad, int size, const void *data)
{
	char buf[255] = {0, };
	char hex[4] = {0, };
	int i;
	unsigned const char *p;

	if (size <= 0) {
		msg("%sno data", pad);
		return;
	}

	p = (unsigned const char *)data;

	snprintf(buf, 255, "%s%04X: ", pad, 0);
	for (i = 0; i < size; i++) {
		snprintf(hex, 4, "%02X ", p[i]);
		strncat(buf, hex, strlen(hex));

		if ((i + 1) % 8 == 0) {
			if ((i + 1) % 16 == 0) {
				msg("%s", buf);
				memset(buf, 0, 255);
				snprintf(buf, 255, "%s%04X: ", pad, i + 1);
			} else {
				strncat(buf, TAB_SPACE, strlen(TAB_SPACE));
			}
		}
	}

	msg("%s", buf);
}

void vdpram_hex_dump(int dir, unsigned short data_len, void *data)
{
	const char *d;

	if (!data)
		return;

	if (dir == IPC_RX)
		d = "[RX]";
	else
		d = "[TX]";

	msg("");
	msg("  %s\tlen=%d\t%s", d, data_len, (char *)data);
	hex_dump("        ", data_len, (const void *)data);

	msg("");
}
