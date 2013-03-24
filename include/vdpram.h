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

#ifndef __VDPRAM_H__
#define __VDPRAM_H__

int vdpram_close(int fd);
int vdpram_open (void);
gboolean vdpram_poweron(int fd);
gboolean vdpram_poweroff(int fd);

int vdpram_tty_read(int nFd, void* buf, size_t nbytes);
int vdpram_tty_write(int nFd, void* buf, size_t nbytes);

#endif
