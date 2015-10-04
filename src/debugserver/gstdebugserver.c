/* GStreamer
 * Copyright (C) 2015 Marcin Kolny <marcin.kolny@gmail.com>
 *
 * gstdebugserver.c: tracing module that sends serialized data to
 * an user.
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
/**
 * SECTION:gstdebugserver
 * @short_description: communicate with user via tcp/ip protocol
 *
 * A tracing module that communicates with user via tcp/ip.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstdebugserver.h"

#include "gstdebugservertopology.h"

#include "../common/gst-utils.h"
#include "common/serializer.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_debugserver_debug);
#define GST_CAT_DEFAULT gst_debugserver_debug
#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_debugserver_debug, "debugserver", 0, "debugserver tracer");
#define gst_debugserver_tracer_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDebugserverTracer, gst_debugserver_tracer,
    GST_TYPE_TRACER, _do_init)

#define DEFAULT_PORT 8080

enum
{
  PROP_0,
  PROP_PORT
};

static void gst_debugserver_tracer_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_debugserver_tracer_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_debugserver_command_handler (GstDebugger__Command * command,
    gpointer debugtracer, TcpClient * client);

static void gst_debugserver_tracer_finalize (GObject * obj);

static void
gst_debugserver_log_handler (GstDebugCategory *category, GstDebugLevel level,
  const gchar *file, const gchar *function, gint line, GObject *object,
  GstDebugMessage *message, gpointer user_data) G_GNUC_NO_INSTRUMENT;

static void
gst_debugserver_log_handler (GstDebugCategory *category, GstDebugLevel level,
  const gchar *file, const gchar *function, gint line, GObject *object,
  GstDebugMessage *message, gpointer user_data)
{
  GstDebugserverTracer *tracer = GST_DEBUGSERVER_TRACER (user_data);

  gst_debugserver_log_send_log (tracer->log, tracer->tcp_server, category, level,
    file, function, line, object, message);
}

static void
message_broadcaster (GstBus * bus, GstMessage * message, gpointer user_data)
{
  GstDebugserverTracer *self = GST_DEBUGSERVER_TRACER (user_data);

  gst_debugserver_message_send_message (self->message, self->tcp_server, message);
}

static void
do_element_new (GstTracer * self, guint64 ts, GstElement * element)
{
  GstBus *bus;
  GstDebugserverTracer *debugserver = GST_DEBUGSERVER_TRACER (self);
  if (GST_IS_PIPELINE (element)) {
    debugserver->pipeline = GST_PIPELINE (element);
    bus = gst_pipeline_get_bus (debugserver->pipeline);
    gst_bus_add_signal_watch (bus);
    g_signal_connect (bus, "message", G_CALLBACK (message_broadcaster), debugserver);
  }
}

static void
do_pad_unlink_post (GstTracer * self, guint64 ts, GstPad * src, GstPad * sink, gboolean result)
{
  if (result == TRUE) {
    gst_debugserver_topology_send_pad_link (src, sink, FALSE, GST_DEBUGSERVER_TRACER (self)->tcp_server, NULL);
  }
}

static void
do_pad_link_post (GstTracer * self, guint64 ts, GstPad * src, GstPad * sink, GstPadLinkReturn result)
{
  if (result == TRUE) {
    gst_debugserver_topology_send_pad_link (src, sink, TRUE, GST_DEBUGSERVER_TRACER (self)->tcp_server, NULL);
  }
}

static void
do_bin_add_post (GstTracer * self, gint64 ts, GstBin * bin, GstElement * element, gboolean result)
{
  if (result == TRUE) {
    gst_debugserver_topology_send_element_in_bin (bin, element, TRUE, GST_DEBUGSERVER_TRACER (self)->tcp_server, NULL);
  }
}

static void
do_bin_remove_pre (GstTracer * self, gint64 ts, GstBin * bin, GstElement * element)
{
  gst_debugserver_topology_send_element_in_bin (bin, element, FALSE, GST_DEBUGSERVER_TRACER (self)->tcp_server, NULL);
}

static void
do_element_add_pad (GstTracer * self, gint64 ts, GstElement * element, GstPad * pad)
{
  gst_debugserver_topology_send_pad_in_element (element, pad, TRUE, GST_DEBUGSERVER_TRACER (self)->tcp_server, NULL);
}

static void
do_element_remove_pad (GstTracer * self, gint64 ts, GstElement * element, GstPad * pad)
{
  gst_debugserver_topology_send_pad_in_element (element, pad, FALSE, GST_DEBUGSERVER_TRACER (self)->tcp_server, NULL);
}

static void
do_push_event_pre (GstTracer * self, guint64 ts, GstPad * pad, GstEvent * event)
{
  GstDebugserverTracer *tracer = GST_DEBUGSERVER_TRACER (self);

  gst_debugserver_qe_send_qe (tracer->event, tracer->tcp_server, pad, GST_MINI_OBJECT_CAST (event));
}

static void
do_pad_query_pre (GstTracer * self, guint64 ts, GstPad * pad, GstQuery * query)
{
  GstDebugserverTracer *tracer = GST_DEBUGSERVER_TRACER (self);

  gst_debugserver_qe_send_qe (tracer->query, tracer->tcp_server, pad, GST_MINI_OBJECT_CAST (query));
}

static void
do_pad_push_pre (GstTracer * self, guint64 ts, GstPad * pad, GstBuffer * buffer)
{
  GstDebugserverTracer *tracer = GST_DEBUGSERVER_TRACER (self);

  gst_debugserver_buffer_send_buffer (tracer->buffer, tracer->tcp_server, pad, buffer);

}

static void
gst_debugserver_tracer_client_disconnected (TcpClient * client, gpointer user_data)
{
  GstDebugserverTracer *self = GST_DEBUGSERVER_TRACER (user_data);

  gst_debugserver_log_remove_client (self->log, client);
  gst_debugserver_message_remove_client (self->message, client);
  gst_debugserver_qe_remove_client (self->event, client);
  gst_debugserver_qe_remove_client (self->query, client);
  gst_debugserver_buffer_remove_client (self->buffer, client);
}

static void
gst_debugserver_tracer_class_init (GstDebugserverTracerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = gst_debugserver_tracer_set_property;
  gobject_class->get_property = gst_debugserver_tracer_get_property;
  gobject_class->finalize = gst_debugserver_tracer_finalize;

  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_int ("port", "Port", "Server port",
          0, 65535, DEFAULT_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_debugserver_tracer_send_property (GstDebugserverTcp * tcp_server, TcpClient * client, GParamSpec * spec, GstElement * element)
{
  GstDebugger__GStreamerData gst_data = GST_DEBUGGER__GSTREAMER_DATA__INIT;
  GstDebugger__PropertyValue property = GST_DEBUGGER__PROPERTY_VALUE__INIT;
  GstDebugger__Value value = GST_DEBUGGER__VALUE__INIT;
  GType out_gtype;
  InternalGType out_internal_type;
  GValue gvalue = G_VALUE_INIT;
  gchar *object = gst_utils_get_object_path (GST_OBJECT_CAST (element));
  gchar *serialized_data = NULL;

  if (spec == NULL) {
    return;
  }

  property.name = (gchar*) g_param_spec_get_name (spec);
  property.object = (gchar*) object;

  g_value_init (&gvalue, spec->value_type);
  g_object_get_property (G_OBJECT (element), g_param_spec_get_name (spec), &gvalue);

  serialized_data = g_value_serialize (&gvalue, &out_gtype, &out_internal_type);

  value.data.data = serialized_data;
  value.data.len = serialized_data == NULL ? 0 : strlen (serialized_data);
  value.gtype = out_gtype;
  value.type_name = (gchar*) g_type_name (spec->value_type);

  if (out_gtype != spec->value_type) {
    value.internal_type = out_internal_type;
    value.has_internal_type = TRUE;
  } else {
    value.has_internal_type = FALSE;
  }

  property.value = &value;
  gst_data.property_value = &property;
  gst_data.info_type_case = GST_DEBUGGER__GSTREAMER_DATA__INFO_TYPE_PROPERTY_VALUE;

  gst_debugserver_tcp_send_packet (tcp_server, client, &gst_data);

  g_value_unset (&gvalue);
  g_free (serialized_data);
  g_free (object);
}

static void
gst_debugserver_tracer_init (GstDebugserverTracer * self)
{
  GstTracer *tracer = GST_TRACER (self);

  self->port = DEFAULT_PORT;

  self->log = gst_debugserver_log_new ();
  gst_debug_add_log_function (gst_debugserver_log_handler, self, NULL);

  self->tcp_server = gst_debugserver_tcp_new ();
  self->tcp_server->command_handler = gst_debugserver_command_handler;
  self->tcp_server->client_disconnected_handler = gst_debugserver_tracer_client_disconnected;
  self->tcp_server->owner = self;
  gst_debugserver_tcp_start_server (self->tcp_server, self->port);

  self->message = gst_debugserver_message_new ();

  self->event = gst_debugserver_qe_new ();

  self->query = gst_debugserver_qe_new ();

  self->buffer = gst_debugserver_buffer_new ();

  gst_tracing_register_hook (tracer, "element-new",
      G_CALLBACK (do_element_new));
  gst_tracing_register_hook (tracer, "pad-push-event-pre",
      G_CALLBACK (do_push_event_pre));
  gst_tracing_register_hook (tracer, "pad-query-pre",
      G_CALLBACK (do_pad_query_pre));
  gst_tracing_register_hook (tracer, "pad-push-pre",
      G_CALLBACK (do_pad_push_pre));
  gst_tracing_register_hook (tracer, "element-add-pad",
      G_CALLBACK (do_element_add_pad));
  gst_tracing_register_hook (tracer, "element-remove-pad",
      G_CALLBACK (do_element_remove_pad));
  gst_tracing_register_hook (tracer, "bin-add-post",
      G_CALLBACK (do_bin_add_post));
  gst_tracing_register_hook (tracer, "bin-remove-pre",
      G_CALLBACK (do_bin_remove_pre));
  gst_tracing_register_hook (tracer, "pad-link-post",
      G_CALLBACK (do_pad_link_post));
  gst_tracing_register_hook (tracer, "pad-unlink-post",
      G_CALLBACK (do_pad_unlink_post));
}

static void
gst_debugserver_tracer_finalize (GObject * obj)
{
  GstDebugserverTracer *self = GST_DEBUGSERVER_TRACER (obj);

  gst_object_unref (GST_DEBUGSERVER_TCP (self->tcp_server));

  gst_debug_remove_log_function (gst_debugserver_log_handler);
  gst_debugserver_log_free (self->log);
  gst_debugserver_message_free (self->message);
  gst_debugserver_qe_free (self->event);
  gst_debugserver_qe_free (self->query);
  gst_debugserver_buffer_free (self->buffer);
}

static void
gst_debugserver_tracer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDebugserverTracer *self = GST_DEBUGSERVER_TRACER (object);
  gint tmp_port;

  switch (prop_id) {
    case PROP_PORT:
      tmp_port = g_value_get_int (value);
      if (tmp_port != self->port) {
        self->port = g_value_get_int (value);
        gst_debugserver_tcp_stop_server (self->tcp_server);
        gst_debugserver_tcp_start_server (self->tcp_server, self->port);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_debugserver_tracer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDebugserverTracer *debugserver;

  debugserver = GST_DEBUGSERVER_TRACER (object);

  switch (prop_id) {
    case PROP_PORT:
      g_value_set_int (value, debugserver->port);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void gst_debugserver_process_pad_hook (GstDebugserverTracer * self, GstDebugger__Command * command, TcpClient * client)
{
  GstDebugger__PadHookRequest *request = command->pad_hook;
  GstPad *pad = gst_utils_get_pad_from_path (GST_ELEMENT_CAST (self->pipeline), request->pad);

  switch (request->pad_hook_type_case)
  {
  case GST_DEBUGGER__PAD_HOOK_REQUEST__PAD_HOOK_TYPE_EVENT:
    if (gst_debugserver_qe_set_hook (self->event, request->action == GST_DEBUGGER__ACTION__ADD, request->event->type, pad, request->pad, client)) {
      GstDebugger__GStreamerData data = GST_DEBUGGER__GSTREAMER_DATA__INIT;
      data.confirmation = command;
      data.info_type_case = GST_DEBUGGER__GSTREAMER_DATA__INFO_TYPE_CONFIRMATION;
      gst_debugserver_tcp_send_packet (self->tcp_server, client, &data);
    }
    break;
  case GST_DEBUGGER__PAD_HOOK_REQUEST__PAD_HOOK_TYPE_QUERY:
    if (gst_debugserver_qe_set_hook (self->query, request->action == GST_DEBUGGER__ACTION__ADD, request->query->type, pad, request->pad, client)) {
      GstDebugger__GStreamerData data = GST_DEBUGGER__GSTREAMER_DATA__INIT;
      data.confirmation = command;
      data.info_type_case = GST_DEBUGGER__GSTREAMER_DATA__INFO_TYPE_CONFIRMATION;
      gst_debugserver_tcp_send_packet (self->tcp_server, client, &data);
    }
    break;
  case GST_DEBUGGER__PAD_HOOK_REQUEST__PAD_HOOK_TYPE_BUFFER:
    if (gst_debugserver_buffer_set_hook (self->buffer, request->action == GST_DEBUGGER__ACTION__ADD, request->buffer->send_data, pad,
        request->pad, client)) {
      GstDebugger__GStreamerData data = GST_DEBUGGER__GSTREAMER_DATA__INIT;
      data.confirmation = command;
      data.info_type_case = GST_DEBUGGER__GSTREAMER_DATA__INFO_TYPE_CONFIRMATION;
      gst_debugserver_tcp_send_packet (self->tcp_server, client, &data);
    }
    break;
  }
}

static void gst_debugserver_send_single_property (GstDebugserverTracer * self, TcpClient * client, GstElement * element, const gchar * name)
{
  GParamSpec *spec = g_object_class_find_property (G_OBJECT_CLASS (GST_ELEMENT_GET_CLASS (element)), name);

  gst_debugserver_tracer_send_property (self->tcp_server, client, spec, element);
}

static void gst_debugserver_process_property_request (GstDebugserverTracer * self, TcpClient * client,
  GstDebugger__PropertyRequest * request)
{
  GstElement * element = gst_utils_get_element_from_path (GST_ELEMENT_CAST (self->pipeline), request->object);
  gint n_properties, i;
  GParamSpec **spec;

  if (element == NULL) {
    return;
  }

  if (request->name == NULL || request->name[0] == '\0') {
    spec = g_object_class_list_properties (G_OBJECT_CLASS (GST_ELEMENT_GET_CLASS (element)), &n_properties);

    for (i = 0; i < n_properties; i++) {
      gst_debugserver_send_single_property (self, client, element, spec[i]->name);
    }
  } else {
    gst_debugserver_send_single_property (self, client, element, request->name);
  }
}

static void gst_debugserver_command_handler (GstDebugger__Command * command,
    gpointer debugtracer, TcpClient * client)
{
  GstDebugserverTracer *self = GST_DEBUGSERVER_TRACER (debugtracer);
  GstDebugserverTcp * tcp = self->tcp_server;

  switch (command->command_type_case) {
  case GST_DEBUGGER__COMMAND__COMMAND_TYPE_TYPE_DESCRIPTION:
    gst_debugserver_types_send_type (tcp, client, command->type_description);
    break;
  case GST_DEBUGGER__COMMAND__COMMAND_TYPE_DEBUG_CATEGORIES_LIST:
    gst_debugserver_log_send_debug_categories (tcp, client);
    break;
  case GST_DEBUGGER__COMMAND__COMMAND_TYPE_LOG_THRESHOLD:
    gst_debugserver_log_set_threshold (command->log_threshold);
    break;
  case GST_DEBUGGER__COMMAND__COMMAND_TYPE_MESSAGE:
    if (gst_debugserver_message_set_hook (self->message, client, command->message)) {
      GstDebugger__GStreamerData data = GST_DEBUGGER__GSTREAMER_DATA__INIT;
      data.confirmation = command;
      data.info_type_case = GST_DEBUGGER__GSTREAMER_DATA__INFO_TYPE_CONFIRMATION;
      gst_debugserver_tcp_send_packet (self->tcp_server, client, &data);
    }
    break;
  case GST_DEBUGGER__COMMAND__COMMAND_TYPE_LOG:
    if (gst_debugserver_log_set_hook (self->log, command->log->action == GST_DEBUGGER__ACTION__ADD,
          command->log->level, command->log->category, client)) {
      GstDebugger__GStreamerData data = GST_DEBUGGER__GSTREAMER_DATA__INIT;
      data.confirmation = command;
      data.info_type_case = GST_DEBUGGER__GSTREAMER_DATA__INFO_TYPE_CONFIRMATION;
      gst_debugserver_tcp_send_packet (self->tcp_server, client, &data);
    }
    break;
  case GST_DEBUGGER__COMMAND__COMMAND_TYPE_PAD_HOOK:
    gst_debugserver_process_pad_hook (self, command, client);
    break;
  case GST_DEBUGGER__COMMAND__COMMAND_TYPE_ENTIRE_TOPOLOGY:
    gst_debugserver_topology_send_entire_topology (GST_BIN_CAST (self->pipeline), self->tcp_server, client);
    break;
  case GST_DEBUGGER__COMMAND__COMMAND_TYPE_PROPERTY:
    gst_debugserver_process_property_request (self, client, command->property);
    break;
  case GST_DEBUGGER__COMMAND__COMMAND_TYPE_PROPERTY_SET:
  {
    GValue val = G_VALUE_INIT;
    GstElement *element = gst_utils_get_element_from_path (GST_ELEMENT_CAST (self->pipeline), command->property_set->object);
    g_value_deserialize (&val, command->property_set->value->gtype, command->property_set->value->internal_type,
      command->property_set->value->data.data, command->property_set->value->data.len);
    g_object_set_property (G_OBJECT (element), command->property_set->name, &val);
    g_value_unset (&val);
  }
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_tracer_register (plugin, "debugserver",
          gst_debugserver_tracer_get_type ()))
    return FALSE;
  return TRUE;
}

#define PACKAGE "gst-debugger"

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, debugtracers,
    "GStreamer core tracers", plugin_init, "1.0", "LGPL",
    "gst-debugger", "http://github.com/loganek")
