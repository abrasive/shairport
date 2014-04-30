ShairPort 2.0
=============

Shairport 2.0 is a pretty big rewrite of Shairport 1.0 by James Laird. As present, the files are being pushed to this repository, but they aren't there yet; it may take a little time.

What it is
----------
This program emulates an AirPort Express for the purpose of streaming music from iTunes and compatible iPods and iPhones. It implements a server for the Apple RAOP protocol.
ShairPort does not support AirPlay v2 (video and photo streaming).

What's new in Shairport 2.0
---------------------------
Synchronisation.

(1) Shairport 2.0 sets a delay ("latency") between when music is sent and when it is played. The duration of the delay can be set to match the delay through other iTunes output devices, including other Shairplay 2.0 devices. Latency is actively kept at the set level, so synchronisation is actively maintained.

(2) Shairport 2.0 is developed for ALSA, so is ALSA and Linux only.

Status
------
Shairport 2.0 is working on Raspberry Pi and Linksys NSLU2, both on OpenWrt. It works on an Ubuntu laptop. It works well with built-in audio and with a variety of USB-connected Amplifiers and DACs.

Shairport 2.0 DOES NOT WORK WITH RASPIAN. It runs, but it's very glitchy. This seems to be a known problem with Raspian and the Raspberry Pi. Shairport 2.0 runs very well on the exact same hardware but using OpenWrt in place of Raspian.

Shairport 2.0 runs on Ubuntu inside VMWare Fusion on a Mac, but syncronisation is off when using the built-in emulated soundcard.

Build Requirements
------------------
Required:
* OpenSSL
* Avahi
* ALSA

Debian users can get the basics with
`apt-get install libssl-dev libavahi-client-dev libasound2-dev`
