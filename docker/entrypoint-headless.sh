#!/bin/bash
set -e

# Defaults
export DISPLAY=${DISPLAY:-:1}
export RESOLUTION=${RESOLUTION:-1280x720x24}

# Clean up any stale lock files from previous runs
rm -f /tmp/.X*-lock
rm -rf /tmp/.X11-unix

echo "Starting Xvfb on $DISPLAY with resolution $RESOLUTION..."
Xvfb $DISPLAY -screen 0 $RESOLUTION &
XVFB_PID=$!
sleep 2

echo "Starting Fluxbox window manager..."
fluxbox > /dev/null 2>&1 &
FLUXBOX_PID=$!
sleep 5

echo "Starting x11vnc..."
# run once, display :1, loop forever, shared mode, no password by default for local usage
x11vnc -display $DISPLAY -forever -shared -nopw -rfbport 5900 &

echo "Starting websockify (noVNC)..."
# Proxy standard VNC port 5900 to web port 6080
websockify --web /usr/share/novnc 6080 127.0.0.1:5900 &
WEBSOCKIFY_PID=$!

echo "Starting LoungeCat..."
# Execute the Java command passed as arguments or default
exec "$@"
