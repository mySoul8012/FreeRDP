# FreeRDP: A Remote Desktop Protocol Implementation
# FreeRDP cmake build script
#
# Copyright (c) 2015 Rozhuk Ivan <rozhuk.im@gmail.com>
# Copyright (c) 2020 Ingo Feinerer <feinerer@logic.at>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

define_channel_client_subsystem("rdpsnd" "sndio" "")

find_package(SNDIO REQUIRED)

set(${MODULE_PREFIX}_SRCS rdpsnd_sndio.c)

set(${MODULE_PREFIX}_LIBS winpr freerdp ${SNDIO_LIBRARIES})

include_directories(..)
include_directories(SYSTEM ${SNDIO_INCLUDE_DIRS})

add_channel_client_subsystem_library(${MODULE_PREFIX} ${MODULE_NAME} ${CHANNEL_NAME} "" TRUE "")
