/* GStreamer
 * Copyright (C) 2015 Marcin Kolny <marcin.kolny@gmail.com>
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

#include "gstdebugservertcp.h"
#include "protocol/protocol_utils.h"

#include <gst/gst.h>
#define GST_USE_UNSTABLE_API
#include <gst/gsttracer.h>

GST_DEBUG_CATEGORY_STATIC (gst_debugserver_tcp);
#define GST_CAT_DEFAULT gst_debugserver_tcp
#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_debugserver_tcp, "debugserver-tcp", 0, "debugserver tcp tracer");
#define gst_debugserver_tcp_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDebugserverTcp, gst_debugserver_tcp,
    G_TYPE_OBJECT, _do_init);

static void gst_debugserver_tcp_finalize (GObject * tcp);

static void
gst_debugserver_tcp_class_init (GstDebugserverTcpClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = gst_debugserver_tcp_finalize;
}

static void
gst_debugserver_tcp_init (GstDebugserverTcp * self)
{
  self->service = NULL;
  self->process_command = NULL;
  self->process_command_user_data = NULL;
}

GstDebugserverTcp * gst_debugserver_tcp_new (void)
{
  return g_object_new (GST_TYPE_DEBUGSERVER_TCP, NULL);
}

static void
gst_debugserver_tcp_finalize (GObject * obj)
{
  GstDebugserverTcp * tcp = GST_DEBUGSERVER_TCP (obj);
  gst_debugserver_tcp_stop_server (tcp);
}

static gpointer
gst_debugserver_tcp_process_client (gpointer user_data)
{
  GArray *tmp = (GArray *) user_data;
  GSocketConnection *connection = g_array_index (tmp, GSocketConnection *, 0);
  GstDebugserverTcp *tcp =
      g_array_index (tmp, GstDebugserverTcp*, 1);
  GInputStream *istream;
  guint8 message[1024];
  Command *command;
  gint size;

  g_array_unref (tmp);

  istream = g_io_stream_get_input_stream (G_IO_STREAM (connection));

  GST_DEBUG_OBJECT (tcp, "Received connection from client!\n");

  while ((size = gst_debugger_protocol_utils_read_header (istream)) > 0) {
     assert (size <= 1024);
     GST_DEBUG_OBJECT (tcp, "Received message of size: %d\n", size);
     gst_debugger_protocol_utils_read_requested_size (istream, size, message);
     command = command__unpack (NULL, size, message);
     if (command == NULL) {
       g_print ("error unpacking incoming message\n");
       continue;
     }

     GST_LOG_OBJECT (tcp, "Command type: %d\n", command->command_type);

     if (tcp->process_command)
       tcp->process_command (command, connection, tcp->process_command_user_data);

     command__free_unpacked (command, NULL);
   }

  return NULL;
}


static gboolean
gst_debugserver_tcp_incoming_callback (GSocketService * service,
    GSocketConnection * connection, GObject * source_object, gpointer user_data)
{
  GArray *tmp = g_array_new (FALSE, FALSE, sizeof (gpointer));
  g_array_insert_val (tmp, 0, connection);
  g_array_insert_val (tmp, 1, user_data);
  g_object_ref (connection);
  g_thread_new ("connection",
      (GThreadFunc) gst_debugserver_tcp_process_client, tmp);
  return TRUE;
}

void
gst_debugserver_tcp_start_server (GstDebugserverTcp * tcp, guint port)
{
  GError *error = NULL;
  tcp->service = g_socket_service_new ();

  g_socket_listener_add_inet_port ((GSocketListener *) tcp->service,
      port, NULL, &error);

  if (error != NULL) {
    GST_ERROR ("%s", error->message);
    g_error_free (error);
  }

  g_signal_connect (tcp->service,
      "incoming", G_CALLBACK (gst_debugserver_tcp_incoming_callback), tcp);

  g_socket_service_start (tcp->service);
}

void
gst_debugserver_tcp_stop_server (GstDebugserverTcp * tcp)
{
  if (tcp->service != NULL) {
    g_socket_service_stop (tcp->service);
    tcp->service = NULL;
  }
}

gboolean gst_debugserver_tcp_send_packet (GSocket * socket, guint8 * buffer,
  gint size)
{
  GError *err = NULL;
  guint8 size_buffer[4];

  gst_debugger_protocol_utils_serialize_integer (size, size_buffer, 4);
  g_socket_send (socket, (gchar*)size_buffer, 4, NULL, &err);

  if (err) {
    g_print ("cannot send size of data: %s\n", err->message);
    g_error_free (err);
    return FALSE;
  }

  g_socket_send (socket, (gchar*)buffer, size, NULL, &err);
  if (err) {
    g_print ("cannot send data: %s\n", err->message);
    g_error_free (err);
    return FALSE;
  }

  return TRUE;
}