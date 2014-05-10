Shairport 2.0
=============

Shairport 2.0 allows you to synchronise the audio coming from all your devices. Specifically, Shairport 2.0 allows you to set the "latency" -- the time between when a sound is sent and when it is played. This allows you to synchronise Shairport 2.0 devices reliably with other devices playing the same source. For example, synchronised multi-room audio is possible without difficulty.

Shairport 2.0 is a pretty substantial rewrite of Shairport 1.0 by James Laird. It is still experimental, and only works with Linux and ALSA. Some of the support files, e.g. PKGBUILD and shairport.service files, are out of date.

What is Shairport?
----------
Shairport emulates an AirPort Express for the purpose of streaming music from iTunes and compatible iPods and iPhones. It implements a server for the Apple RAOP protocol.
Shairport does not support AirPlay v2 (video and photo streaming).

Shairport 2.0 does Audio Synchronisation
---------------------------
Shairport 2.0 allows you to set a delay (a "latency") from when music is sent by iTunes or your iOS device to when it is played in the Shairport audio device. The latency can be set to match the latency of other output devices playing the music, achieving audio synchronisation. Shairport 2.0 uses extra timing information to stay in sync with the source's time signals, eliminating "drift", where audio streams slowly drift out of synchronisation.

What else?
--------------
* Shairport 2.0 offers finer control at very top and very bottom of the volume range. See http://tangentsoft.net/audio/atten.html for a good discussion of audio "attenuators", upon which volume control in Shairport 2 is modelled.
* Shairport 2.0 will mute properly if the hardware supports it.
* If it has to use software volume and mute controls, the response time is shorter than before.

Status
------
Shairport 2.0 is working on Raspberry Pi and Linksys NSLU2, both using OpenWrt. It works on an Ubuntu laptop. It works well with built-in audio and with a variety of USB-connected Amplifiers and DACs.

Shairport 2.0 compiles and runs pretty well on the built-in sound card of a Raspberry Pi model B under Raspian. Due to the limitations of the sound card, you wouldn't mistake the output for HiFi, but it's really not too shabby. However, driving any USB-based audio output device is glitchy if you are using Ethernet at the same time. It works, but it's very glitchy. This seems to be due to a known problem -- see http://www.raspberrypi.org/forums/viewtopic.php?t=23544 -- and it will hopefully be fixed in a forthcoming update to Raspian. Right now, though, Shairport 2.0 runs well on the same hardware but using OpenWrt in place of Raspian.

Shairport 2.0 sort-of runs on Ubuntu 13.10 inside VMWare Fusion 6.0.3 on a Mac, but synchronisation does not work too well -- possibly because the soundcard is being emulated. Also, Shairport doesn't always start properly. Still being investigated, this.

Shairport 2.0 works only with the ALSA back end. You can compile the other back ends in as you wish, but it definitely will not work properly with them. Maybe someday...

One other change of note is that the Shairport 2.0 build process now uses autotools to examine and configure the build environment -- very important for cross compilation. All previous versions looked in the current system to determine which packages were available, instead of looking at what packages were available in the target system.

Build Requirements
------------------
Required:
* OpenSSL
* Avahi
* ALSA

Debian (and Raspian) users can get the basics with

`apt-get install avahi-daemon autoconf libtool libssl-dev libavahi-client-dev libasound2-dev`

Building Instructions
---------------------
If you're interested in Shairport for OpenWrt, there's an OpenWrt package at https://github.com/mikebrady/shairport.

Otherwise, to build Shairport, `cd` into the shairport directory and execute the following commands:

`$ autoreconf -i`

`$ ./configure --with-alsa --with-avahi`

`$ make`

Note that `shairport` is not installed automatically, so where it says `shairport` below, you might have to enter its path name, e.g. if you are still in the shairport directory:

`$ ./shairport -v` will execute the just-compiled shairport in the slightly verbose mode.

Running Shairport 2.0
---------------------
The `-L` setting is for the amount of latency -- the units are frames, with 44,100 frames to the second. Although 99,400 frames  is slightly more than two seconds, it sounds good -- YMMV.

Examples
--------
The first is an example of a standard Ubuntu based laptop:

`shairport -d -L 99400 -a "Shairport 2.0" -- -d hw:0`

In the following are examples of the Raspberry Pi and the NSLU2 -- little-endian and a big-endian ARM systems running OpenWrt. For best results, including getting true mute and instant response to volume control and pause commands, you should access the hardware volume controls as shown. Use `amixer` or `alsamixer` or similar to discover the name of the volume controller to be used after the `-c` option.

For a Raspberry Pi using its internal soundcard that drives the headphone jack:

`shairport -d -L 99400 -a "Shairport 2.0" -- -d hw:0 -t hardware -c PCM`

For a Raspberry Pi driving a Topping TP30 Digital Amplifier, which has an integrated USB DAC:

`shairport -d -a Kitchen -L 99400 -- -d hw:1 -t hardware -c PCM`

For a cheapo "3D Sound" USB card (Stereo output and input only) on a Raspberry Pi:

`shairport -d -L 99400 -a "Shairport 2.0" -- -d hw:1 -t hardware -c Speaker`

For a first generation Griffin iMic on a Raspberry Pi:

`shairport -d -L 99400 -a "Shairport 2.0" -- -d hw:1 -t hardware -c PCM`

For an NSLU2, which has no internal soundcard, there appears to be a bug in ALSA -- you can not specify a device other than "default". Thus:

On an NSLU2, to drive a first generation Griffin iMic:

`shairport -d -L 99400 -a "Shairport 2.0" -- -t hardware -c PCM`

On an NSLU2, to drive the "3D Sound" USB card:

`shairport -d -L 99400 -a "Shairport 2.0" -- -t hardware -c Speaker`

Latency
-------
The latency you set with the -L option is the exact time from a sound signal's original timestamp until that signal actually "appears" on the output of the DAC, irrespective of any internal delays, processing times, etc. in the computer. Thus, to get perfect audio synchronisation, the latency should be the same for all Shairport 2.0 devices, no matter what output devices they use -- build-in audio, USB DACs, etc. In the writer's experience, this is true.

What is slightly curious is that the latency that most closely matches that of the Airport Express is around 99,400 frames, a little over the two seconds that most people report as the Airport Express's latency.

Some Statistics
---------------
If you run Shairport from the command line without daemonising it (omit the `-d`), and if you turn on one level of verbosity (include `-v`), e.g. as follows for the Raspberry Pi with "3D Sound" card:

`shairport -L 99400 -a "Shairport 2.0" -v -- -d hw:1 -t hardware -c Speaker`

it will print statistics like this occasionally:

`Drift: -15.3 (ppm); Corrections: 21.6 (ppm); missing_packets 0; late_packets 0; too_late_packets 0; resend_requests 0.`

"Drift" is the negative of the net corrections -- the number of frame insertions less the number of frame deletions made, given as a moving average in parts per million. After an initial settling period, it represents the divergence between the source clock and the sound device's clock. The example above indicates that the output DAC's clock is running 15.3 ppm slower than the source's clock.

"Corrections" is the number of frame insertions plus the number of frame deletions (i.e. the total number of "corrections" made), given as a moving average in parts per million. The closer this is to the absolute value of the drift, the fewer "unnecessary" corrections that are being made.

For reference, a drift of one second per day is approximately 11.57 ppm. Left uncorrected, even a drift this small between two audio outputs will be audible after a short time.

It's not unusual to have resend requests, late packets and even missing packets if some part of the connection to the Shairport device is over WiFi.
