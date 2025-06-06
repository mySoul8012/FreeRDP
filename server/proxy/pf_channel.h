/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 *
 *
 * Copyright 2022 David Fort <contact@hardening-consulting.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef SERVER_PROXY_PF_CHANNEL_H_
#define SERVER_PROXY_PF_CHANNEL_H_

#include <freerdp/server/proxy/proxy_context.h>

/** @brief operating mode of a channel tracker */
typedef enum
{
	CHANNEL_TRACKER_PEEK, /*!< inquiring content, accumulating packet fragments */
	CHANNEL_TRACKER_PASS, /*!< pass all the fragments of the current packet */
	CHANNEL_TRACKER_DROP  /*!< drop all the fragments of the current packet */
} ChannelTrackerMode;

typedef struct sChannelStateTracker ChannelStateTracker;
typedef PfChannelResult (*ChannelTrackerPeekFn)(ChannelStateTracker* tracker, BOOL first,
                                                BOOL lastPacket);

void channelTracker_free(ChannelStateTracker* t);

WINPR_ATTR_MALLOC(channelTracker_free, 1)
ChannelStateTracker* channelTracker_new(pServerStaticChannelContext* channel,
                                        ChannelTrackerPeekFn fn, void* data);

BOOL channelTracker_setMode(ChannelStateTracker* tracker, ChannelTrackerMode mode);
ChannelTrackerMode channelTracker_getMode(ChannelStateTracker* tracker);

BOOL channelTracker_setPData(ChannelStateTracker* tracker, proxyData* pdata);
proxyData* channelTracker_getPData(ChannelStateTracker* tracker);

BOOL channelTracker_setCustomData(ChannelStateTracker* tracker, void* data);
void* channelTracker_getCustomData(ChannelStateTracker* tracker);

wStream* channelTracker_getCurrentPacket(ChannelStateTracker* tracker);

size_t channelTracker_getCurrentPacketSize(ChannelStateTracker* tracker);
BOOL channelTracker_setCurrentPacketSize(ChannelStateTracker* tracker, size_t size);

PfChannelResult channelTracker_update(ChannelStateTracker* tracker, const BYTE* xdata, size_t xsize,
                                      UINT32 flags, size_t totalSize);

PfChannelResult channelTracker_flushCurrent(ChannelStateTracker* t, BOOL first, BOOL last,
                                            BOOL toBack);

BOOL pf_channel_setup_generic(pServerStaticChannelContext* channel);

#endif /* SERVER_PROXY_PF_CHANNEL_H_ */
