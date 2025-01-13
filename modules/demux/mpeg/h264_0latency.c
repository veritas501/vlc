/*****************************************************************************
 * h264_0latency.c: demux
 *****************************************************************************
 * Copyright (C) 2022 Videolabs
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_demux.h>

#define NO_PTS UINT64_C(-1) // as sent by the server

struct sys
{
    es_format_t fmt;
    es_out_id_t *es;

    block_t *pending;
};

static int Demux(demux_t *demux)
{
    struct sys *sys = demux->p_sys;

#define HEADER_SIZE 12
    unsigned char header[HEADER_SIZE];
    ssize_t r = vlc_stream_Read(demux->s, header, HEADER_SIZE);
    if (r < HEADER_SIZE) {
        return VLC_DEMUXER_EOF;
    }

    // Each raw packet is prepended by a 12-byte header
    // [. . . . . . . .|. . . .]. . . . . . . . . . . . . . . ...
    //  <-------------> <-----> <-----------------------------...
    //        PTS        packet        raw packet
    //                    size
    uint64_t pts = ((uint64_t) header[0] << 56)
                 | ((uint64_t) header[1] << 48)
                 | ((uint64_t) header[2] << 40)
                 | ((uint64_t) header[3] << 32)
                 | ((uint64_t) header[4] << 24)
                 | (header[5] << 16) | (header[6] << 8) | header[7];
    uint32_t len = ((uint32_t) header[8] << 24)
                 | (header[9] << 16) | (header[10] << 8) | header[11];

    block_t *block = vlc_stream_Block(demux->s, len);
    if (!block)
        return VLC_DEMUXER_EOF;

    bool is_config = pts == NO_PTS;
    block->i_pts = pts != NO_PTS ? pts : VLC_TICK_INVALID;

    if (sys->pending || is_config) {
        size_t offset;
        if (sys->pending) {
            offset = sys->pending->i_buffer;
            block_t *b = block_Realloc(sys->pending, 0, offset + block->i_buffer);
            if (!b) {
                return VLC_DEMUXER_EGENERIC;
            }
            sys->pending = b;
        } else {
            offset = 0;
            sys->pending = block_Alloc(block->i_buffer);
            if (!sys->pending) {
                return VLC_DEMUXER_EGENERIC;
            }
        }

        memcpy(sys->pending->p_buffer + offset, block->p_buffer,
               block->i_buffer);

        if (!is_config) {
            // prepare the concat packet to send to the decoder
            sys->pending->i_pts = block->i_pts;
            sys->pending->i_dts = block->i_dts;
            sys->pending->i_flags = block->i_flags;
            block = sys->pending;
            sys->pending = NULL;
        }
    }

    if (!is_config)
    {
#if 0
        static bool first_packet_printed;
        if (!first_packet_printed) {
            fprintf(stderr, "========= FIRST PACKET (%ld bytes)\n", block->i_buffer);
            for (size_t i = 0; i < __MIN(128, block->i_buffer); ++i)
                fprintf(stderr, "%02x%s", block->p_buffer[i], i%16==15 ? "\n" : " ");
            fprintf(stderr, "========= END FIRST PACKET\n");
            first_packet_printed = true;
        }
#endif

        int ret = es_out_Send(demux->out, sys->es, block);
        if (ret != VLC_SUCCESS)
            return VLC_DEMUXER_EGENERIC;
    }

    return VLC_DEMUXER_SUCCESS;
}

static int Control(demux_t *demux, int ctl, va_list list)
{
    (void) demux;
    (void) ctl;
    (void) list;
    // Reject all controls
    return VLC_EGENERIC;
}

static int Open(vlc_object_t *obj)
{
    demux_t *demux = (demux_t *) obj;

    struct sys *sys = malloc(sizeof(*sys));
    if (!sys)
        return VLC_ENOMEM;

    demux->p_sys = sys;
    demux->pf_demux = Demux;
    demux->pf_control = Control;

    es_format_Init(&sys->fmt, VIDEO_ES, VLC_CODEC_H264);
    sys->es = NULL;
    sys->pending = NULL;

    sys->es = es_out_Add(demux->out, &sys->fmt);
    if (!sys->es)
        return VLC_EGENERIC;

    return VLC_SUCCESS;
}

static void Close(vlc_object_t *obj)
{
    demux_t *demux = (demux_t *) obj;
    struct sys *sys = demux->p_sys;
    free(sys);
}

vlc_module_begin()
    set_shortname("H264_0latency")
    set_subcategory(SUBCAT_INPUT_DEMUX)
    set_description("H264 0latency video demuxer")
    set_capability("demux", 0)
    set_callbacks(Open, Close)
    add_shortcut("h264_0latency")
vlc_module_end()
