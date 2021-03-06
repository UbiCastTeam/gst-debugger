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

#include "common/gstdebugger.pb-c.h"

#include <gst/gst.h>

#include "../common/protocol-utils.h"
#define GST_USE_UNSTABLE_API
#include <gst/gsttracer.h>

GST_DEBUG_CATEGORY_STATIC (gst_debugserver_tcp);
#define GST_CAT_DEFAULT gst_debugserver_tcp
#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_debugserver_tcp, "debugserver-tcp", 0, "debugserver tcp tracer");
#define gst_debugserver_tcp_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstDebugserverTcp, gst_debugserver_tcp,
    G_TYPE_OBJECT, _do_init)

static void
gst_debugserver_tcp_finalize (GObject * tcp);

static gboolean
gst_debugserver_tcp_incoming_callback (GSocketService * service,
    GSocketConnection * connection, GObject * source_object, gpointer user_data);

static gpointer
gst_debugserver_tcp_handle_client (gpointer user_data);

static TcpClient*
gst_debugserver_tcp_add_client (GstDebugserverTcp * tcp, GSocketConnection * connection);

static gboolean
gst_debugserver_tcp_send_packet_to_all_clients (GstDebugserverTcp * tcp, GstDebugger__GStreamerData * gst_data);

static void
gst_debugserver_tcp_class_init (GstDebugserverTcpClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = gst_debugserver_tcp_finalize;
}

static void
gst_debugserver_tcp_finalize (GObject * obj)
{
  GstDebugserverTcp * tcp = GST_DEBUGSERVER_TCP (obj);
  gst_debugserver_tcp_stop_server (tcp);
  g_slist_free_full (tcp->clients, g_free);
}

static void
gst_debugserver_tcp_init (GstDebugserverTcp * self)
{
  self->service = NULL;
  self->clients = NULL;
  self->client_disconnected_handler = NULL;
  self->command_handler = NULL;
  self->owner = NULL;
}

GstDebugserverTcp * gst_debugserver_tcp_new (void)
{
  GstDebugserverTcp * tcp = g_object_new (GST_TYPE_DEBUGSERVER_TCP, NULL);

  return tcp;
}

gboolean
gst_debugserver_tcp_start_server (GstDebugserverTcp * tcp, guint port)
{
  GError *error = NULL;

  tcp->service = g_socket_service_new ();

  g_socket_listener_add_inet_port ((GSocketListener *) tcp->service,
      port, NULL, &error);

  if (error != NULL) {
    GST_ERROR ("Error on running server: %s", error->message);
    g_error_free (error);
    return FALSE;
  }

  g_signal_connect (tcp->service,
      "incoming", G_CALLBACK (gst_debugserver_tcp_incoming_callback), tcp);

  g_socket_service_start (tcp->service);

  return TRUE;
}

void
gst_debugserver_tcp_stop_server (GstDebugserverTcp * tcp)
{
  if (tcp->service != NULL) {
    g_socket_service_stop (tcp->service);
    tcp->service = NULL;
  }
}

static gboolean
gst_debugserver_tcp_incoming_callback (GSocketService * service,
    GSocketConnection * connection, GObject * source_object, gpointer user_data)
{
  GArray *user_data_array = g_array_new (FALSE, FALSE, sizeof (gpointer));
  TcpClient *client = gst_debugserver_tcp_add_client (GST_DEBUGSERVER_TCP (user_data), connection);

  g_array_insert_val (user_data_array, 0, client);
  g_array_insert_val (user_data_array, 1, user_data);
  g_object_ref (connection);
  g_thread_new ("connection",
      (GThreadFunc) gst_debugserver_tcp_handle_client, user_data_array);

  return TRUE;
}

static TcpClient*
gst_debugserver_tcp_add_client (GstDebugserverTcp * tcp, GSocketConnection * connection)
{
  TcpClient *client = (TcpClient*) g_malloc (sizeof (TcpClient));

  g_mutex_init (&client->mutex);
  client->connection = connection;
  tcp->clients = g_slist_append (tcp->clients, client);

  return client;
}

static gpointer
gst_debugserver_tcp_handle_client (gpointer user_data)
{
  GArray *user_data_array = (GArray *) user_data;
  TcpClient *client = g_array_index (user_data_array, TcpClient *, 0);
  GstDebugserverTcp *self = g_array_index (user_data_array, GstDebugserverTcp*, 1);
  GInputStream *istream;
  gchar buffer[1024];
  gint size;
  GstDebugger__Command *command;

  g_array_unref (user_data_array);

  istream = g_io_stream_get_input_stream (G_IO_STREAM (client->connection));

  GST_DEBUG_OBJECT (self, "Received connection from client!\n");

  while ((size = gst_debugger_protocol_utils_read_header (istream, NULL)) > 0) {
     assert (size <= 1024); // todo max message size in global file
     GST_DEBUG_OBJECT (self, "Received message of size: %d\n", size);
     gst_debugger_protocol_utils_read_requested_size (istream, size, buffer, NULL);
     command = gst_debugger__command__unpack (NULL, size, (uint8_t*) buffer);
     if (command == NULL) {
       g_print ("error unpacking incoming message\n");
       continue;
     }

     if (self->command_handler != NULL) {
       self->command_handler (command, self->owner, client);
     }

     gst_debugger__command__free_unpacked (command, NULL);
   }

  self->clients = g_slist_remove (self->clients, client);

  if (self->client_disconnected_handler)
    self->client_disconnected_handler (client, self->owner);

  g_free (client);

  GST_LOG_OBJECT (self, "Client disconnected");

  return NULL;
}

TcpClient*
gst_debugserver_tcp_find_client (GstDebugserverTcp * tcp, GSocketConnection * connection)
{
  GSList *client_list = tcp->clients;
  TcpClient *client;

  while (client_list != NULL) {
    client = (TcpClient*) client_list->data;
    if (client->connection == connection) {
      return client;
    }
    client_list = g_slist_next (client_list);
  }
 return NULL;
}

gboolean
gst_debugserver_tcp_send_packet (GstDebugserverTcp * tcp, TcpClient * client,
  GstDebugger__GStreamerData * gst_data)
{
  GError *err = NULL;
  gchar size_buffer[4];
  GOutputStream *ostream;
  gchar buff[1024];
  gchar *m_buff = buff;
  gint size;

  if (client == NULL) {
    return gst_debugserver_tcp_send_packet_to_all_clients (tcp, gst_data);
  }

  g_mutex_lock (&client->mutex);

  if (!g_socket_connection_is_connected (client->connection)) {
    return FALSE;
  }

  ostream = g_io_stream_get_output_stream (G_IO_STREAM (client->connection));

  size = gst_debugger__gstreamer_data__get_packed_size (gst_data);

  err = gst_debugger_protocol_write_header (ostream, size);

  if (err) {
    g_mutex_unlock (&client->mutex);
    g_print ("cannot send size of data: %s\n", err->message);
    g_error_free (err);
    return FALSE;
  }

  if (size > 1024) {
    m_buff = (char*) g_malloc (size * sizeof (char));
  }

  gst_debugger__gstreamer_data__pack (gst_data, m_buff);

  g_output_stream_write (ostream, (gchar*)m_buff, size, NULL, &err);

  if (m_buff != buff) {
    g_free (m_buff);
  }

  g_mutex_unlock (&client->mutex);

  if (err) {
    g_print ("cannot send data: %s\n", err->message);
    g_error_free (err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_debugserver_tcp_send_packet_to_all_clients (GstDebugserverTcp * tcp, GstDebugger__GStreamerData * gst_data)
{
  GSList *clients = tcp->clients;
  TcpClient *client;
  gboolean ret = TRUE;

  while (clients != NULL) {
    client = (TcpClient*)clients->data;
    ret = ret && gst_debugserver_tcp_send_packet (tcp, client, gst_data);
    clients = clients->next;
  }

  return ret;
}
