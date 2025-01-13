/*****************************************************************************
 * pic_buf.h
 *****************************************************************************
 * Copyright © 2022 Videolabs
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

#ifndef VLC_PIC_BUF
#define VLC_PIC_BUF

#include <vlc_threads.h>

struct vlc_pic_buf {
    vlc_mutex_t mutex;
    vlc_cond_t cond;
    picture_t *pending;
    bool stopped;
};

void vlc_pic_buf_Init(struct vlc_pic_buf *pb);

void vlc_pic_buf_Destroy(struct vlc_pic_buf *pb);

void vlc_pic_buf_Push(struct vlc_pic_buf *pb, picture_t *pic);

picture_t *vlc_pic_buf_Pop(struct vlc_pic_buf *pb);

void vlc_pic_buf_Stop(struct vlc_pic_buf *pb);

#endif
