%.pb-c.c %.pb-c.h: %.proto
	$(PROTOC_C) -I$(srcdir) --c_out=$(builddir) $^

%.pb.cc %.pb.h: %.proto
	$(PROTOC) --proto_path=$(srcdir) --cpp_out=$(builddir) $^

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libgst-debugger-common-@GST_DEBUGGER_API_VERSION@.pc

dist_noinst_DATA = gstdebugger.proto

MOSTLYCLEANFILES = gstdebugger.pb-c.c gstdebugger.pb-c.h gstdebugger.pb.cc gstdebugger.pb.h

common_sources =		\
	gst-utils.c		\
	protocol-utils.c	\
	serializer.c

common_headers =		\
	common.h		\
	gst-utils.h		\
	protocol-utils.h	\
	serializer.h

common_include_dir = $(includedir)/gst-debugger-@GST_DEBUGGER_API_VERSION@/gst-debugger

lib_LTLIBRARIES = libgst-debugger-common-c-@GST_DEBUGGER_API_VERSION@.la libgst-debugger-common-cpp-@GST_DEBUGGER_API_VERSION@.la 

libgst_debugger_common_c_@GST_DEBUGGER_API_VERSION@_la_SOURCES = $(common_sources) gstdebugger.pb-c.c
libgst_debugger_common_c_@GST_DEBUGGER_API_VERSION@_lainclude_HEADERS = $(common_headers) gstdebugger.pb-c.h
libgst_debugger_common_c_@GST_DEBUGGER_API_VERSION@_laincludedir = $(common_include_dir)
libgst_debugger_common_c_@GST_DEBUGGER_API_VERSION@_la_LDFLAGS = $(PROTOBUF_C_LIBS) $(GSTREAMER_LIBS)
libgst_debugger_common_c_@GST_DEBUGGER_API_VERSION@_la_CFLAGS = -I.. $(PROTOBUF_C_CFLAGS) $(GSTREAMER_CFLAGS)

libgst_debugger_common_cpp_@GST_DEBUGGER_API_VERSION@_la_SOURCES = $(common_sources) gstdebugger.pb.cc
libgst_debugger_common_cpp_@GST_DEBUGGER_API_VERSION@_lainclude_HEADERS = $(common_headers) gstdebugger.pb.h
libgst_debugger_common_cpp_@GST_DEBUGGER_API_VERSION@_laincludedir = $(common_include_dir)
libgst_debugger_common_cpp_@GST_DEBUGGER_API_VERSION@_la_LDFLAGS  = $(PROTOBUF_LIBS) $(GSTREAMER_LIBS)
libgst_debugger_common_cpp_@GST_DEBUGGER_API_VERSION@_la_CFLAGS = -I.. $(PROTOBUF_CFLAGS) $(GSTREAMER_CFLAGS)
