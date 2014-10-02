/* GStreamer
 *
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_IOS_INIT_H__
#define __GST_IOS_INIT_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_G_IO_MODULE_DECLARE(name) \
extern void G_PASTE(g_io_module_, G_PASTE(name, _load_static)) (void)

#define GST_G_IO_MODULE_LOAD(name) \
G_PASTE(g_io_module_, G_PASTE(name, _load_static)) ()

/* Uncomment each line to enable the plugin categories that your application needs.
 * You can also enable individual plugins. See gst_ios_init.c to see their names
 */

#define GST_IOS_PLUGINS_CORE
#define GST_IOS_PLUGINS_CAPTURE
#define GST_IOS_PLUGINS_CODECS_RESTRICTED
#define GST_IOS_PLUGINS_ENCODING
#define GST_IOS_PLUGINS_CODECS_GPL
#define GST_IOS_PLUGINS_NET_RESTRICTED
#define GST_IOS_PLUGINS_SYS
#define GST_IOS_PLUGINS_VIS
#define GST_IOS_PLUGINS_PLAYBACK
#define GST_IOS_PLUGINS_EFFECTS
#define GST_IOS_PLUGINS_CODECS
#define GST_IOS_PLUGINS_NET
//#define GST_IOS_PLUGINS_EDITING


#define GST_IOS_GIO_MODULE_GNUTLS

void gst_ios_init ();

G_END_DECLS

#endif
