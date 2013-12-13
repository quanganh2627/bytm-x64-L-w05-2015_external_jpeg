/*
 *  Copyright 2012 Intel Corporation All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#include "jerror.h"
#include "jerror_libva_ext.h"
#include "jinclude.h"
#include "jpeglib.h"
#include <pthread.h>
#include "jversion.h"   /* for version message */

#include "jd_libva.h"

j_context_list_decoder *g_context_list_decoder = NULL;
pthread_mutex_t mutex_decoder = PTHREAD_MUTEX_INITIALIZER;
const char * const * jpeg_message_table_temp_decoder = NULL;
#ifndef JMESSAGE(code,string)
#define JMESSAGE(code,string) string ,
const char * const jpeg_libva_ext_message_table_decoder[] = {
#include "jerror.h"
#define JMESSAGE(code,string) string ,
#include "jerror_libva_ext.h"
  NULL
};
#endif

LOCAL(boolean) output_pass_setup JPP((j_decompress_ptr cinfo));

/*
 * Initialization of a JPEG decompression object.
 * The error manager must already be set up (in case memory manager fails).
 */
GLOBAL(void)
jpeg_CreateDecompress_hw (j_decompress_ptr cinfo)
{
  boolean ret = TRUE;
  jd_libva_struct * jd_libva_ptr = malloc (sizeof (jd_libva_struct));
  if (jd_libva_ptr == NULL)
    ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 0);

  MEMZERO (jd_libva_ptr, sizeof (jd_libva_struct));

  /*
   * Please note that we won't check jd_libva_ptr anymore after that to follow the
   * convention of libjpeg
   */

  j_context_list_decoder * context_list_decoder = NULL;
  context_list_decoder = malloc (sizeof (j_context_list_decoder));
  if (context_list_decoder == NULL)
    ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 0);

  context_list_decoder->cinfo = cinfo;
  context_list_decoder->jd_libva_ptr = jd_libva_ptr;
  context_list_decoder->next = NULL;

  pthread_mutex_lock(&mutex_decoder);
  g_context_list_decoder = append_context_list_decoder (g_context_list_decoder, context_list_decoder);
  pthread_mutex_unlock(&mutex_decoder);

  jpeg_message_table_temp_decoder = cinfo->err->jpeg_message_table; //save the std error message table
  cinfo->err->jpeg_message_table = jpeg_libva_ext_message_table_decoder;
  cinfo->err->last_jpeg_message = JMSG_LASTLIBVAEXTCODE - 1;

  if (!(jd_libva_ptr->initialized)) {
    ret = jdva_initialize (jd_libva_ptr);
    if (ret) {
      jd_libva_ptr->hw_state_ready = FALSE;
      WARNMS(cinfo, JERR_JVA_INITIALIZE);  //HW JPEG initialize error, we have to go to software path
    } else {
      jd_libva_ptr->hw_state_ready = TRUE;
    }
  }
}

GLOBAL(void)
jpeg_destroy_decompress_hw (j_decompress_ptr cinfo)
{
  j_context_list_decoder * context_list_decoder = NULL;
  pthread_mutex_lock(&mutex_decoder);
  context_list_decoder = get_context_list_decoder_by_cinfo (g_context_list_decoder, cinfo);
  pthread_mutex_unlock(&mutex_decoder);

  if (!context_list_decoder) {
    ERREXIT1(cinfo, JERR_CINFO_NOT_FOUND, cinfo);
  }

  jd_libva_struct * jd_libva_ptr = context_list_decoder->jd_libva_ptr;
  if (!(jd_libva_ptr)) {
    ERREXIT(cinfo, JERR_NULL_JDVA_CONTEXT);
  }

  if (jd_libva_ptr->hw_path || jd_libva_ptr->hw_state_ready)
    jpeg_destroy_decompress_libva ((j_decompress_ptr) cinfo, jd_libva_ptr);
  else
    jpeg_destroy_decompress_native ((j_decompress_ptr) cinfo);

  /*
  * Here we already finish the decoding
  * Free the jd_libva context
  * remove from the context list
  */
  pthread_mutex_lock(&mutex_decoder);
  g_context_list_decoder = remove_context_list_decoder (g_context_list_decoder, context_list_decoder);
  pthread_mutex_unlock(&mutex_decoder);

  if (context_list_decoder) {
    if (context_list_decoder->jd_libva_ptr) {
      free (context_list_decoder->jd_libva_ptr);
      context_list_decoder->jd_libva_ptr = NULL;
    }
    free (context_list_decoder);
    context_list_decoder = NULL;
  }
}

GLOBAL(void)
jpeg_destroy_decompress_libva (j_decompress_ptr cinfo, jd_libva_struct * jd_libva_ptr)
{
//  jdva_release_resource (jd_libva_ptr);
  jdva_deinitialize (jd_libva_ptr);
  if (jd_libva_ptr->bitstream_buf) {
    free(jd_libva_ptr->bitstream_buf);
    jd_libva_ptr->bitstream_buf = NULL;
  }
//  jpeg_abort((j_common_ptr) cinfo);
  jpeg_destroy((j_common_ptr) cinfo); /* use common routine */
}

GLOBAL(void)
jpeg_destroy_decompress_native (j_decompress_ptr cinfo)
{
  jpeg_destroy((j_common_ptr) cinfo); /* use common routine */
}

GLOBAL(void)
jpeg_abort_decompress_hw (j_decompress_ptr cinfo)
{
  j_context_list_decoder * context_list_decoder = NULL;
  pthread_mutex_lock(&mutex_decoder);
  context_list_decoder = get_context_list_decoder_by_cinfo (g_context_list_decoder, cinfo);
  pthread_mutex_unlock(&mutex_decoder);
  if (!context_list_decoder) {
    ERREXIT1(cinfo, JERR_CINFO_NOT_FOUND, cinfo);
  }

  jd_libva_struct * jd_libva_ptr = context_list_decoder->jd_libva_ptr;
  if (!(jd_libva_ptr)) {
    ERREXIT(cinfo, JERR_NULL_JDVA_CONTEXT);
  }
  if (jd_libva_ptr->hw_path) {
    jpeg_abort_decompress_libva (cinfo, jd_libva_ptr);
  } else {
    jpeg_abort_decompress_native (cinfo);
  }

  pthread_mutex_lock(&mutex_decoder);
  g_context_list_decoder = remove_context_list_decoder (g_context_list_decoder, context_list_decoder);
  pthread_mutex_unlock(&mutex_decoder);

  if (context_list_decoder) {
    if (context_list_decoder->jd_libva_ptr) {
      free (context_list_decoder->jd_libva_ptr);
      context_list_decoder->jd_libva_ptr = NULL;
    }
    free (context_list_decoder);
    context_list_decoder = NULL;
  }
}

GLOBAL(void)
jpeg_abort_decompress_libva (j_decompress_ptr cinfo, jd_libva_struct * jd_libva_ptr)
{
  // jva_release_resource (cinfo, jd_libva_ptr);
  // jva_deinitialize (cinfo, jd_libva_ptr);
  jpeg_abort((j_common_ptr) cinfo); /* use common routine */
}

GLOBAL(void)
jpeg_abort_decompress_native (j_decompress_ptr cinfo)
{
  jpeg_abort((j_common_ptr) cinfo); /* use common routine */
}

GLOBAL(int)
jpeg_read_header_hw (j_decompress_ptr cinfo, boolean require_image)
{
  int retcode;
  int ret;
  j_context_list_decoder * context_list_decoder = NULL;
  pthread_mutex_lock(&mutex_decoder);
  context_list_decoder = get_context_list_decoder_by_cinfo (g_context_list_decoder, cinfo);
  pthread_mutex_unlock(&mutex_decoder);
  if (!context_list_decoder) {
    ERREXIT1(cinfo, JERR_CINFO_NOT_FOUND, cinfo);
  }

  jd_libva_struct * jd_libva_ptr = context_list_decoder->jd_libva_ptr;
  if (!(jd_libva_ptr)) {
    ERREXIT(cinfo, JERR_NULL_JDVA_CONTEXT);
  }

  ret = jpeg_read_header_libva(cinfo, jd_libva_ptr);
  if (jd_libva_ptr->hw_state_ready && (!ret)) {
    retcode = JPEG_HEADER_OK;
    cinfo->out_color_space = JCS_RGB;
    jd_libva_ptr->hw_path = TRUE;
  } else {
    if (jd_libva_ptr->hw_state_ready) {
      jdva_deinitialize (jd_libva_ptr);
    }
    retcode = jpeg_read_header_native (cinfo, require_image);
    jd_libva_ptr->hw_path = FALSE;
    jd_libva_ptr->hw_state_ready = FALSE;
  }

  return retcode;
}

GLOBAL(int)
jpeg_read_header_libva (j_decompress_ptr cinfo, jd_libva_struct * jd_libva_ptr)
{
  long file_size = 1024 * 1024 * 3;
  int ret;
  jd_libva_ptr->bitstream_buf = (uint8_t* )malloc(sizeof(uint8_t) * file_size);
  if (jd_libva_ptr->bitstream_buf == NULL)
    ERREXIT1(cinfo, JERR_OUT_OF_MEMORY, 0);

  memset(jd_libva_ptr->bitstream_buf, 0, file_size);
  jd_libva_ptr->file_size = 0;

  (*cinfo->inputctl->reset_input_controller)(cinfo);
  (*cinfo->src->init_source)(cinfo);

  do {
    if ((*cinfo->src->fill_input_buffer)(cinfo)) {
        memcpy(jd_libva_ptr->bitstream_buf + jd_libva_ptr->file_size, cinfo->src->next_input_byte, cinfo->src->bytes_in_buffer);
        jd_libva_ptr->file_size += cinfo->src->bytes_in_buffer;
    }
    else {
        // fill_input_buffer returns FALSE when no byte is read
        break;
    }
  } while (cinfo->src->bytes_in_buffer > 0);

  ret = jdva_parse_bitstream(cinfo, jd_libva_ptr);
  return ret;
}

GLOBAL(int)
jpeg_read_header_native (j_decompress_ptr cinfo, boolean require_image)
{
  int retcode;

  if (cinfo->global_state != DSTATE_START &&
      cinfo->global_state != DSTATE_INHEADER)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);

  retcode = jpeg_consume_input(cinfo);

  switch (retcode) {
  case JPEG_REACHED_SOS:
    retcode = JPEG_HEADER_OK;
    break;
  case JPEG_REACHED_EOI:
    if (require_image)  /* Complain if application wanted an image */
      ERREXIT(cinfo, JERR_NO_IMAGE);
    /* Reset to start state; it would be safer to require the application to
     * call jpeg_abort, but we can't change it now for compatibility reasons.
     * A side effect is to free any temporary memory (there shouldn't be any).
     */
    jpeg_abort((j_common_ptr) cinfo); /* sets state = DSTATE_START */
    retcode = JPEG_HEADER_TABLES_ONLY;
    break;
  case JPEG_SUSPENDED:
    /* no work */
    break;
  }

  return retcode;
}

GLOBAL(boolean)
jpeg_finish_decompress_hw (j_decompress_ptr cinfo)
{
   boolean ret;
  j_context_list_decoder * context_list_decoder = NULL;
  pthread_mutex_lock(&mutex_decoder);
  context_list_decoder = get_context_list_decoder_by_cinfo (g_context_list_decoder, cinfo);
  pthread_mutex_unlock(&mutex_decoder);
  if (!context_list_decoder) {
    ERREXIT1(cinfo, JERR_CINFO_NOT_FOUND, cinfo);
  }

  jd_libva_struct * jd_libva_ptr = context_list_decoder->jd_libva_ptr;
  if (!(jd_libva_ptr)) {
    ERREXIT(cinfo, JERR_NULL_JDVA_CONTEXT);
  }
  if (jd_libva_ptr->hw_path) {
    ret = jpeg_finish_decompress_libva(cinfo, jd_libva_ptr);
  } else {
    ret = jpeg_finish_decompress_native(cinfo);
  }
  return ret;
}

GLOBAL(boolean)
jpeg_finish_decompress_libva (j_decompress_ptr cinfo, jd_libva_struct * jd_libva_ptr)
{
  int ret;
  ret = jdva_release_resource(jd_libva_ptr);
  free (jd_libva_ptr->output_image);
  jd_libva_ptr->output_lines = 0;
  if (!ret)
    return TRUE;
  else
    return FALSE;
}

GLOBAL(boolean)
jpeg_finish_decompress_native (j_decompress_ptr cinfo)
{
  if ((cinfo->global_state == DSTATE_SCANNING ||
       cinfo->global_state == DSTATE_RAW_OK) && ! cinfo->buffered_image) {
    /* Terminate final pass of non-buffered mode */
#ifdef ANDROID_TILE_BASED_DECODE
    cinfo->output_scanline = cinfo->output_height;
#endif
    if (cinfo->output_scanline < cinfo->output_height)
      ERREXIT(cinfo, JERR_TOO_LITTLE_DATA);
    (*cinfo->master->finish_output_pass) (cinfo);
    cinfo->global_state = DSTATE_STOPPING;
  } else if (cinfo->global_state == DSTATE_BUFIMAGE) {
    /* Finishing after a buffered-image operation */
    cinfo->global_state = DSTATE_STOPPING;
  } else if (cinfo->global_state != DSTATE_STOPPING) {
    /* STOPPING = repeat call after a suspension, anything else is error */
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  }
  /* Read until EOI */
#ifndef ANDROID_TILE_BASED_DECODE
  while (! cinfo->inputctl->eoi_reached) {
    if ((*cinfo->inputctl->consume_input) (cinfo) == JPEG_SUSPENDED)
      return FALSE;  /* Suspend, come back later */
  }
#endif
  /* Do final cleanup */
  (*cinfo->src->term_source) (cinfo);
  /* We can use jpeg_abort to release memory and reset global_state */
  jpeg_abort((j_common_ptr) cinfo);
  return TRUE;
}

GLOBAL(boolean)
jpeg_start_decompress_hw (j_decompress_ptr cinfo)
{
  boolean ret;
  j_context_list_decoder * context_list_decoder = NULL;
  pthread_mutex_lock(&mutex_decoder);
  context_list_decoder = get_context_list_decoder_by_cinfo (g_context_list_decoder, cinfo);
  pthread_mutex_unlock(&mutex_decoder);
  if (!context_list_decoder) {
    ERREXIT1(cinfo, JERR_CINFO_NOT_FOUND, cinfo);
  }

  jd_libva_struct * jd_libva_ptr = context_list_decoder->jd_libva_ptr;
  if (!(jd_libva_ptr)) {
    ERREXIT(cinfo, JERR_NULL_JDVA_CONTEXT);
  }
  if (jd_libva_ptr->hw_state_ready && jd_libva_ptr->hw_path) {
    ret = jpeg_start_decompress_libva (cinfo, jd_libva_ptr);
  } else {
    ret = jpeg_start_decompress_native (cinfo);
  }
  return ret;
}

GLOBAL(boolean)
jpeg_start_decompress_libva (j_decompress_ptr cinfo, jd_libva_struct * jd_libva_ptr)
{
  int ret;

  ret = jdva_create_resource(jd_libva_ptr);
  if (!ret) {
    cinfo->output_width = cinfo->image_width;
    cinfo->output_height = cinfo->image_height;
    cinfo->output_scan_number = cinfo->input_scan_number;
    jd_libva_ptr->output_image = (char**)malloc(cinfo->image_height * sizeof(char*));
    jd_libva_ptr->output_lines = 0;
    return TRUE;
  }
  else
    return FALSE;
}

GLOBAL(boolean)
jpeg_start_decompress_native (j_decompress_ptr cinfo)
{
  if (cinfo->global_state == DSTATE_READY) {
    /* First call: initialize master control, select active modules */
    jinit_master_decompress(cinfo);
    if (cinfo->buffered_image) {
      /* No more work here; expecting jpeg_start_output next */
      cinfo->global_state = DSTATE_BUFIMAGE;
      return TRUE;
    }
    cinfo->global_state = DSTATE_PRELOAD;
  }
  if (cinfo->global_state == DSTATE_PRELOAD) {
    /* If file has multiple scans, absorb them all into the coef buffer */
    if (cinfo->inputctl->has_multiple_scans) {
#ifdef D_MULTISCAN_FILES_SUPPORTED
      for (;;) {
        int retcode;
        /* Call progress monitor hook if present */
        if (cinfo->progress != NULL)
          (*cinfo->progress->progress_monitor) ((j_common_ptr) cinfo);
        /* Absorb some more input */
        retcode = (*cinfo->inputctl->consume_input) (cinfo);
        if (retcode == JPEG_SUSPENDED)
          return FALSE;
        if (retcode == JPEG_REACHED_EOI)
          break;
        /* Advance progress counter if appropriate */
        if (cinfo->progress != NULL &&
          (retcode == JPEG_ROW_COMPLETED || retcode == JPEG_REACHED_SOS)) {
          if (++cinfo->progress->pass_counter >= cinfo->progress->pass_limit) {
          /* jdmaster underestimated number of scans; ratchet up one scan */
            cinfo->progress->pass_limit += (long) cinfo->total_iMCU_rows;
          }
        }
      }
#else
      ERREXIT(cinfo, JERR_NOT_COMPILED);
#endif /* D_MULTISCAN_FILES_SUPPORTED */
    }
    cinfo->output_scan_number = cinfo->input_scan_number;
  } else if (cinfo->global_state != DSTATE_PRESCAN)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  /* Perform any dummy output passes, and set up for the final pass */
  return output_pass_setup(cinfo);
}

GLOBAL(JDIMENSION)
jpeg_read_scanlines_hw (j_decompress_ptr cinfo, JSAMPARRAY scanlines, JDIMENSION max_lines)
{
  JDIMENSION row_ctr;
  JDIMENSION cur_line;
  j_context_list_decoder * context_list_decoder = NULL;
  pthread_mutex_lock(&mutex_decoder);
  context_list_decoder = get_context_list_decoder_by_cinfo (g_context_list_decoder, cinfo);
  pthread_mutex_unlock(&mutex_decoder);
  if (!context_list_decoder) {
    ERREXIT1(cinfo, JERR_CINFO_NOT_FOUND, cinfo);
  }

  jd_libva_struct * jd_libva_ptr = context_list_decoder->jd_libva_ptr;
  if (!(jd_libva_ptr)) {
    ERREXIT(cinfo, JERR_NULL_JDVA_CONTEXT);
  }
  if (jd_libva_ptr->hw_path) {
    for (cur_line = 0; cur_line < max_lines; ++ cur_line) {
        jd_libva_ptr->output_image[jd_libva_ptr->output_lines++] = scanlines[cur_line];
    }
    if (jd_libva_ptr->output_lines == cinfo->output_height)
        row_ctr = jpeg_read_scanlines_libva (cinfo, jd_libva_ptr);
    else
        row_ctr = max_lines;
  } else {
    row_ctr = jpeg_read_scanlines_native (cinfo, scanlines, max_lines);
  }
  return row_ctr;
}

GLOBAL(JDIMENSION)
jpeg_read_scanlines_libva (j_decompress_ptr cinfo, jd_libva_struct * jd_libva_ptr)
{
  int ret;
  ret = jdva_decode (cinfo, jd_libva_ptr);
  if (ret)
    ERREXIT1(cinfo, JERR_JVA_DECODE, cinfo);

  cinfo->output_scanline = cinfo->output_height;
  return cinfo->output_scanline;
}

GLOBAL(JDIMENSION)
jpeg_read_scanlines_native (j_decompress_ptr cinfo, JSAMPARRAY scanlines, JDIMENSION max_lines)
{
  JDIMENSION row_ctr;
  if (cinfo->output_scanline >= cinfo->output_height) {
    WARNMS(cinfo, JWRN_TOO_MUCH_DATA);
  return 0;
  }

  if (cinfo->global_state != DSTATE_SCANNING)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  if (cinfo->output_scanline >= cinfo->output_height) {
    WARNMS(cinfo, JWRN_TOO_MUCH_DATA);
    return 0;
  }

  /* Call progress monitor hook if present */
  if (cinfo->progress != NULL) {
    cinfo->progress->pass_counter = (long) cinfo->output_scanline;
    cinfo->progress->pass_limit = (long) cinfo->output_height;
    (*cinfo->progress->progress_monitor) ((j_common_ptr) cinfo);
  }

  /* Process some data */
  row_ctr = 0;
  (*cinfo->main->process_data) (cinfo, scanlines, &row_ctr, max_lines);
  cinfo->output_scanline += row_ctr;
  return row_ctr;
}

/*
* Following 3 funtion is used to operate the list
* The list is to contain the jpeg context as well
* as the HW jpeg context, for thread safe purpose
*/
j_context_list_decoder * append_context_list_decoder (j_context_list_decoder * head, j_context_list_decoder * list)
{
  if (head == NULL) {
    return list;
  }

  j_context_list_decoder *node = head;
  j_context_list_decoder *tail = NULL;

  while (node != NULL) {
    tail = node;
    node = node->next;
  }
  tail->next = list;

  return head;
}

j_context_list_decoder * remove_context_list_decoder (j_context_list_decoder * head, j_context_list_decoder * list)
{
  j_context_list_decoder * node = head;
  j_context_list_decoder * tmpNode = NULL;

  if (head == list) {
    tmpNode = head->next;
    list->next = NULL;
    return tmpNode;
  }

  while (node != NULL) {
    if (node->next == list)
      break;
    node = node->next;
  }

  if (node != NULL) {
    node->next = list->next;
  }

  list->next = NULL;
  return head;

}

j_context_list_decoder * get_context_list_decoder_by_cinfo (j_context_list_decoder * head, j_decompress_ptr cinfo)
{
  j_context_list_decoder * node = head;

  while (node != NULL) {
    if (node->cinfo == cinfo)
      break;
    node = node->next;
  }

  return node;

}

/*
 * Set up for an output pass, and perform any dummy pass(es) needed.
 * Common subroutine for jpeg_start_decompress and jpeg_start_output.
 * Entry: global_state = DSTATE_PRESCAN only if previously suspended.
 * Exit: If done, returns TRUE and sets global_state for proper output mode.
 *       If suspended, returns FALSE and sets global_state = DSTATE_PRESCAN.
 */

LOCAL(boolean)
output_pass_setup (j_decompress_ptr cinfo)
{
  if (cinfo->global_state != DSTATE_PRESCAN) {
    /* First call: do pass setup */
    (*cinfo->master->prepare_for_output_pass) (cinfo);
    cinfo->output_scanline = 0;
    cinfo->global_state = DSTATE_PRESCAN;
  }
  /* Loop over any required dummy passes */
  while (cinfo->master->is_dummy_pass) {
#ifdef QUANT_2PASS_SUPPORTED
    /* Crank through the dummy pass */
    while (cinfo->output_scanline < cinfo->output_height) {
      JDIMENSION last_scanline;
      /* Call progress monitor hook if present */
      if (cinfo->progress != NULL) {
        cinfo->progress->pass_counter = (long) cinfo->output_scanline;
        cinfo->progress->pass_limit = (long) cinfo->output_height;
        (*cinfo->progress->progress_monitor) ((j_common_ptr) cinfo);
      }
      /* Process some data */
      last_scanline = cinfo->output_scanline;
      (*cinfo->main->process_data) (cinfo, (JSAMPARRAY) NULL,
                                    &cinfo->output_scanline, (JDIMENSION) 0);
      if (cinfo->output_scanline == last_scanline)
        return FALSE;  /* No progress made, must suspend */
    }
    /* Finish up dummy pass, and set up for another one */
    (*cinfo->master->finish_output_pass) (cinfo);
    (*cinfo->master->prepare_for_output_pass) (cinfo);
    cinfo->output_scanline = 0;
#else
    ERREXIT(cinfo, JERR_NOT_COMPILED);
#endif /* QUANT_2PASS_SUPPORTED */
  }
  /* Ready for application to drive output pass through
   * jpeg_read_scanlines or jpeg_read_raw_data.
   */
  cinfo->global_state = cinfo->raw_data_out ? DSTATE_RAW_OK : DSTATE_SCANNING;
  return TRUE;
}
