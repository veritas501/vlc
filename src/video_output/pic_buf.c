/*****************************************************************************
 * pic_buf.c
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>

#include "pic_buf.h"

#include <assert.h>
#include <vlc_picture.h>

void vlc_pic_buf_Init(struct vlc_pic_buf *pb)
{
    vlc_mutex_init(&pb->mutex);
    vlc_cond_init(&pb->cond);
    pb->pending = NULL;
    pb->stopped = false;
}

void vlc_pic_buf_Destroy(struct vlc_pic_buf *pb)
{
    if (pb->pending)
        picture_Release(pb->pending);
}

void vlc_pic_buf_Push(struct vlc_pic_buf *pb, picture_t *pic)
{
    vlc_mutex_lock(&pb->mutex);

    if (pb->pending)
        picture_Release(pb->pending);

    /* Take ownership of pic */
    pb->pending = pic;
    vlc_cond_signal(&pb->cond);

    vlc_mutex_unlock(&pb->mutex);
}

picture_t *vlc_pic_buf_Pop(struct vlc_pic_buf *pb)
{
    vlc_mutex_lock(&pb->mutex);

    while (!pb->stopped && !pb->pending)
        vlc_cond_wait(&pb->cond, &pb->mutex);

    if (pb->stopped)
    {
        vlc_mutex_unlock(&pb->mutex);
        return NULL;
    }

    picture_t *out = pb->pending;
    pb->pending = NULL;
    vlc_mutex_unlock(&pb->mutex);

    assert(out);
    return out;
}

void vlc_pic_buf_Stop(struct vlc_pic_buf *pb)
{
    vlc_mutex_lock(&pb->mutex);
    pb->stopped = true;
    vlc_cond_signal(&pb->cond);
    vlc_mutex_unlock(&pb->mutex);
}
