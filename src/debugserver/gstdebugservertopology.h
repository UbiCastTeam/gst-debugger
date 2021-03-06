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

#ifndef SRC_DEBUGSERVER_GSTDEBUGSERVERTOPOLOGY_H_
#define SRC_DEBUGSERVER_GSTDEBUGSERVERTOPOLOGY_H_

#include "gstdebugservertcp.h"

#include <gst/gst.h>

void gst_debugserver_topology_send_entire_topology (GstBin *root, GstDebugserverTcp * server, TcpClient * client);

void gst_debugserver_topology_send_pad_link (GstPad * src, GstPad * sink, gboolean link, GstDebugserverTcp * server, TcpClient * client);

void gst_debugserver_topology_send_element_in_bin (GstBin * bin, GstElement * element, gboolean add, GstDebugserverTcp * server, TcpClient * client);

void gst_debugserver_topology_send_pad_in_element (GstElement * element, GstPad * pad, gboolean add, GstDebugserverTcp * server, TcpClient * client);

#endif /* SRC_DEBUGSERVER_GSTDEBUGSERVERTOPOLOGY_H_ */
