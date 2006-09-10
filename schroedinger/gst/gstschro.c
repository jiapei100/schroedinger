/* GStreamer
 * Copyright (C) 2005 David Schleef <ds@schleef.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <schroedinger/schro.h>
#include <liboil/liboil.h>

GType gst_schrotoy_get_type (void);
GType gst_schro_enc_get_type (void);
GType gst_schro_dec_get_type (void);
GType gst_schro_parse_get_type (void);
GType gst_waveletvisualizer_get_type (void);

GST_DEBUG_CATEGORY (schro_debug);
#define GST_CAT_DEFAULT schro_debug

static gboolean
plugin_init (GstPlugin * plugin)
{
  schro_init();
  oil_init();

  GST_DEBUG_CATEGORY_INIT (schro_debug, "schro", 0, "Schroedinger");
#if 0
  gst_element_register (plugin, "schrotoy", GST_RANK_NONE,
      gst_schrotoy_get_type ());
#endif
  gst_element_register (plugin, "schroenc", GST_RANK_PRIMARY,
      gst_schro_enc_get_type ());
  gst_element_register (plugin, "schrodec", GST_RANK_PRIMARY,
      gst_schro_dec_get_type ());
  gst_element_register (plugin, "schroparse", GST_RANK_NONE,
      gst_schro_parse_get_type ());
#if 0
  gst_element_register (plugin, "waveletvisualizer", GST_RANK_NONE,
      gst_waveletvisualizer_get_type ());
#endif

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "schro",
    "Schro plugins",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)

