ShairPort 2.0
=============

Shairport 2.0 is a pretty big rewrite of Shairport 1.0 by James Laird.

It is very experimental, and only works with Linux and ALSA.

Some of the support files, e.g. PKGBUILD and shairport.service files, are out of date.

What it is
----------
This program emulates an AirPort Express for the purpose of streaming music from iTunes and compatible iPods and iPhones. It implements a server for the Apple RAOP protocol.
ShairPort does not support AirPlay v2 (video and photo streaming).

What's new in Shairport 2.0
---------------------------
Synchronisation.

(1) Shairport 2.0 sets a delay (a "latency") from when music is sent by iTunes to when it is played in the Shairport audio device. The duration of the latency can be set to match the delay through other iTunes output devices, (including other Shairplay 2.0 devices). Latency is actively kept at the set level, so synchronisation is actively maintained.

(2) Shairport 2.0 is developed for ALSA, so is ALSA and Linux only.

(3) Lots of little changes, e.g. volume control profile, muting, autotools build control...

Status
------
Shairport 2.0 is working on Raspberry Pi and Linksys NSLU2, both using OpenWrt. It works on an Ubuntu laptop. It works well with built-in audio and with a variety of USB-connected Amplifiers and DACs.

Shairport 2.0 DOES NOT WORK WITH RASPIAN. It runs, but it's very glitchy. This seems to be a known problem with Raspian. On the other hand, Shairport 2.0 runs well on the exact same hardware but using OpenWrt in place of Raspian.

Shairport 2.0 runs on Ubuntu inside VMWare Fusion on a Mac, but syncronisation is off when using the built-in emulated soundcard.

Please note that Shairport 2.0 only works with the ALSA back end. You can compile the other back ends in as you wish, but it will not work properly with them. Maybe someday...

Build Requirements
------------------
Required:
* OpenSSL
* Avahi
* ALSA

Debian users can get the basics with
`apt-get install libssl-dev libavahi-client-dev libasound2-dev`

Building Instructions
---------------------
$ autotools -i

$ ./configure --with-alsa --with-avahi

$ make


Running Shairport 2.0
---------------------
Settings are for the amount of latency -- the units are "frames", with 44100 frames to the second. Although 99000 is slightly more than two seconds, it works pretty well, as does 99300 -- YMMV.

Examples
--------
In the following are examples of the Raspberry Pi and the NSLU2, little-endian and a big-endian ARM systems running OpenWrt. Also an example of a standard Ubuntu based laptop.

For a Ubuntu laptop:

shairport -d -L 99000 -a "Shairport"

For best results, you should access the hardware volume control. Use alsamixer or similar to find out the name of the volume controller to be used after the -c option. For a Raspberry Pi using its internal soundcard that drives the headphone jack:

shairport -d -L 99000 -a "Shairport" -- -d hw:0 -t hardware -c PCM

For a cheapo "3D Sound" USB card (Stereo output and input only) on a Raspberry Pi:

shairport -d -L 99000 -a "Shairport" -- -d hw:1 -t hardware -c Speaker

For a first generation Griffin iMic on a Raspberry Pi:

shairport -d -L 99000 -a "Shairport" -- -d hw:1 -t hardware -c PCM

For an NSLU2, which has no internal soundcard, to drive the "3D Sound" USB card:

shairport -d -L 99000 -a "Shairport" -- -d hw:0 -t hardware -c Speaker

