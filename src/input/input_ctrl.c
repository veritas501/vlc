/*******************************************************************************
 * input_ctrl.c: Decodeur control
 * (c)1999 VideoLAN
 *******************************************************************************
 * Control the extraction and the decoding of the programs elements carried in
 * a stream.
 *******************************************************************************/

/*******************************************************************************
 * Preamble
 *******************************************************************************/
#include <errno.h>
#include <pthread.h>
#include <sys/uio.h>                                                 /* iovec */
#include <stdlib.h>                               /* atoi(), malloc(), free() */
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <sys/soundcard.h>
#include <netinet/in.h>                                              /* ntohs */

#include "common.h"
#include "config.h"
#include "mtime.h"
#include "intf_msg.h"
#include "debug.h"

#include "input.h"
#include "input_netlist.h"

#include "decoder_fifo.h"

#include "audio_output.h"
#include "audio_dsp.h"
#include "audio_decoder.h"

#include "video.h"
#include "video_output.h"
#include "video_decoder.h"


/******************************************************************************
 * input_AddPgrmElem: Start the extraction and the decoding of a program element
 ******************************************************************************
 * Add the element given by its PID in the list of PID to extract and spawn
 * the decoding thread. 
 * This function only modifies the table of selected es, but must NOT modify
 * the table of ES itself.
 ******************************************************************************/
int input_AddPgrmElem( input_thread_t *p_input, int i_current_id )
{
    int i_es_loop, i_selected_es_loop;
    
    /* Since this function is intended to be called by interface, lock the
     * elementary stream structure. */
    pthread_mutex_lock( &p_input->es_lock );

    /* Find out which PID we need. */
    for( i_es_loop = 0; i_es_loop < INPUT_MAX_ES; i_es_loop++ )
    {
        if( p_input->p_es[i_es_loop].i_id == i_current_id )
        {
            if( p_input->p_es[i_es_loop].p_dec != NULL )
            {
                /* We already have a decoder for that PID. */
                pthread_mutex_unlock( &p_input->es_lock );
                intf_ErrMsg("input error: PID %d already selected\n",
                            i_current_id);
                return( -1 );
            }

            intf_DbgMsg("Requesting selection of PID %d\n",
                        i_current_id);

            /* Find a free spot in pp_selected_es. */
            for( i_selected_es_loop = 0; p_input->pp_selected_es[i_selected_es_loop] != NULL
                  && i_selected_es_loop < INPUT_MAX_SELECTED_ES; i_selected_es_loop++ );
            
            if( i_selected_es_loop == INPUT_MAX_SELECTED_ES )
            {
                /* array full */
                pthread_mutex_unlock( &p_input->es_lock );
                intf_ErrMsg("input error: MAX_SELECTED_ES reached: try increasing it in config.h\n");
                return( -1 );
            }

            /* Don't decode PSI streams ! */
            if( p_input->p_es[i_es_loop].b_psi )
            {
                intf_ErrMsg("input_error: trying to decode PID %d which is the one of a PSI\n");
                pthread_mutex_unlock( &p_input->es_lock );
                return( -1 );
            }
            else
            {
                /* Spawn the decoder. */
                switch( p_input->p_es[i_es_loop].i_type )
                {
                    case MPEG1_AUDIO_ES:
                    case MPEG2_AUDIO_ES:
                        /* Spawn audio thread. */
                        if( ((adec_thread_t*)(p_input->p_es[i_es_loop].p_dec) =
                            adec_CreateThread( p_input )) == NULL )
                        {
                            intf_ErrMsg("Could not start audio decoder\n");
                            pthread_mutex_unlock( &p_input->es_lock );
                            return( -1 );
                        }
                        break;

                    case MPEG1_VIDEO_ES:
                    case MPEG2_VIDEO_ES:
                        /* Spawn video thread. */
/* Les 2 pointeurs NULL ne doivent pas etre NULL sinon on segfault !!!! */
                        if( ((vdec_thread_t*)(p_input->p_es[i_es_loop].p_dec) =
                            vdec_CreateThread( p_input )) == NULL )
                        {
                            intf_ErrMsg("Could not start video decoder\n");
                            pthread_mutex_unlock( &p_input->es_lock );
                            return( -1 );
                        }
                        break;

                    default:
                        /* That should never happen. */
                        intf_DbgMsg("input error: unknown stream type (%d)\n",
                                    p_input->p_es[i_es_loop].i_type);
                        pthread_mutex_unlock( &p_input->es_lock );
                        return( -1 );
                        break;
                }

                /* Initialise the demux */
                p_input->p_es[i_es_loop].p_pes_packet = NULL;
                p_input->p_es[i_es_loop].i_continuity_counter = 0;
                p_input->p_es[i_es_loop].b_random = 0;
		
                /* Mark stream to be demultiplexed. */
                intf_DbgMsg("Stream %d added in %d\n", i_current_id, i_selected_es_loop);
                p_input->pp_selected_es[i_selected_es_loop] = &p_input->p_es[i_es_loop];
                pthread_mutex_unlock( &p_input->es_lock );
                return( 0 );
            }
        }
    }
    
    /* We haven't found this PID in the current stream. */
    pthread_mutex_unlock( &p_input->es_lock );
    intf_ErrMsg("input error: can't find PID %d\n", i_current_id);
    return( -1 );
}

/******************************************************************************
 * input_DelPgrmElem: Stop the decoding of a program element
 ******************************************************************************
 * Stop the extraction of the element given by its PID and kill the associated
 * decoder thread
 * This function only modifies the table of selected es, but must NOT modify
 * the table of ES itself.
 ******************************************************************************/
int input_DelPgrmElem( input_thread_t *p_input, int i_current_id )
{
    int i_selected_es_loop, i_last_selected;

    /* Since this function is intended to be called by interface, lock the
       structure. */
    pthread_mutex_lock( &p_input->es_lock );

    /* Find out which PID we need. */
    for( i_selected_es_loop = 0; i_selected_es_loop < INPUT_MAX_SELECTED_ES;
         i_selected_es_loop++ )
    {
        if( p_input->pp_selected_es[i_selected_es_loop] )
        {
            if( p_input->pp_selected_es[i_selected_es_loop]->i_id == i_current_id )
            {
                if( !(p_input->pp_selected_es[i_selected_es_loop]->p_dec) )
                {
                    /* We don't have a decoder for that PID. */
                    pthread_mutex_unlock( &p_input->es_lock );
                    intf_ErrMsg("input error: PID %d already deselected\n",
                                i_current_id);
                    return( -1 );
                }

                intf_DbgMsg("input debug: requesting termination of PID %d\n",
                            i_current_id);

                /* Cancel the decoder. */
                switch( p_input->pp_selected_es[i_selected_es_loop]->i_type )
                {
                    case MPEG1_AUDIO_ES:
                    case MPEG2_AUDIO_ES:
                        adec_DestroyThread( (adec_thread_t*)(p_input->pp_selected_es[i_selected_es_loop]->p_dec) );
                        break;

                    case MPEG1_VIDEO_ES:
                    case MPEG2_VIDEO_ES:
                        vdec_DestroyThread( (vdec_thread_t*)(p_input->pp_selected_es[i_selected_es_loop]->p_dec) /*, NULL */ );
                        break;
                }

                /* Unmark stream. */
                p_input->pp_selected_es[i_selected_es_loop]->p_dec = NULL;

                /* Find last selected stream. */
                for( i_last_selected = i_selected_es_loop;
                        p_input->pp_selected_es[i_last_selected]
                        && i_last_selected < INPUT_MAX_SELECTED_ES;
                     i_last_selected++ );

                /* Exchange streams. */
                p_input->pp_selected_es[i_selected_es_loop] = 
                            p_input->pp_selected_es[i_last_selected];
                p_input->pp_selected_es[i_last_selected] = NULL;

                pthread_mutex_unlock( &p_input->es_lock );
                return( 0 );
            }
        }
    }

    /* We haven't found this PID in the current stream. */
    pthread_mutex_unlock( &p_input->es_lock );
    intf_ErrMsg("input error: can't find PID %d\n", i_current_id);
    return( -1 );
}



/******************************************************************************
 * input_IsElemRecv: Test if an element given by its PID is currently received
 ******************************************************************************
 * Cannot return the position of the es in the pp_selected_es, for it can
 * change once we have released the lock
 ******************************************************************************/
boolean_t input_IsElemRecv( input_thread_t *p_input, int i_id )
{
  boolean_t b_is_recv = 0;
    int i_index = 0;

   /* Since this function is intended to be called by interface, lock the
       structure. */
    pthread_mutex_lock( &p_input->es_lock );

    /* Scan the table */
    while( i_index < INPUT_MAX_SELECTED_ES && !p_input->pp_selected_es[i_index] )
    {
      if( p_input->pp_selected_es[i_index]->i_id == i_id )
      {
        b_is_recv = 1;
        break;
      }
    }

    /* Unlock the structure */
    pthread_mutex_unlock( &p_input->es_lock );

    return( b_is_recv );
}
