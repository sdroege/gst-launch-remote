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

#include "gst-launch-remote.h"

#include <string.h>
#include <stdlib.h>
#include <gst/net/net.h>

GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

static void gst_launch_remote_set_pipeline (GstLaunchRemote * self,
    const gchar * pipeline_string);

G_LOCK_DEFINE_STATIC (debug_sockets);
typedef struct
{
  GSocket *socket;
  GSocketAddress *address;
} DebugSocket;
static GList *debug_sockets = NULL;

static void
send_debug (const gchar * prefix, const gchar * message)
{
  GList *l;
  GOutputVector data[4];

  data[0].buffer = prefix;
  data[0].size = strlen (prefix);
  data[1].buffer = ": ";
  data[1].size = 2;
  data[2].buffer = message;
  data[2].size = strlen (message);
  data[3].buffer = "\n";
  data[3].size = 1;

  G_LOCK (debug_sockets);
  for (l = debug_sockets; l; l = l->next) {
    DebugSocket *s = l->data;

    if (!s->address)
      continue;

    g_socket_send_message (s->socket, s->address, data, G_N_ELEMENTS (data),
        NULL, 0, G_SOCKET_MSG_NONE, NULL, NULL);
  }
  G_UNLOCK (debug_sockets);
}

void
priv_glib_print_handler (const gchar * string)
{
  send_debug ("GLib+stdout", string);
}

void
priv_glib_printerr_handler (const gchar * string)
{
  send_debug ("GLib+stderr", string);
}


/* Based on GLib's default handler */
#define CHAR_IS_SAFE(wc) (!((wc < 0x20 && wc != '\t' && wc != '\n' && wc != '\r') || \
			    (wc == 0x7f) || \
			    (wc >= 0x80 && wc < 0xa0)))
#define FORMAT_UNSIGNED_BUFSIZE ((GLIB_SIZEOF_LONG * 3) + 3)
#define	STRING_BUFFER_SIZE	(FORMAT_UNSIGNED_BUFSIZE + 32)
#define	ALERT_LEVELS		(G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)
#define DEFAULT_LEVELS (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE)
#define INFO_LEVELS (G_LOG_LEVEL_INFO | G_LOG_LEVEL_DEBUG)

static void
escape_string (GString * string)
{
  const char *p = string->str;
  gunichar wc;

  while (p < string->str + string->len) {
    gboolean safe;

    wc = g_utf8_get_char_validated (p, -1);
    if (wc == (gunichar) - 1 || wc == (gunichar) - 2) {
      gchar *tmp;
      guint pos;

      pos = p - string->str;

      /* Emit invalid UTF-8 as hex escapes 
       */
      tmp = g_strdup_printf ("\\x%02x", (guint) (guchar) * p);
      g_string_erase (string, pos, 1);
      g_string_insert (string, pos, tmp);

      p = string->str + (pos + 4);      /* Skip over escape sequence */

      g_free (tmp);
      continue;
    }
    if (wc == '\r') {
      safe = *(p + 1) == '\n';
    } else {
      safe = CHAR_IS_SAFE (wc);
    }

    if (!safe) {
      gchar *tmp;
      guint pos;

      pos = p - string->str;

      /* Largest char we escape is 0x0a, so we don't have to worry
       * about 8-digit \Uxxxxyyyy
       */
      tmp = g_strdup_printf ("\\u%04x", wc);
      g_string_erase (string, pos, g_utf8_next_char (p) - p);
      g_string_insert (string, pos, tmp);
      g_free (tmp);

      p = string->str + (pos + 6);      /* Skip over escape sequence */
    } else
      p = g_utf8_next_char (p);
  }
}

void
priv_glib_log_handler (const gchar * log_domain, GLogLevelFlags log_level,
    const gchar * message, gpointer user_data)
{
  gchar *string;
  GString *gstring;
  const gchar *domains;
  const gchar *level;
  gchar *tag;

  if ((log_level & DEFAULT_LEVELS) || (log_level >> G_LOG_LEVEL_USER_SHIFT))
    goto emit;

  domains = g_getenv ("G_MESSAGES_DEBUG");
  if (((log_level & INFO_LEVELS) == 0) ||
      domains == NULL ||
      (strcmp (domains, "all") != 0 && (!log_domain
              || !strstr (domains, log_domain))))
    return;

emit:

  switch (log_level & G_LOG_LEVEL_MASK) {
    case G_LOG_LEVEL_ERROR:
      level = "ERROR";
      break;
    case G_LOG_LEVEL_CRITICAL:
      level = "CRITICAL";
      break;
    case G_LOG_LEVEL_WARNING:
      level = "WARNING";
      break;
    case G_LOG_LEVEL_MESSAGE:
      level = "MESSAGE";
      break;
    case G_LOG_LEVEL_INFO:
      level = "INFO";
      break;
    case G_LOG_LEVEL_DEBUG:
      level = "DEBUG";
      break;
    default:
      level = "DEBUG";
      break;
  }

  if (log_domain)
    tag = g_strdup_printf ("GLib+%s (%s)", log_domain, level);
  else
    tag = g_strdup_printf ("GLib (%s)", level);

  gstring = g_string_new (NULL);

  if (!message) {
    g_string_append (gstring, "(NULL) message");
  } else {
    GString *msg = g_string_new (message);
    escape_string (msg);
    g_string_append (gstring, msg->str);
    g_string_free (msg, TRUE);
  }
  string = g_string_free (gstring, FALSE);

  send_debug (tag, string);

  g_free (string);
  g_free (tag);
}

static GstClockTime start_time;

void
priv_gst_debug_logcat (GstDebugCategory * category, GstDebugLevel level,
    const gchar * file, const gchar * function, gint line,
    GObject * object, GstDebugMessage * message, gpointer unused)
{
  GstClockTime elapsed;
  const gchar *level_str;
  gchar *tag, *m;

  if (level > gst_debug_category_get_threshold (category))
    return;

  elapsed = GST_CLOCK_DIFF (start_time, gst_util_get_timestamp ());

  switch (level) {
    case GST_LEVEL_ERROR:
      level_str = "ERROR";
      break;
    case GST_LEVEL_WARNING:
      level_str = "WARNING";
      break;
    case GST_LEVEL_INFO:
      level_str = "INFO";
      break;
    case GST_LEVEL_DEBUG:
      level_str = "DEBUG";
      break;
    default:
      level_str = "OTHER";
      break;
  }

  tag = g_strdup_printf ("GStreamer+%s (%s)",
      gst_debug_category_get_name (category), level_str);

  if (object) {
    gchar *obj;

    if (GST_IS_PAD (object) && GST_OBJECT_NAME (object)) {
      obj = g_strdup_printf ("<%s:%s>", GST_DEBUG_PAD_NAME (object));
    } else if (GST_IS_OBJECT (object) && GST_OBJECT_NAME (object)) {
      obj = g_strdup_printf ("<%s>", GST_OBJECT_NAME (object));
    } else if (G_IS_OBJECT (object)) {
      obj = g_strdup_printf ("<%s@%p>", G_OBJECT_TYPE_NAME (object), object);
    } else {
      obj = g_strdup_printf ("<%p>", object);
    }

    m = g_strdup_printf ("%" GST_TIME_FORMAT " %p %s:%d:%s:%s %s",
        GST_TIME_ARGS (elapsed), g_thread_self (), file, line, function, obj,
        gst_debug_message_get (message));
    g_free (obj);
  } else {
    m = g_strdup_printf ("%" GST_TIME_FORMAT " %p %s:%d:%s %s\n",
        GST_TIME_ARGS (elapsed), g_thread_self (),
        file, line, function, gst_debug_message_get (message));
  }
  send_debug (tag, m);
  g_free (tag);
  g_free (m);
}

static void
set_message (GstLaunchRemote * self, const gchar * format, ...)
{
  gchar *message;
  va_list args;

  if (!self->app_context.set_message) {
    return;
  }

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  self->app_context.set_message (message, self->app_context.app);

  g_free (message);
}

static gboolean
update_position_cb (GstLaunchRemote * self)
{
  gint64 duration = 0, position = 0;

  if (!self || !self->app_context.set_current_position)
    return TRUE;

  if (self->pipeline) {
    if (!gst_element_query_duration (self->pipeline, GST_FORMAT_TIME,
            &duration)) {
      GST_WARNING ("Could not query current duration");
    }

    if (!gst_element_query_position (self->pipeline, GST_FORMAT_TIME,
            &position)) {
      GST_WARNING ("Could not query current position");
    }

    position = MAX (position, 0);
    duration = MAX (duration, 0);
  }

  self->app_context.set_current_position (position / GST_MSECOND,
      duration / GST_MSECOND, self->app_context.app);

  return TRUE;
}

static void
error_cb (GstBus * bus, GstMessage * msg, GstLaunchRemote * self)
{
  GError *err;
  gchar *debug_info;

  gst_message_parse_error (msg, &err, &debug_info);
  set_message (self, "Error received from element %s: %s",
      GST_OBJECT_NAME (msg->src), err->message);
  g_clear_error (&err);
  g_free (debug_info);

  self->target_state = GST_STATE_NULL;
  gst_element_set_state (self->pipeline, GST_STATE_NULL);
  gst_object_unref (self->pipeline);
  if (self->video_sink)
    gst_object_unref (self->video_sink);
  self->pipeline = NULL;
  self->video_sink = NULL;
}

static void
eos_cb (GstBus * bus, GstMessage * msg, GstLaunchRemote * self)
{
  self->target_state = GST_STATE_NULL;
  gst_element_set_state (self->pipeline, GST_STATE_NULL);
  gst_object_unref (self->pipeline);
  if (self->video_sink)
    gst_object_unref (self->video_sink);
  self->pipeline = NULL;
  self->video_sink = NULL;
}

static void
buffering_cb (GstBus * bus, GstMessage * msg, GstLaunchRemote * self)
{
  gint percent;

  if (self->is_live)
    return;

  gst_message_parse_buffering (msg, &percent);
  if (percent < 100 && self->target_state >= GST_STATE_PAUSED) {
    gst_element_set_state (self->pipeline, GST_STATE_PAUSED);
    set_message (self, "Buffering %d%%", percent);
  } else if (self->target_state >= GST_STATE_PLAYING) {
    gst_element_set_state (self->pipeline, GST_STATE_PLAYING);
  } else if (self->target_state >= GST_STATE_PAUSED) {
    set_message (self, "Buffering complete");
  }
}

static void
clock_lost_cb (GstBus * bus, GstMessage * msg, GstLaunchRemote * self)
{
  if (self->target_state >= GST_STATE_PLAYING) {
    gst_element_set_state (self->pipeline, GST_STATE_PAUSED);
    gst_element_set_state (self->pipeline, GST_STATE_PLAYING);
  }
}

static void
check_media_size (GstLaunchRemote * self)
{
  GstPad *video_sink_pad;
  GstCaps *caps;
  GstVideoInfo info;

  if (!self->video_sink || !self->app_context.media_size_changed)
    return;

  /* Retrieve the Caps at the entrance of the video sink */
  video_sink_pad = gst_element_get_static_pad (self->video_sink, "sink");
  if (!video_sink_pad)
    return;

  caps = gst_pad_get_current_caps (video_sink_pad);

  if (gst_video_info_from_caps (&info, caps)) {
    info.width = info.width * info.par_n / info.par_d;
    GST_DEBUG ("Media size is %dx%d, notifying application", info.width,
        info.height);

    self->app_context.media_size_changed (info.width, info.height,
        self->app_context.app);
  }

  gst_caps_unref (caps);
  gst_object_unref (video_sink_pad);
}

static void
notify_caps_cb (GObject * object, GParamSpec * pspec, GstLaunchRemote * self)
{
  check_media_size (self);
}

static void
sync_message_cb (GstBus * bus, GstMessage * msg, GstLaunchRemote * self)
{
  if (gst_is_video_overlay_prepare_window_handle_message (msg)) {
    GstElement *element = GST_ELEMENT (GST_MESSAGE_SRC (msg));
    GstPad *sinkpad;

    /* Store video sink for later usage and set window on it if we have one */
    gst_object_replace ((GstObject **) & self->video_sink,
        (GstObject *) element);

    sinkpad = gst_element_get_static_pad (element, "sink");
    if (!sinkpad) {
      sinkpad = gst_element_get_static_pad (element, "video_sink");
    }

    if (sinkpad) {
      g_signal_connect (sinkpad, "notify::caps", (GCallback) notify_caps_cb,
          self);
      gst_object_unref (sinkpad);
    }

    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (element),
        (guintptr) self->window_handle);
  }
}

/* Notify UI about pipeline state changes */
static void
state_changed_cb (GstBus * bus, GstMessage * msg, GstLaunchRemote * self)
{
  GstState old_state, new_state, pending_state;

  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  /* Only pay attention to messages coming from the pipeline, not its children */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (self->pipeline)) {
    set_message (self, "State changed to %s",
        gst_element_state_get_name (new_state));

    /* The Ready to Paused state change is particularly interesting: */
    if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
      /* By now the sink already knows the media size */
      check_media_size (self);
    }
  }
}

/* Check if all conditions are met to report GStreamer as initialized.
 * These conditions will change depending on the application */
static void
check_initialization_complete (GstLaunchRemote * self)
{
  if (!self->initialized && self->window_handle && self->main_loop) {
    GST_DEBUG
        ("Initialization complete, notifying application. window handle: %p",
        (gpointer) self->window_handle);

    if (self->app_context.initialized)
      self->app_context.initialized (self->app_context.app);
    self->initialized = TRUE;
  }
}

static void
handle_eof (GstLaunchRemote * self)
{
  g_object_unref (self->distream);
  g_object_unref (self->connection);
  self->connection = NULL;
}

static void
read_line_cb (GObject * source_object, GAsyncResult * res, gpointer user_data)
{
  GDataInputStream *distream = G_DATA_INPUT_STREAM (source_object);
  GstLaunchRemote *self = user_data;
  gchar *line, *outline;
  gsize length, bytes_written;
  GError *err = NULL;

  line = g_data_input_stream_read_line_finish (distream, res, &length, &err);
  if (!line) {
    if (err) {
      GST_ERROR ("ERROR: Reading line: %s", err->message);
    } else {
      GST_WARNING ("EOF");
    }
    g_clear_error (&err);
    handle_eof (self);
    return;
  }

  GST_DEBUG ("Received command: %s", line);
  if (line) {
    gboolean ok = TRUE;

    if (g_str_has_prefix (line, "+DEBUG ")) {
      gchar *address = line + sizeof ("+DEBUG ") - 1;
      gchar *colon = strchr (address, ':');

      ok = FALSE;
      if (colon) {
        gint port = strtol (colon + 1, NULL, 10);

        if (port > 0) {
          GSocketAddress *addr;
          *colon = '\0';

          addr = g_inet_socket_address_new_from_string (address, port);
          if (addr) {
            GList *l;

            G_LOCK (debug_sockets);
            for (l = debug_sockets; l; l = l->next) {
              DebugSocket *s = l->data;

              if (s->socket == self->debug_socket) {
                ok = TRUE;
                s->address = addr;
                gst_debug_set_active (TRUE);
                gst_debug_set_default_threshold (GST_LEVEL_DEBUG);
                break;
              }
            }
            G_UNLOCK (debug_sockets);
          }
        }
      }
    } else if (g_str_has_prefix (line, "-DEBUG")) {
      GList *l;
      gboolean all_disabled = TRUE;

      G_LOCK (debug_sockets);
      for (l = debug_sockets; l; l = l->next) {
        DebugSocket *s = l->data;

        if (s->socket == self->debug_socket) {
          if (s->address)
            g_object_unref (s->address);
          s->address = NULL;
        }

        all_disabled &= s->address == NULL;
      }
      G_UNLOCK (debug_sockets);
      gst_debug_set_active (!all_disabled);
      if (!all_disabled)
        gst_debug_set_default_threshold (GST_LEVEL_DEBUG);
    } else if (g_str_has_prefix (line, "+PLAY")) {
      gst_launch_remote_play (self);
    } else if (g_str_has_prefix (line, "+PAUSE")) {
      gst_launch_remote_pause (self);
    } else if (g_str_has_prefix (line, "+SEEK ")) {
      gchar *position = line + sizeof ("+SEEK ") - 1;
      gchar *endptr = NULL;
      guint64 ms = g_ascii_strtoull (position, &endptr, 10);

      if (*endptr == '\0') {
        gst_launch_remote_seek (self, ms);
      } else {
        ok = FALSE;
      }
    } else if (g_str_has_prefix (line, "+NETCLOCK ")) {
      gchar **command;

      if (*(line + sizeof ("+NETCLOCK") - 1) == '\0')
        command = g_new0 (gchar *, 1);
      else
        command = g_strsplit (line + sizeof ("+NETCLOCK"), " ", 2);

      if (self->net_clock)
        gst_object_unref (self->net_clock);
      self->net_clock = NULL;
      if (command[0] && command[1]) {
        gint64 port = g_ascii_strtoll (command[1], NULL, 10);
        GST_DEBUG ("Setting netclock %s %" G_GINT64_FORMAT, command[0], port);
        self->net_clock = gst_net_client_clock_new ("netclock", command[0], port, 0);
      } else {
        GST_DEBUG ("Unsetting netclock");
      }

      g_strfreev (command);
    } else if (g_str_has_prefix (line, "+BASETIME ")) {
      gchar *endptr = NULL;
      guint64 base_time = g_ascii_strtoull (line + sizeof ("+BASETIME"), &endptr, 10);

      if (*endptr != '\0') {
        ok = FALSE;
        self->base_time = GST_CLOCK_TIME_NONE;
      } else {
        self->base_time = base_time;
        GST_DEBUG ("Setting base time %" GST_TIME_FORMAT, GST_TIME_ARGS (base_time));
        if (self->pipeline) {
          gst_element_set_base_time (self->pipeline, base_time);
          gst_element_set_start_time (self->pipeline, GST_CLOCK_TIME_NONE);
        }
      }
    } else if (g_str_has_prefix (line, "+STAT")) {
      GstClockTime position = -1, duration = -1;
      gchar *tmp;
      GstState s;

      gst_element_query_duration (self->pipeline, GST_FORMAT_TIME, &duration);
      gst_element_query_position (self->pipeline, GST_FORMAT_TIME, &position);
      s = GST_STATE (self->pipeline);

      tmp = g_strdup_printf ("%" GST_TIME_FORMAT " / %" GST_TIME_FORMAT " @ %s\n", GST_TIME_ARGS (position), GST_TIME_ARGS (duration), gst_element_state_get_name (s));
      g_output_stream_write_all (self->ostream, tmp, strlen (tmp), NULL, NULL, NULL);
      g_free (tmp);
    } else if (!g_str_has_prefix (line, "+") && !g_str_has_prefix (line, "-")) {
      gst_launch_remote_set_pipeline (self, line);
    } else {
      ok = FALSE;
    }

    if (ok)
      outline = g_strdup ("OK\n");
    else
      outline = g_strdup ("NOK\n");
  } else {
    outline = g_strdup ("NOK\n");
  }
  g_free (line);

  if (!g_output_stream_write_all (self->ostream, outline, strlen (outline),
          &bytes_written, NULL, &err)) {
    GST_ERROR ("ERROR: Writing line: %s", err->message);

    g_free (outline);
    g_clear_error (&err);
    handle_eof (self);
    return;
  }
  g_free (outline);

  g_data_input_stream_read_line_async (distream, 0, NULL, read_line_cb, self);
}

static gboolean
incoming_cb (GSocketService * service, GSocketConnection * connection,
    GObject * source_object, gpointer user_data)
{
  GstLaunchRemote *self = user_data;
  GIOStream *stream;
  GInputStream *istream;

  if (self->connection) {
    GST_ERROR ("ERROR: Already have a connection\n");
    return FALSE;
  }

  self->connection = g_object_ref (connection);
  stream = G_IO_STREAM (connection);
  istream = g_io_stream_get_input_stream (stream);
  self->distream = g_data_input_stream_new (istream);
  self->ostream = g_io_stream_get_output_stream (stream);

  g_data_input_stream_read_line_async (self->distream, 0, NULL, read_line_cb,
      self);

  return TRUE;
}

static void
gst_launch_remote_set_pipeline (GstLaunchRemote * self, const gchar * pipeline_string)
{
  GstBus *bus;
  GSource *bus_source;
  GError *err = NULL;

  if (self->pipeline) {
    gst_element_set_state (self->pipeline, GST_STATE_NULL);
    gst_object_unref (self->pipeline);
    if (self->video_sink)
      gst_object_unref (self->video_sink);
    self->pipeline = NULL;
    self->video_sink = NULL;
  }

  g_free (self->pipeline_string);
  self->pipeline_string = NULL;
  self->target_state = GST_STATE_NULL;

  if (!pipeline_string)
    return;

  self->pipeline_string = g_strdup (pipeline_string);
  self->pipeline = gst_parse_launch (pipeline_string, &err);
  if (err) {
    set_message (self, "Unable to build pipeline '%s': %s", pipeline_string,
        err->message);
    g_clear_error (&err);
    return;
  }

  bus = gst_element_get_bus (self->pipeline);
  bus_source = gst_bus_create_watch (bus);
  g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func,
      NULL, NULL);
  g_source_attach (bus_source, self->context);
  g_source_unref (bus_source);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback) error_cb,
      self);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback) eos_cb, self);
  g_signal_connect (G_OBJECT (bus), "message::state-changed",
      (GCallback) state_changed_cb, self);
  g_signal_connect (G_OBJECT (bus), "message::buffering",
      (GCallback) buffering_cb, self);
  g_signal_connect (G_OBJECT (bus), "message::clock-lost",
      (GCallback) clock_lost_cb, self);

  gst_bus_enable_sync_message_emission (bus);
  g_signal_connect (G_OBJECT (bus), "sync-message", (GCallback) sync_message_cb,
      self);

  gst_object_unref (bus);

  if (self->net_clock)
    gst_pipeline_use_clock (GST_PIPELINE (self->pipeline), self->net_clock);

  if (self->base_time != GST_CLOCK_TIME_NONE) {
    gst_element_set_base_time (self->pipeline, self->base_time);
    gst_element_set_start_time (self->pipeline, GST_CLOCK_TIME_NONE);
  }
}

static gpointer
gst_launch_remote_main (gpointer user_data)
{
  GstLaunchRemote *self = user_data;
  GSource *timeout_source;
  GSocketAddress *bind_addr;
  GInetAddress *bind_iaddr;
  GError *err = NULL;

  GST_DEBUG ("GstLaunchRemote main %p", self);

  /* Create our own GLib Main Context and make it the default one */
  self->context = g_main_context_new ();
  g_main_context_push_thread_default (self->context);

  self->debug_socket =
      g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM,
      G_SOCKET_PROTOCOL_UDP, &err);
  if (!self->debug_socket) {
    GST_ERROR ("ERROR: Can't create debug socket: %s", err->message);
    g_clear_error (&err);
  } else {
    bind_iaddr = g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);
    bind_addr = g_inet_socket_address_new (bind_iaddr, 0);
    g_socket_bind (self->debug_socket, bind_addr, TRUE, &err);
    g_object_unref (bind_addr);
    g_object_unref (bind_iaddr);
    if (err != NULL) {
      GST_ERROR ("ERROR: Can't bind debug socket: %s", err->message);
      g_clear_error (&err);
      g_socket_close (self->debug_socket, NULL);
      g_object_unref (self->debug_socket);
      self->debug_socket = NULL;
    }
  }

  if (self->debug_socket) {
    DebugSocket *s = g_slice_new0 (DebugSocket);

    s->socket = self->debug_socket;
    s->address = NULL;

    G_LOCK (debug_sockets);
    debug_sockets = g_list_prepend (debug_sockets, s);
    G_UNLOCK (debug_sockets);
  }

  self->service = g_socket_service_new ();

  bind_iaddr = g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);
  bind_addr = g_inet_socket_address_new (bind_iaddr, PORT);

  if (!g_socket_listener_add_address (G_SOCKET_LISTENER
          (self->service), bind_addr, G_SOCKET_TYPE_STREAM,
          G_SOCKET_PROTOCOL_TCP, NULL, NULL, &err)) {
    GST_ERROR ("ERROR: Can't add port %d: %s", PORT, err->message);
    g_clear_error (&err);
    g_object_unref (self->service);
    self->service = NULL;
  } else {
    GST_DEBUG ("Listening on port %u", PORT);
    g_signal_connect (self->service, "incoming", G_CALLBACK (incoming_cb),
        self);
    g_socket_service_start (self->service);
  }

  g_object_unref (bind_addr);
  g_object_unref (bind_iaddr);

  gst_launch_remote_set_pipeline (self, "fakesrc ! fakesink");

  timeout_source = g_timeout_source_new (250);
  g_source_set_callback (timeout_source, (GSourceFunc) update_position_cb, self,
      NULL);
  g_source_attach (timeout_source, self->context);
  g_source_unref (timeout_source);

  GST_DEBUG ("Starting main loop");
  self->main_loop = g_main_loop_new (self->context, FALSE);
  check_initialization_complete (self);
  g_main_loop_run (self->main_loop);
  GST_DEBUG ("Exited main loop");
  g_main_loop_unref (self->main_loop);
  self->main_loop = NULL;

  if (self->service) {
    g_socket_service_stop (self->service);
    g_object_unref (self->service);
  }

  if (self->connection) {
    g_object_unref (self->distream);
    g_object_unref (self->connection);
  }

  if (self->debug_socket) {
    GList *l;

    G_LOCK (debug_sockets);
    for (l = debug_sockets; l; l = l->next) {
      DebugSocket *s = l->data;

      if (s->socket == self->debug_socket) {
        debug_sockets = g_list_remove_link (debug_sockets, l);
        if (s->address)
          g_object_unref (s->address);
        g_slice_free (DebugSocket, s);
        break;
      }
    }
    G_UNLOCK (debug_sockets);
    g_socket_close (self->debug_socket, NULL);
    g_object_unref (self->debug_socket);
  }

  /* Free resources */
  g_main_context_pop_thread_default (self->context);
  g_main_context_unref (self->context);
  self->target_state = GST_STATE_NULL;
  if (self->pipeline) {
    gst_element_set_state (self->pipeline, GST_STATE_NULL);
    gst_object_unref (self->pipeline);
    if (self->video_sink)
      gst_object_unref (self->video_sink);
    self->pipeline = NULL;
    self->video_sink = NULL;
  }
  g_free (self->pipeline_string);

  return NULL;
}

static gpointer
gst_launch_remote_init (gpointer user_data)
{
  GST_DEBUG_CATEGORY_INIT (debug_category, "gst-launch-remote", 0, "GstLaunchRemote");
  gst_debug_set_threshold_for_name ("gst-launch-remote", GST_LEVEL_DEBUG);

  g_set_print_handler (priv_glib_print_handler);
  g_set_printerr_handler (priv_glib_printerr_handler);
  g_log_set_default_handler (priv_glib_log_handler, NULL);

  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_remove_log_function_by_data (NULL);
  gst_debug_add_log_function ((GstLogFunction) priv_gst_debug_logcat, NULL,
      NULL);

  gst_debug_set_active (FALSE);

  start_time = gst_util_get_timestamp ();

  return NULL;
}

GstLaunchRemote *
gst_launch_remote_new (const GstLaunchRemoteAppContext * ctx)
{
  GstLaunchRemote *self = g_slice_new0 (GstLaunchRemote);
  static GOnce once = G_ONCE_INIT;

  g_once (&once, gst_launch_remote_init, NULL);

  self->app_context = *ctx;
  self->thread = g_thread_new ("gst-launch-remote", gst_launch_remote_main, self);

  return self;
}

void
gst_launch_remote_free (GstLaunchRemote * self)
{
  g_main_loop_quit (self->main_loop);
  g_thread_join (self->thread);
  g_slice_free (GstLaunchRemote, self);
}

void
gst_launch_remote_play (GstLaunchRemote * self)
{
  GstStateChangeReturn state_ret;

  if (!self || !self->pipeline_string)
    return;

  if (!self->pipeline) {
    gchar *pipeline_string = g_strdup (self->pipeline_string);
    gst_launch_remote_set_pipeline (self, pipeline_string);
    g_free (pipeline_string);
  }
  GST_DEBUG ("Setting state to PLAYING");

  self->target_state = GST_STATE_PLAYING;
  state_ret = gst_element_set_state (self->pipeline, GST_STATE_PLAYING);
  self->is_live = (state_ret == GST_STATE_CHANGE_NO_PREROLL);

  if (state_ret == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR ("Failed to set pipeline to PLAYING");
    set_message (self, "Failed to set pipeline to PLAYING");
  }
}

void
gst_launch_remote_pause (GstLaunchRemote * self)
{
  GstStateChangeReturn state_ret;

  if (!self || !self->pipeline_string)
    return;

  if (!self->pipeline) {
    gchar *pipeline_string = g_strdup (self->pipeline_string);
    gst_launch_remote_set_pipeline (self, pipeline_string);
    g_free (pipeline_string);
  }

  GST_DEBUG ("Setting state to PAUSED");

  self->target_state = GST_STATE_PAUSED;
  state_ret = gst_element_set_state (self->pipeline, GST_STATE_PAUSED);
  self->is_live = (state_ret == GST_STATE_CHANGE_NO_PREROLL);

  if (state_ret == GST_STATE_CHANGE_FAILURE) {
    GST_ERROR ("Failed to set pipeline to PAUSED");
    set_message (self, "Failed to set pipeline to PAUSED");
  }
}

void
gst_launch_remote_seek (GstLaunchRemote * self, gint position_ms)
{
  GstClockTime position;

  if (!self || !self->pipeline)
    return;

  position = gst_util_uint64_scale (position_ms, GST_MSECOND, 1);
  GST_DEBUG ("Seeking to %" GST_TIME_FORMAT, GST_TIME_ARGS (position));

  if (!gst_element_seek_simple (self->pipeline, GST_FORMAT_TIME,
          GST_SEEK_FLAG_FLUSH, position)) {
    GST_ERROR ("Seeking failed");
    set_message (self, "Seeking failed");
  } else {
    GST_DEBUG ("Seek successful");
  }
}

void
gst_launch_remote_set_window_handle (GstLaunchRemote * self, guintptr handle)
{
  if (!self)
    return;

  GST_DEBUG ("Received window handle %p", (gpointer) handle);

  if (self->window_handle) {
    if (self->window_handle == handle) {
      GST_DEBUG ("New window handle is the same as the previous one");
      if (self->video_sink) {
        gst_video_overlay_expose (GST_VIDEO_OVERLAY (self->video_sink));
      }
      return;
    } else {
      GST_DEBUG ("Released previous window handle %p",
          (gpointer) self->window_handle);
      self->initialized = FALSE;
    }
  }

  self->window_handle = handle;

  if (!self->window_handle) {
    if (self->video_sink) {
      gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (self->video_sink),
          (guintptr) NULL);
      gst_element_set_state (self->pipeline, GST_STATE_NULL);
      gst_object_unref (self->pipeline);
      if (self->video_sink)
        gst_object_unref (self->video_sink);
      self->pipeline = NULL;
      self->video_sink = NULL;
    }
  }

  check_initialization_complete (self);
}
