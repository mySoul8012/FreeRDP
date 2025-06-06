/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Video Redirection Virtual Channel - FFmpeg Decoder
 *
 * Copyright 2010-2011 Vic Lee
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

#include <freerdp/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winpr/crt.h>

#include <freerdp/channels/log.h>
#include <freerdp/client/tsmf.h>

#include <libavcodec/avcodec.h>
#include <libavutil/common.h>
#include <libavutil/cpu.h>
#include <libavutil/imgutils.h>

#include "tsmf_constants.h"
#include "tsmf_decoder.h"
#include "tsmf_audio.h"

/* Compatibility with older FFmpeg */
#if LIBAVUTIL_VERSION_MAJOR < 50
#define AVMEDIA_TYPE_VIDEO 0
#define AVMEDIA_TYPE_AUDIO 1
#endif

#if LIBAVCODEC_VERSION_MAJOR < 54
#define MAX_AUDIO_FRAME_SIZE AVCODEC_MAX_AUDIO_FRAME_SIZE
#else
#define MAX_AUDIO_FRAME_SIZE 192000
#endif

#if LIBAVCODEC_VERSION_MAJOR < 55
#define AV_CODEC_ID_VC1 CODEC_ID_VC1
#define AV_CODEC_ID_WMAV2 CODEC_ID_WMAV2
#define AV_CODEC_ID_WMAPRO CODEC_ID_WMAPRO
#define AV_CODEC_ID_MP3 CODEC_ID_MP3
#define AV_CODEC_ID_MP2 CODEC_ID_MP2
#define AV_CODEC_ID_MPEG2VIDEO CODEC_ID_MPEG2VIDEO
#define AV_CODEC_ID_WMV3 CODEC_ID_WMV3
#define AV_CODEC_ID_AAC CODEC_ID_AAC
#define AV_CODEC_ID_H264 CODEC_ID_H264
#define AV_CODEC_ID_AC3 CODEC_ID_AC3
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(56, 34, 2)
#define AV_CODEC_CAP_TRUNCATED CODEC_CAP_TRUNCATED
#define AV_CODEC_FLAG_TRUNCATED CODEC_FLAG_TRUNCATED
#endif

#if LIBAVUTIL_VERSION_MAJOR < 52
#define AV_PIX_FMT_YUV420P PIX_FMT_YUV420P
#endif

typedef struct
{
	ITSMFDecoder iface;

	int media_type;
#if LIBAVCODEC_VERSION_MAJOR < 55
	enum CodecID codec_id;
#else
	enum AVCodecID codec_id;
#endif
	AVCodecContext* codec_context;
	const AVCodec* codec;
	AVFrame* frame;
	int prepared;

	BYTE* decoded_data;
	UINT32 decoded_size;
	UINT32 decoded_size_max;
} TSMFFFmpegDecoder;

static BOOL tsmf_ffmpeg_init_context(ITSMFDecoder* decoder)
{
	TSMFFFmpegDecoder* mdecoder = (TSMFFFmpegDecoder*)decoder;
	mdecoder->codec_context = avcodec_alloc_context3(NULL);

	if (!mdecoder->codec_context)
	{
		WLog_ERR(TAG, "avcodec_alloc_context failed.");
		return FALSE;
	}

	return TRUE;
}

static BOOL tsmf_ffmpeg_init_video_stream(ITSMFDecoder* decoder, const TS_AM_MEDIA_TYPE* media_type)
{
	TSMFFFmpegDecoder* mdecoder = (TSMFFFmpegDecoder*)decoder;
	mdecoder->codec_context->width = WINPR_ASSERTING_INT_CAST(int, media_type->Width);
	mdecoder->codec_context->height = WINPR_ASSERTING_INT_CAST(int, media_type->Height);
	mdecoder->codec_context->bit_rate = WINPR_ASSERTING_INT_CAST(int, media_type->BitRate);
	mdecoder->codec_context->time_base.den =
	    WINPR_ASSERTING_INT_CAST(int, media_type->SamplesPerSecond.Numerator);
	mdecoder->codec_context->time_base.num =
	    WINPR_ASSERTING_INT_CAST(int, media_type->SamplesPerSecond.Denominator);
#if LIBAVCODEC_VERSION_MAJOR < 55
	mdecoder->frame = avcodec_alloc_frame();
#else
	mdecoder->frame = av_frame_alloc();
#endif
	return TRUE;
}

static BOOL tsmf_ffmpeg_init_audio_stream(ITSMFDecoder* decoder, const TS_AM_MEDIA_TYPE* media_type)
{
	TSMFFFmpegDecoder* mdecoder = (TSMFFFmpegDecoder*)decoder;
	mdecoder->codec_context->sample_rate =
	    WINPR_ASSERTING_INT_CAST(int, media_type->SamplesPerSecond.Numerator);
	mdecoder->codec_context->bit_rate = WINPR_ASSERTING_INT_CAST(int, media_type->BitRate);
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
	mdecoder->codec_context->ch_layout.nb_channels =
	    WINPR_ASSERTING_INT_CAST(int, media_type->Channels);
#else
	mdecoder->codec_context->channels = WINPR_ASSERTING_INT_CAST(int, media_type->Channels);
#endif
	mdecoder->codec_context->block_align = WINPR_ASSERTING_INT_CAST(int, media_type->BlockAlign);
#if LIBAVCODEC_VERSION_MAJOR < 55
#ifdef AV_CPU_FLAG_SSE2
	mdecoder->codec_context->dsp_mask = AV_CPU_FLAG_SSE2 | AV_CPU_FLAG_MMX2;
#else
#if LIBAVCODEC_VERSION_MAJOR < 53
	mdecoder->codec_context->dsp_mask = FF_MM_SSE2 | FF_MM_MMXEXT;
#else
	mdecoder->codec_context->dsp_mask = FF_MM_SSE2 | FF_MM_MMX2;
#endif
#endif
#else /* LIBAVCODEC_VERSION_MAJOR < 55 */
#ifdef AV_CPU_FLAG_SSE2
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(57, 17, 100)
	av_set_cpu_flags_mask(AV_CPU_FLAG_SSE2 | AV_CPU_FLAG_MMXEXT);
#else
	av_force_cpu_flags(AV_CPU_FLAG_SSE2 | AV_CPU_FLAG_MMXEXT);
#endif
#else
	av_set_cpu_flags_mask(FF_MM_SSE2 | FF_MM_MMX2);
#endif
#endif /* LIBAVCODEC_VERSION_MAJOR < 55 */
	return TRUE;
}

static BOOL tsmf_ffmpeg_init_stream(ITSMFDecoder* decoder, const TS_AM_MEDIA_TYPE* media_type)
{
	BYTE* p = NULL;
	UINT32 size = 0;
	const BYTE* s = NULL;
	TSMFFFmpegDecoder* mdecoder = (TSMFFFmpegDecoder*)decoder;

	WINPR_PRAGMA_DIAG_PUSH
	WINPR_PRAGMA_DIAG_IGNORED_QUALIFIERS
	mdecoder->codec = avcodec_find_decoder(mdecoder->codec_id);
	WINPR_PRAGMA_DIAG_POP

	if (!mdecoder->codec)
	{
		WLog_ERR(TAG, "avcodec_find_decoder failed.");
		return FALSE;
	}

	mdecoder->codec_context->codec_id = mdecoder->codec_id;
	mdecoder->codec_context->codec_type = mdecoder->media_type;

	switch (mdecoder->media_type)
	{
		case AVMEDIA_TYPE_VIDEO:
			if (!tsmf_ffmpeg_init_video_stream(decoder, media_type))
				return FALSE;

			break;

		case AVMEDIA_TYPE_AUDIO:
			if (!tsmf_ffmpeg_init_audio_stream(decoder, media_type))
				return FALSE;

			break;

		default:
			WLog_ERR(TAG, "unknown media_type %d", mdecoder->media_type);
			break;
	}

	if (media_type->ExtraData)
	{
		/* Add a padding to avoid invalid memory read in some codec */
		mdecoder->codec_context->extradata_size =
		    WINPR_ASSERTING_INT_CAST(int, media_type->ExtraDataSize + 8);
		mdecoder->codec_context->extradata = calloc(1, mdecoder->codec_context->extradata_size);

		if (!mdecoder->codec_context->extradata)
			return FALSE;

		if (media_type->SubType == TSMF_SUB_TYPE_AVC1 &&
		    media_type->FormatType == TSMF_FORMAT_TYPE_MPEG2VIDEOINFO)
		{
			size_t required = 6;
			/* The extradata format that FFmpeg uses is following CodecPrivate in Matroska.
			   See http://haali.su/mkv/codecs.pdf */
			p = mdecoder->codec_context->extradata;
			if ((mdecoder->codec_context->extradata_size < 0) ||
			    ((size_t)mdecoder->codec_context->extradata_size < required))
				return FALSE;
			*p++ = 1;                         /* Reserved? */
			*p++ = media_type->ExtraData[8];  /* Profile */
			*p++ = 0;                         /* Profile */
			*p++ = media_type->ExtraData[12]; /* Level */
			*p++ = 0xff;                      /* Flag? */
			*p++ = 0xe0 | 0x01;               /* Reserved | #sps */
			s = media_type->ExtraData + 20;
			size = ((UINT32)(*s)) * 256 + ((UINT32)(*(s + 1)));
			required += size + 2;
			if ((mdecoder->codec_context->extradata_size < 0) ||
			    ((size_t)mdecoder->codec_context->extradata_size < required))
				return FALSE;
			memcpy(p, s, size + 2);
			s += size + 2;
			p += size + 2;
			required++;
			if ((mdecoder->codec_context->extradata_size < 0) ||
			    ((size_t)mdecoder->codec_context->extradata_size < required))
				return FALSE;
			*p++ = 1; /* #pps */
			size = ((UINT32)(*s)) * 256 + ((UINT32)(*(s + 1)));
			required += size + 2;
			if ((mdecoder->codec_context->extradata_size < 0) ||
			    ((size_t)mdecoder->codec_context->extradata_size < required))
				return FALSE;
			memcpy(p, s, size + 2);
		}
		else
		{
			memcpy(mdecoder->codec_context->extradata, media_type->ExtraData,
			       media_type->ExtraDataSize);
			if ((mdecoder->codec_context->extradata_size < 0) ||
			    ((size_t)mdecoder->codec_context->extradata_size <
			     media_type->ExtraDataSize + 8ull))
				return FALSE;
			memset(mdecoder->codec_context->extradata + media_type->ExtraDataSize, 0, 8);
		}
	}

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(59, 18, 100)
	if (mdecoder->codec->capabilities & AV_CODEC_CAP_TRUNCATED)
		mdecoder->codec_context->flags |= AV_CODEC_FLAG_TRUNCATED;
#endif

	return TRUE;
}

static BOOL tsmf_ffmpeg_prepare(ITSMFDecoder* decoder)
{
	TSMFFFmpegDecoder* mdecoder = (TSMFFFmpegDecoder*)decoder;

	if (avcodec_open2(mdecoder->codec_context, mdecoder->codec, NULL) < 0)
	{
		WLog_ERR(TAG, "avcodec_open2 failed.");
		return FALSE;
	}

	mdecoder->prepared = 1;
	return TRUE;
}

static BOOL tsmf_ffmpeg_set_format(ITSMFDecoder* decoder, TS_AM_MEDIA_TYPE* media_type)
{
	TSMFFFmpegDecoder* mdecoder = (TSMFFFmpegDecoder*)decoder;

	WINPR_ASSERT(mdecoder);
	WINPR_ASSERT(media_type);

	switch (media_type->MajorType)
	{
		case TSMF_MAJOR_TYPE_VIDEO:
			mdecoder->media_type = AVMEDIA_TYPE_VIDEO;
			break;

		case TSMF_MAJOR_TYPE_AUDIO:
			mdecoder->media_type = AVMEDIA_TYPE_AUDIO;
			break;

		default:
			return FALSE;
	}

	switch (media_type->SubType)
	{
		case TSMF_SUB_TYPE_WVC1:
			mdecoder->codec_id = AV_CODEC_ID_VC1;
			break;

		case TSMF_SUB_TYPE_WMA2:
			mdecoder->codec_id = AV_CODEC_ID_WMAV2;
			break;

		case TSMF_SUB_TYPE_WMA9:
			mdecoder->codec_id = AV_CODEC_ID_WMAPRO;
			break;

		case TSMF_SUB_TYPE_MP3:
			mdecoder->codec_id = AV_CODEC_ID_MP3;
			break;

		case TSMF_SUB_TYPE_MP2A:
			mdecoder->codec_id = AV_CODEC_ID_MP2;
			break;

		case TSMF_SUB_TYPE_MP2V:
			mdecoder->codec_id = AV_CODEC_ID_MPEG2VIDEO;
			break;

		case TSMF_SUB_TYPE_WMV3:
			mdecoder->codec_id = AV_CODEC_ID_WMV3;
			break;

		case TSMF_SUB_TYPE_AAC:
			mdecoder->codec_id = AV_CODEC_ID_AAC;

			/* For AAC the pFormat is a HEAACWAVEINFO struct, and the codec data
			   is at the end of it. See
			   http://msdn.microsoft.com/en-us/library/dd757806.aspx */
			if (media_type->ExtraData)
			{
				if (media_type->ExtraDataSize < 12)
					return FALSE;

				media_type->ExtraData += 12;
				media_type->ExtraDataSize -= 12;
			}

			break;

		case TSMF_SUB_TYPE_H264:
		case TSMF_SUB_TYPE_AVC1:
			mdecoder->codec_id = AV_CODEC_ID_H264;
			break;

		case TSMF_SUB_TYPE_AC3:
			mdecoder->codec_id = AV_CODEC_ID_AC3;
			break;

		default:
			return FALSE;
	}

	if (!tsmf_ffmpeg_init_context(decoder))
		return FALSE;

	if (!tsmf_ffmpeg_init_stream(decoder, media_type))
		return FALSE;

	if (!tsmf_ffmpeg_prepare(decoder))
		return FALSE;

	return TRUE;
}

static BOOL tsmf_ffmpeg_decode_video(ITSMFDecoder* decoder, const BYTE* data, UINT32 data_size,
                                     UINT32 extensions)
{
	TSMFFFmpegDecoder* mdecoder = (TSMFFFmpegDecoder*)decoder;
	int decoded = 0;
	int len = 0;
	AVFrame* frame = NULL;
	BOOL ret = TRUE;
#if LIBAVCODEC_VERSION_MAJOR < 52 || \
    (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR <= 20)
	len = avcodec_decode_video(mdecoder->codec_context, mdecoder->frame, &decoded, data, data_size);
#else
	{
		AVPacket pkt = { 0 };
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 133, 100)
		av_init_packet(&pkt);
#endif
		pkt.data = WINPR_CAST_CONST_PTR_AWAY(data, BYTE*);
		pkt.size = WINPR_ASSERTING_INT_CAST(int, data_size);

		if (extensions & TSMM_SAMPLE_EXT_CLEANPOINT)
			pkt.flags |= AV_PKT_FLAG_KEY;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 48, 101)
		len = avcodec_decode_video2(mdecoder->codec_context, mdecoder->frame, &decoded, &pkt);
#else
		len = avcodec_send_packet(mdecoder->codec_context, &pkt);
		if (len > 0)
		{
			len = avcodec_receive_frame(mdecoder->codec_context, mdecoder->frame);
			if (len == AVERROR(EAGAIN))
				return TRUE;
		}
#endif
	}
#endif

	if (len < 0)
	{
		WLog_ERR(TAG, "data_size %" PRIu32 ", avcodec_decode_video failed (%d)", data_size, len);
		ret = FALSE;
	}
	else if (!decoded)
	{
		WLog_ERR(TAG, "data_size %" PRIu32 ", no frame is decoded.", data_size);
		ret = FALSE;
	}
	else
	{
		DEBUG_TSMF("linesize[0] %d linesize[1] %d linesize[2] %d linesize[3] %d "
		           "pix_fmt %d width %d height %d",
		           mdecoder->frame->linesize[0], mdecoder->frame->linesize[1],
		           mdecoder->frame->linesize[2], mdecoder->frame->linesize[3],
		           mdecoder->codec_context->pix_fmt, mdecoder->codec_context->width,
		           mdecoder->codec_context->height);
		mdecoder->decoded_size = av_image_get_buffer_size(mdecoder->codec_context->pix_fmt,
		                                                  mdecoder->codec_context->width,
		                                                  mdecoder->codec_context->height, 1);
		mdecoder->decoded_data = calloc(1, mdecoder->decoded_size);

		if (!mdecoder->decoded_data)
			return FALSE;

#if LIBAVCODEC_VERSION_MAJOR < 55
		frame = avcodec_alloc_frame();
#else
		frame = av_frame_alloc();
#endif
		av_image_fill_arrays(frame->data, frame->linesize, mdecoder->decoded_data,
		                     mdecoder->codec_context->pix_fmt, mdecoder->codec_context->width,
		                     mdecoder->codec_context->height, 1);

		const uint8_t* ptr[AV_NUM_DATA_POINTERS] = { 0 };
		for (size_t x = 0; x < AV_NUM_DATA_POINTERS; x++)
			ptr[x] = mdecoder->frame->data[x];

		av_image_copy(frame->data, frame->linesize, ptr, mdecoder->frame->linesize,
		              mdecoder->codec_context->pix_fmt, mdecoder->codec_context->width,
		              mdecoder->codec_context->height);
		av_free(frame);
	}

	return ret;
}

static BOOL tsmf_ffmpeg_decode_audio(ITSMFDecoder* decoder, const BYTE* data, UINT32 data_size,
                                     UINT32 extensions)
{
	TSMFFFmpegDecoder* mdecoder = (TSMFFFmpegDecoder*)decoder;
	int len = 0;
	int frame_size = 0;

	if (mdecoder->decoded_size_max == 0)
		mdecoder->decoded_size_max = MAX_AUDIO_FRAME_SIZE + 16;

	mdecoder->decoded_data = calloc(1, mdecoder->decoded_size_max);

	if (!mdecoder->decoded_data)
		return FALSE;

	/* align the memory for SSE2 needs */
	BYTE* dst = (BYTE*)(((uintptr_t)mdecoder->decoded_data + 15) & ~0x0F);
	size_t dst_offset = (size_t)(dst - mdecoder->decoded_data);
	const BYTE* src = data;
	UINT32 src_size = data_size;

	while (src_size > 0)
	{
		/* Ensure enough space for decoding */
		if (mdecoder->decoded_size_max - mdecoder->decoded_size < MAX_AUDIO_FRAME_SIZE)
		{
			BYTE* tmp_data = NULL;
			tmp_data = realloc(mdecoder->decoded_data, mdecoder->decoded_size_max * 2 + 16);

			if (!tmp_data)
				return FALSE;

			mdecoder->decoded_size_max = mdecoder->decoded_size_max * 2 + 16;
			mdecoder->decoded_data = tmp_data;
			dst = (BYTE*)(((uintptr_t)mdecoder->decoded_data + 15) & ~0x0F);

			const size_t diff = (size_t)(dst - mdecoder->decoded_data);
			if (diff != dst_offset)
			{
				/* re-align the memory if the alignment has changed after realloc */
				memmove(dst, mdecoder->decoded_data + dst_offset, mdecoder->decoded_size);
				dst_offset = diff;
			}

			dst += mdecoder->decoded_size;
		}

#if LIBAVCODEC_VERSION_MAJOR < 52 || \
    (LIBAVCODEC_VERSION_MAJOR == 52 && LIBAVCODEC_VERSION_MINOR <= 20)
		frame_size = mdecoder->decoded_size_max - mdecoder->decoded_size;
		len = avcodec_decode_audio2(mdecoder->codec_context, (int16_t*)dst, &frame_size, src,
		                            src_size);
#else
		{
#if LIBAVCODEC_VERSION_MAJOR < 55
			AVFrame* decoded_frame = avcodec_alloc_frame();
#else
			AVFrame* decoded_frame = av_frame_alloc();
#endif
			int got_frame = 0;
			AVPacket pkt = { 0 };
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 133, 100)
			av_init_packet(&pkt);
#endif

			pkt.data = WINPR_CAST_CONST_PTR_AWAY(src, BYTE*);
			pkt.size = WINPR_ASSERTING_INT_CAST(int, src_size);
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(57, 48, 101)
			len = avcodec_decode_audio4(mdecoder->codec_context, decoded_frame, &got_frame, &pkt);
#else
			len = avcodec_send_packet(mdecoder->codec_context, &pkt);
			if (len > 0)
			{
				len = avcodec_receive_frame(mdecoder->codec_context, decoded_frame);
				if (len == AVERROR(EAGAIN))
					return TRUE;
			}
#endif

			if (len >= 0 && got_frame)
			{
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
				const int channels = mdecoder->codec_context->ch_layout.nb_channels;
#else
				const int channels = mdecoder->codec_context->channels;
#endif
				frame_size = av_samples_get_buffer_size(NULL, channels, decoded_frame->nb_samples,
				                                        mdecoder->codec_context->sample_fmt, 1);
				memcpy(dst, decoded_frame->data[0], frame_size);
			}
			else
			{
				frame_size = 0;
			}

			av_free(decoded_frame);
		}
#endif

		if (len > 0)
		{
			src += len;
			src_size -= len;
		}

		if (frame_size > 0)
		{
			mdecoder->decoded_size += frame_size;
			dst += frame_size;
		}
	}

	if (mdecoder->decoded_size == 0)
	{
		free(mdecoder->decoded_data);
		mdecoder->decoded_data = NULL;
	}
	else if (dst_offset)
	{
		/* move the aligned decoded data to original place */
		memmove(mdecoder->decoded_data, mdecoder->decoded_data + dst_offset,
		        mdecoder->decoded_size);
	}

	DEBUG_TSMF("data_size %" PRIu32 " decoded_size %" PRIu32 "", data_size, mdecoder->decoded_size);
	return TRUE;
}

static BOOL tsmf_ffmpeg_decode(ITSMFDecoder* decoder, const BYTE* data, UINT32 data_size,
                               UINT32 extensions)
{
	TSMFFFmpegDecoder* mdecoder = (TSMFFFmpegDecoder*)decoder;

	if (mdecoder->decoded_data)
	{
		free(mdecoder->decoded_data);
		mdecoder->decoded_data = NULL;
	}

	mdecoder->decoded_size = 0;

	switch (mdecoder->media_type)
	{
		case AVMEDIA_TYPE_VIDEO:
			return tsmf_ffmpeg_decode_video(decoder, data, data_size, extensions);

		case AVMEDIA_TYPE_AUDIO:
			return tsmf_ffmpeg_decode_audio(decoder, data, data_size, extensions);

		default:
			WLog_ERR(TAG, "unknown media type.");
			return FALSE;
	}
}

static BYTE* tsmf_ffmpeg_get_decoded_data(ITSMFDecoder* decoder, UINT32* size)
{
	BYTE* buf = NULL;
	TSMFFFmpegDecoder* mdecoder = (TSMFFFmpegDecoder*)decoder;
	*size = mdecoder->decoded_size;
	buf = mdecoder->decoded_data;
	mdecoder->decoded_data = NULL;
	mdecoder->decoded_size = 0;
	return buf;
}

static UINT32 tsmf_ffmpeg_get_decoded_format(ITSMFDecoder* decoder)
{
	TSMFFFmpegDecoder* mdecoder = (TSMFFFmpegDecoder*)decoder;

	switch (mdecoder->codec_context->pix_fmt)
	{
		case AV_PIX_FMT_YUV420P:
			return RDP_PIXFMT_I420;

		default:
			WLog_ERR(TAG, "unsupported pixel format %u", mdecoder->codec_context->pix_fmt);
			return (UINT32)-1;
	}
}

static BOOL tsmf_ffmpeg_get_decoded_dimension(ITSMFDecoder* decoder, UINT32* width, UINT32* height)
{
	TSMFFFmpegDecoder* mdecoder = (TSMFFFmpegDecoder*)decoder;

	if (mdecoder->codec_context->width > 0 && mdecoder->codec_context->height > 0)
	{
		*width = mdecoder->codec_context->width;
		*height = mdecoder->codec_context->height;
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

static void tsmf_ffmpeg_free(ITSMFDecoder* decoder)
{
	TSMFFFmpegDecoder* mdecoder = (TSMFFFmpegDecoder*)decoder;

	if (mdecoder->frame)
		av_free(mdecoder->frame);

	free(mdecoder->decoded_data);

	if (mdecoder->codec_context)
	{
		free(mdecoder->codec_context->extradata);
		mdecoder->codec_context->extradata = NULL;

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(55, 69, 100)
		avcodec_free_context(&mdecoder->codec_context);
#else
		if (mdecoder->prepared)
			avcodec_close(mdecoder->codec_context);

		av_free(mdecoder->codec_context);
#endif
	}

	free(decoder);
}

static INIT_ONCE g_Initialized = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK InitializeAvCodecs(PINIT_ONCE once, PVOID param, PVOID* context)
{
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 10, 100)
	avcodec_register_all();
#endif
	return TRUE;
}

FREERDP_ENTRY_POINT(UINT VCAPITYPE ffmpeg_freerdp_tsmf_client_decoder_subsystem_entry(void* ptr))
{
	ITSMFDecoder** sptr = (ITSMFDecoder**)ptr;
	WINPR_ASSERT(sptr);
	*sptr = NULL;

	TSMFFFmpegDecoder* decoder = NULL;
	InitOnceExecuteOnce(&g_Initialized, InitializeAvCodecs, NULL, NULL);
	WLog_DBG(TAG, "TSMFDecoderEntry FFMPEG");
	decoder = (TSMFFFmpegDecoder*)calloc(1, sizeof(TSMFFFmpegDecoder));

	if (!decoder)
		return ERROR_OUTOFMEMORY;

	decoder->iface.SetFormat = tsmf_ffmpeg_set_format;
	decoder->iface.Decode = tsmf_ffmpeg_decode;
	decoder->iface.GetDecodedData = tsmf_ffmpeg_get_decoded_data;
	decoder->iface.GetDecodedFormat = tsmf_ffmpeg_get_decoded_format;
	decoder->iface.GetDecodedDimension = tsmf_ffmpeg_get_decoded_dimension;
	decoder->iface.Free = tsmf_ffmpeg_free;
	*sptr = &decoder->iface;
	return CHANNEL_RC_OK;
}
