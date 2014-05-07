ShairPort 2.0
=============

Shairport 2.0 is a pretty substantial rewrite of Shairport 1.0 by James Laird. It is still very experimental, and only works with Linux and ALSA.

Some of the support files, e.g. PKGBUILD and shairport.service files, are out of date.

What is Shairport?
----------
Shairport emulates an AirPort Express for the purpose of streaming music from iTunes and compatible iPods and iPhones. It implements a server for the Apple RAOP protocol.
ShairPort does not support AirPlay v2 (video and photo streaming).

What's new in Shairport 2.0?
---------------------------
Shairport 2.0 does Audio Synchronisation.

(1) Shairport 2.0 allows you to set a delay (a "latency") from when music is sent by iTunes or your iOS device to when it is played in the Shairport audio device. The latency can be set to match the latency of other output devices playing the music, achieving audio synchronisation. Latency is actively kept at the set level, so synchronisation is actively maintained.

(2) Shairport 2.0 is developed for ALSA, so is ALSA and Linux only.

(3) There are lots of little changes, e.g. volume control profile, muting, autotools build control...

Status
------
Shairport 2.0 is working on Raspberry Pi and Linksys NSLU2, both using OpenWrt. It works on an Ubuntu laptop. It works well with built-in audio and with a variety of USB-connected Amplifiers and DACs.

Shairport 2.0 does not work wih Raspian. It does run, but it's very glitchy. This seems to be due to a known problem with Raspian -- see http://www.raspberrypi.org/forums/viewtopic.php?t=23544. On the other hand, Shairport 2.0 runs well on the exact same hardware but using OpenWrt in place of Raspian.

Shairport 2.0 does not run reliably on Ubuntu inside VMWare Fusion on a Mac, possibly due to the emulated soundcard.

Please note that Shairport 2.0 only works with the ALSA back end. You can compile the other back ends in as you wish, but it will not work properly with them. Maybe someday...

Build Requirements
------------------
Required:
* OpenSSL
* Avahi
* ALSA

Debian users can get the basics with
`apt-get install libtool libssl-dev libavahi-client-dev libasound2-dev`

Building Instructions
---------------------
If you're building ShairPort for OpenWrt, there's an OpenWrt package at https://github.com/mikebrady/shairport.

Otherwise, to build Shairport, `cd` into the shairport directory and execute the following commands:

`$ autoreconf -i`

`$ ./configure --with-alsa --with-avahi`

`$ make`

Note that `shairport` is not installed automatically, so where it says `shairport` below, you might have to enter its path name, e.g. `./shairport` if you are still in the shairport directory.


Running Shairport 2.0
---------------------
The `-L` setting is for the amount of latency -- the units are frames, with 44,100 frames to the second. Although 99,400 frames  is slightly more than two seconds, it sounds good -- YMMV.

Examples
--------
The first is an example of a standard Ubuntu based laptop:

`shairport -d -L 99400 -a "Shairport 2.0" -- -d hw:0`

In the following are examples of the Raspberry Pi and the NSLU2 -- little-endian and a big-endian ARM systems running OpenWrt. For best results, you should access the hardware volume controls as shown. Use `alsamixer` or similar to discover the name of the volume controller to be used after the `-c` option.

For a Raspberry Pi using its internal soundcard that drives the headphone jack:

`shairport -d -L 99400 -a "Shairport 2.0" -- -d hw:0 -t hardware -c PCM`

For a cheapo "3D Sound" USB card (Stereo output and input only) on a Raspberry Pi:

`shairport -d -L 99400 -a "Shairport 2.0" -- -d hw:1 -t hardware -c Speaker`

For a first generation Griffin iMic on a Raspberry Pi:

`shairport -d -L 99400 -a "Shairport 2.0" -- -d hw:1 -t hardware -c PCM`

For an NSLU2, which has no internal soundcard, to drive the "3D Sound" USB card:

`shairport -d -L 99400 -a "Shairport 2.0" -- -d hw:0 -t hardware -c Speaker`

