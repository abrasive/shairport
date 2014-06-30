Shairport Sync
=============
Shairport Sync allows you to synchronise the audio coming from all your devices. Specifically, Shairport Sync allows you to set the "latency" -- the time between when a sound is sent and when it is played. This allows you to synchronise Shairport Sync devices reliably with other devices playing the same source. For example, synchronised multi-room audio is possible without difficulty.

Shairport Sync is a pretty substantial rewrite of Shairport 1.0 by James Laird and others -- please see https://github.com/abrasive/shairport/blob/master/README.md#contributors-to-version-1x for a list of the contributors to Shairport 1.x and Shairport 0.x. From a "heritage" point of view, Shairport Sync is a fork of Shairport 1.0 and the active branch is called Shairport 2.0.

Shairport Sync is still experimental, and only works with Linux and ALSA. The sound card you use must be capable of working with 44,100 samples per second interleaved PCM stereo (you'll get a message in the logfile if there's a problem).

Some of the support files, e.g. PKGBUILD and shairport.service files, are out of date.

For more about the motivation behind Shairport Sync, please see the wiki at https://github.com/mikebrady/shairport-sync/wiki.

What is Shairport?
----------
Shairport emulates an AirPort Express for the purpose of streaming music from iTunes and compatible iPods and iPhones. It implements a server for the Apple RAOP protocol.
Shairport does not support AirPlay video and photo streaming.

Shairport Sync does Audio Synchronisation
---------------------------
Shairport Sync allows you to set a delay (a "latency") from when music is sent by iTunes or your iOS device to when it is played in the Shairport Sync audio device. The latency can be set to match the latency of other output devices playing the music (or video in the case of the AppleTV or Quicktime), achieving audio synchronisation. Shairport Sync uses extra timing information to stay synchronised with the source's time signals, eliminating "drift", where audio streams slowly drift out of synchronisation.

To stay synchronised, if an output device is running slow, Shairport Sync will delete frames of audio to allow it to keep up; if the device is running fast, Shairport Sync will insert interpolated frames to keep time. The number of frames inserted or deleted is so small as to be inaudible. Frames are inserted or deleted as necessary at pseudorandom intervals.

Shairplay Sync will automatically choose a suitable latency for iTunes and for AirPlay devices such as the AppleTV.

What else?
--------------
* Shairport Sync offers finer control at very top and very bottom of the volume range. See http://tangentsoft.net/audio/atten.html for a good discussion of audio "attenuators", upon which volume control in Shairport Sync is modelled. See also the diagram of the volume transfer function in the documents folder.
* Shairport Sync will mute properly if the hardware supports it.
* If it has to use software volume and mute controls, the response time is shorter than before -- now it responds in 0.15 seconds.
* Shairport Sync sends back a "busy" signal if it's already playing audio from another source, so other sources can't "barge in" on an existing Shairport Sync session. (If a source disappears without warning, the session automatically terminates after two minutes and the device becomes available again.)

Status
------
Shairport Sync is working on Raspberry Pi with Raspian and OpenWrt, and it runs on a Linksys NSLU2 using OpenWrt. It also works on a standard Ubuntu laptop. It works well with built-in audio and with a variety of USB-connected Amplifiers and DACs, including a cheapo USB "3D Sound" dongle, a first generation iMic and a Topping TP30 amplifier with a USB DAC input. It also works with the IQAudIO Pi-DAC on the latest version of Raspian -- please see http://iqaudio.com/wp-content/uploads/2014/06/IQaudIO_Doc.pdf for details.

Shairport Sync compiles and runs pretty well on the built-in sound card of a Raspberry Pi model B under Raspian. Due to the limitations of the sound card, you wouldn't mistake the output for HiFi, but it's really not too shabby.
USB-connected sound cards work well, so long as the wired Ethernet  port is not in use -- WiFi is fine if the network is not too busy. However, driving any USB-based audio output device is glitchy if you are using Ethernet at the same time or if you're on a busy WiFi network. It works, but it's very glitchy. This seems to be due to a known problem -- see http://www.raspberrypi.org/forums/viewtopic.php?t=23544 -- and it will hopefully be fixed in a forthcoming update to Raspian.
Shairport Sync runs very well on the same hardware using OpenWrt in place of Raspian and then it works properly with both Ethernet and WiFi.

Shairport Sync runs on Ubuntu and Debian 7 inside VMWare Fusion 6.0.3 on a Mac, but synchronisation does not work -- possibly because the soundcard is being emulated.

Shairport Sync works only with the ALSA back end. You can try compiling the other back ends in as you wish, but it definitely will not work properly with them. Maybe someday...

One other change of note is that the Shairport Sync build process now uses GNU autotools and libtool to examine and configure the build environment -- very important for cross compilation. All previous versions looked in the current system to determine which packages were available, instead of looking at what packages were available in the target system.

Build Requirements
------------------
Required:
* OpenSSL
* Avahi
* ALSA
* libdaemon
* autoconf
* libtool

Debian, Ubuntu and Raspian users can get the basics with:

`apt-get install avahi-daemon autoconf libtool libdaemon-dev libssl-dev libavahi-client-dev libasound2-dev`

Building Instructions
---------------------
If you're interested in Shairport Sync for OpenWrt, there's an OpenWrt package at https://github.com/mikebrady/shairport. OpenWrt doesn't support the IQaudIO Pi-DAC.

Otherwise, to build Shairport Sync, download it:

`git clone https://github.com/mikebrady/shairport-sync.git`

Next, `cd` into the shairport-sync directory and execute the following commands:

`$ autoreconf -i`

`$ ./configure --with-alsa --with-avahi`

`$ make` 

If you run `$sudo make install`, `shairport` will be installed along with an initscript which will automatically launch it at startup. The settings used are the most basic defaults, so you will want to edit the file `/etc/init.d/shairport` to give the service a name, use a different card, use the hardware mixer and volume control, etc. -- there are some examples in the file.

Examples
--------
The first is an example of a standard Ubuntu based laptop:

`shairport -d -a "Shairport 2.0" -- -d hw:0`

In the following are examples of the Raspberry Pi and the NSLU2. For best results, including getting true mute and instant response to volume control and pause commands, you should access the hardware volume controls as shown. Use `amixer` or `alsamixer` or similar to discover the name of the volume controller to be used after the `-c` option.

For a Raspberry Pi using its internal soundcard that drives the headphone jack:

`shairport -d -a "Shairport Sync" -- -d hw:0 -t hardware -c PCM`

For a Raspberry Pi driving a Topping TP30 Digital Amplifier, which has an integrated USB DAC:

`shairport -d -a Kitchen -- -d hw:1 -t hardware -c PCM`

For a cheapo "3D Sound" USB card (Stereo output and input only) on a Raspberry Pi:

`shairport -d -a "Shairport Sync" -- -d hw:1 -t hardware -c Speaker`

For a first generation Griffin iMic on a Raspberry Pi:

`shairport -d -a "Shairport Sync" -- -d hw:1 -t hardware -c PCM`

For an NSLU2, which has no internal soundcard, there appears to be a bug in ALSA -- you can not specify a device other than "default". Thus:

On an NSLU2, to drive a first generation Griffin iMic:

`shairport -d -a "Shairport Sync" -- -t hardware -c PCM`

On an NSLU2, to drive the "3D Sound" USB card:

`shairport -d -a "Shairport Sync" -- -t hardware -c Speaker`

Latency
-------
Latency is the exact time from a sound signal's original timestamp until that signal actually "appears" on the output of the DAC, irrespective of any internal delays, processing times, etc. in the computer. From listening tests, it seems that there are two latencies in current use:
* If the source is iTunes, a latency of 99,400 frames seems to bring Shairport Sync into exact synchronisation both with the speakers on the iTunes computer itself and with AirPort Express receivers.
* If the source is an AirPlay device, the latency seems to be exactly 88,200 frames. AirPlay devices include AppleTV, iPod, iPad and iPhone and Quicktime Player on Mac.

Shairport Sync therefore has default latencies for iTunes and AirPlay sources: 99,400 frames for iTunes and 88,200 for all AirPlay devices.

You can set your own iTunes latency with the `-i` or `--iTunesLatency` option (e.g. `-i 99400` or `--iTunesLatency=99400`). Similarly you can set an AirPlay latency with the `-A` or `--AirPlayLatency` option. If you set a latency with `-L` it overrides both the AirPlay and iTunes latency values. Basically, you shouldn't use it except for experimentation.

Some Statistics
---------------
If you run Shairport Sync from the command line without daemonising it (omit the `-d`), and if you turn on one level of verbosity (include `-v`), e.g. as follows for the Raspberry Pi with "3D Sound" card:

`shairport -a "Shairport Sync" -v -- -d hw:1 -t hardware -c Speaker`

it will print statistics like this occasionally:

`Divergence: 15.3 (ppm); corrections: 21.6 (ppm); missing packets 0; late packets 0; too late packets 0; resend requests 0.`

"Divergence" is actually the net sum of corrections -- the number of frame insertions less the number of frame deletions -- given as a moving average in parts per million. After an initial settling period, it represents the divergence between the source clock and the sound device's clock. The example above indicates that the output DAC's clock is running 15.3 ppm faster than the source's clock.

"Corrections" is the number of frame insertions plus the number of frame deletions (i.e. the total number of "corrections"), given as a moving average in parts per million. The closer this is to the absolute value of the drift, the fewer "unnecessary" corrections that are being made.

For reference, a drift of one second per day is approximately 11.57 ppm. Left uncorrected, even a drift this small between two audio outputs will be audible after a short time.

It's not unusual to have resend requests, late packets and even missing packets if some part of the connection to the Shairport Sync device is over WiFi.

Miscellaneous
-------------
Shairport Sync actively maintains synchronisation with the source. 
If synchronisation is lost -- say due to a busy source or a congested network -- Shairport Sync will mute its output and resynchronise. The loss-of-sync threshold is a very conservative 50 ms -- i.e. the actual time and the expected time must differ by more than 50 ms to trigger a resynchronisation. Smaller disparities are corrected by insertions or deletions, as described above.
