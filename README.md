# Using gst-debugger

To use gst-debugger, run your gstreamer program with the GST_TRACERS=debugserver environment variable:

```
$ GST_TRACERS=debugserver gst-launch-1.0 videotestsrc ! xvimagesink
```

The gstreamer program will now listen on TCP port 8080

Then open gst-debugger-1.0 and click "Server" > "Connect"; if the gstreamer program you wish to debug is running on another system, go to "Edit" > "Preferences" to set the target ip.
