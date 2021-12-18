# --- To build & test ---

# Step 1 - get libraries:

sudo apt install libavcodec-dev libavformat-dev libavfilter-dev libdrm-dev

# Make

make

# Get test files

wget http://www.jell.yfish.us/media/jellyfish-3-mbps-hd-hevc.mkv
wget http://www.jell.yfish.us/media/jellyfish-3-mbps-hd-hevc-10bit.mkv

# Test

# Enable the V4L2 decoder
sudo dtoverlay rpivid-v4l2

# Stop using X if you were
# <ctrl-alt-F1> to switch to a no-X window

# Run the code
./hello_drmprime ~/jellyfish-3-mbps-hd-hevc.mkv

# You should see jellyfish

# So you don't have to do the dtoverlay step every time
# Enable H265 V4L2 request decoder
# add "dtoverlay=rpivid-v4l2" to /boot/config.txt
# You may also want to add more CMA if you are going to try 4k videos
# Change the "dtoverlay=vc4-fkms-v3d" line in config.txt to read
# "dtoverlay=vc4-fkms-v3d,cma-512"
reboot
# Check it has turned up
ls -la /dev/video*
# This should include video19
# crw-rw----+ 1 root video 81, 7 Aug  4 17:25 /dev/video19

# --- Notes ---

This is a trivial example prog on how to get DRM_PRIME frames out of ffmpeg
and how to display them using drm.  It makes no attempt to pace the video or
scale it correctly, video will just be displayed at one frame per vsync
(assuming that decode is keeping pace) and stretched to the edge of the screen.


Current options:

-l <n> | --loop <n>
   Play the filelist <n> times

-f <n> | --frames <n>
   Only play the first <n> frames of each file

-o <output file>
   Dump the raw output frame (after filter) to <output file>

--deinterlace
   Apply the deinterlace filter to the stream before output


