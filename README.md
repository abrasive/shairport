Shairport Sync
=============
What is Shairport Sync?
----------
Shairport Sync emulates an AirPort Express for the purpose of streaming audio from iTunes, iPods, iPhones, iPads and AppleTVs.
Audio played by a Shairport Sync-powered device stays synchronised with the source and hence with similar devices playing the same source. Thus, for example, synchronised multi-room audio is possible without difficulty. (Hence the name Shairport Sync, BTW.)

Shairport Sync does not support AirPlay video or photo streaming.

Shairport Sync works by using timing information present in the audio data stream to keep in step with the source. It does this by monitoring and controlling the "latency" -- the time between when a sound is time-stamped at the source and when it is played by the audio output device. To measure latency precisely, it keeps its own clock synchronised with the clock used by the source, usually to within a fraction of a millisecond, using a variant of NTP synchronisation protocols.

To maintain the exact latency required, if an output device is running slow relative to the source, Shairport Sync will delete frames of audio to allow the device to keep up; if the device is running fast, Shairport Sync will insert frames to keep time. The number of frames inserted or deleted is so small as to be almost inaudible. Frames are inserted or deleted as necessary at pseudorandom intervals. Alternatively, with `libsoxr` support, Shairport Sync can resample the audio feed to ensure the output device can keep up. This is even less obtrusive than insertion and deletion but requires a good deal of processing power -- most embedded devices probably can't support it.

There are two default latency settings, chosen automatically. One latency matches the latency used by recent version of iTunes when playing audio and the other matches the latency used by older version of iTunes, by iOS devices and by iTunes and Quicktime Player when playing video.

Shairport Sync is a pretty substantial rewrite of Shairport 1.0 by James Laird and others -- please see https://github.com/abrasive/shairport/blob/master/README.md#contributors-to-version-1x for a list of the contributors to Shairport 1.x and Shairport 0.x. From a "heritage" point of view, Shairport Sync is a fork of Shairport 1.0 and the active branch is called Shairport Sync 2.1.

Shairport Sync works only with Linux and ALSA. The sound card you use must be capable of working with 44,100 samples per second interleaved PCM stereo (you'll get a message in the logfile if there's a problem).

For more about the motivation behind Shairport Sync, please see the wiki at https://github.com/mikebrady/shairport-sync/wiki.

What else?
--------------
* Shairport Sync offers finer control at very top and very bottom of the volume range. See http://tangentsoft.net/audio/atten.html for a good discussion of audio "attenuators", upon which volume control in Shairport Sync is modelled. See also the diagram of the volume transfer function in the documents folder.
* Shairport Sync will mute properly if the hardware supports it.
* If Shairport Sync has to use software volume and mute controls, the response time is shorter than before -- now it responds in 0.15 seconds.
* Shairport Sync sends back a "busy" signal if it's already playing audio from another source, so other sources can't disrupt an existing Shairport Sync session. (If a source disappears without warning, the session automatically terminates after two minutes and the device becomes available again.)

Status
------
Shairport Sync works on standard Ubuntu laptops, on the Raspberry Pi with Raspian and with OpenWrt, and it runs on a Linksys NSLU2 using OpenWrt. It works with built-in audio and with a variety of USB-connected audio amplifiers and DACs, including a cheapo USB "3D Sound" dongle, a first generation iMic and a Topping TP30 amplifier with a USB DAC input.

Shairport Sync runs well on the Raspberry Pi. It can drive the built-in sound card, and though you wouldn't mistake the output for HiFi, it's really not too shabby. USB-connected sound cards work well on the latest version of Raspian; however older versions of Raspian appear to suffer from a problem -- see http://www.raspberrypi.org/forums/viewtopic.php?t=23544, so it is wise to update. Shairport Sync works very well with the IQAudIO Pi-DAC -- see http://iqaudio.com/wp-content/uploads/2014/06/IQaudIO_Doc.pdf for details.

Shairport Sync runs on Ubuntu and Debian inside VMWare Fusion 7 on a Mac, but synchronisation does not work -- possibly because the soundcard is being emulated.

Shairport Sync works only with the ALSA back end. You can try compiling the other back ends in as you wish, but it definitely will not work properly with them. Maybe someday...

One other difference from other versions of Shairport is that the Shairport Sync build process uses GNU autotools and libtool to examine and configure the build environment -- very important for cross compilation. All previous versions looked in the current system to determine which packages were available, instead of looking at what packages were available in the target system.

For information about changes and updates, please refer to the RELEASENOTES.md file in the distribution.

Building And Installing
---------------------
If you're interested in Shairport Sync for OpenWrt, there's an OpenWrt package at https://github.com/mikebrady/shairport-sync-for-openwrt. OpenWrt doesn't support the IQaudIO Pi-DAC.

Otherwise, follow these instructions.

The following libraries are required:
* OpenSSL or PolarSSL
* Avahi
* ALSA
* libdaemon
* autoconf
* libtool
* libpopt

Optional:
* libsoxr

Many linux distributions have Avahi and OpenSSL already in place, so normally it probably makes sense to choose those options rather than tinysvcmdns or PolarSSL. Libsoxr is available in recent linux distributions, but it requires lots of processor power -- chances are an embedded processor won't be able to keep up.

Assuming the usual build essentials and git, Debian, Ubuntu and Raspian users can get the basics with:

- `apt-get install autoconf libtool libdaemon-dev libasound2-dev libpopt-dev`
- `apt-get install avahi-daemon libavahi-client-dev` if you want to use Avahi (recommended).
- `apt-get install libssl-dev` if you want to use OpenSSL and libcrypto, or use PolarSSL otherwise.
- `apt-get install libpolarssl-dev` if you want to use PolarSSL, or use OpenSSL/libcrypto otherwise.
- `apt-get install libsoxr-dev` if you want support for libsoxr-based resampling. Not yet part of  Raspian.

Download Shairport Sync:

`git clone https://github.com/mikebrady/shairport-sync.git`

Next, `cd` into the shairport-sync directory and execute the following commands:

`$ autoreconf -i -f`

Choose the appropriate `--with-*` options:

- `--with-alsa` for the ALSA audio back end. This is required.
- `--with-avahi` or `--with-tinysvcmdns` for mdns support. Avahi is a widely-used system-wide zero-configuration networking (zeroconf) service -- it may already be in your system. If you don't have Avahi, or similar, then consider including tinysvcmdns, which is a tiny zeroconf service embedded inside the shairport-sync application itself. To enable multicast for `tinysvcmdns`, you may have to add a default route with the following command: `route add -net 224.0.0.0 netmask 224.0.0.0 eth0` (substitute the correct network port for `eth0`). You should not have more than one zeroconf service on the same system -- bad things may happen, according to RFC 6762, §15.
- `--with-ssl=openssl`  or `--with-ssl=polarssl` for encryption and related utilities using either OpenSSL or PolarSSL.
- `--with-soxr` for libsoxr-based resampling.
- `--with-piddir` for specifying where the PID file should be stored. This directory is normally chosen automatically. The directory must be writable. If you use this option, you may have to edit the init script to search for the PID file in your new location.

Here is an example, suitable for most installations:

`$ ./configure --with-alsa --with-avahi --with-ssl=openssl --with-soxr`

Omit the `--with-soxr` if the libsoxr library is not available.

`$ make` 

Run `$sudo make install` to install `shairport-sync` along with an init script which will automatically launch it at startup. The settings in the init script are the most basic defaults, so you will want to edit it -- the file is `/etc/init.d/shairport-sync` -- to give the service a name, use a different card, use the hardware mixer and volume control, etc. -- there are some examples in the script file.

Man Page
--------
You can see a web version of the man page here: http://htmlpreview.github.io/?https://github.com/mikebrady/shairport-sync/blob/2.1/man/shairport-sync.html


Configuring Shairport Sync
--------
Shairport Sync installs a default configuration at `/etc/init.d/shairport-sync` (it won't replace an existing one) which should work in almost any system with a sound card. If there is a problem, it will be noted in the logfile, normally `/etc/log/syslog`. However, to get the most out of your software and hardware, you need to adjust some of the settings.

To understand what follows, note that settings and parameters are passed to Shairport Sync through command line arguments. The purpose of the init script at `/etc/init.d/shairport-sync` is to launch or terminate Shairport Sync while passing the correct arguments to it. You are perfectly free to remove the init script and launch and terminate Shairport Sync yourself directly; indeed it is useful when you are troubleshooting the program. If you do launch it directly, make sure it isn't running already!

As well as the man page, don't forget you can launch Shairport Sync with the `-h` option to get some help on the options available.


These are the important options:

The `-a` option allows you to specify the name Shairport Sync will use on the network. If you don't specify a name, the name `Shairport Sync on ...your computer's hostname...` will be chosen.

The `-S` option allows you to specify the kind of "stuffing" or interpolation to be used -- `basic` (default) for simple insertion/deletion  or `soxr` for smoother resampling-based interpolation.

These may be also of interest:

The `-B`, `-E` and `-w` options allow you to specify a program to execute before (`-B`) and after (`-E`) Shairport Sync plays. This is to facilitate situations where something has to be done before and  after playing, e.g. switching on an amplifier beforehand and switching it off afterwards. Use the `-w` option for Shairport Sync to wait until the respective commands have been completed before continuing. Please note that the full path to the programs must be specified, and script files will not be executed unless they are marked as executable and have the standard `#!/bin/...` first line. (This behaviour may be different from other Shairports.)

* The `-V` option gives you version information about  Shairport Sync and then quits.
* The `-k` option causes Shairport Sync to kill an existing Shairport Sync daemon and then quit. You need to have sudo privileges for this.
* The `-v` option causes Shairport Sync to print some information and debug messages.
* The `-d` option causes Shairport Sync to properly daemonise itself, that is, to run in the background. You may need sudo privileges for this.

Apart from arguments of Shairport Sync, there are also arguments for controlling the ALSA audio system. ALSA arguments follow a `--` on the command line -- see the examples below for layout of command line.
These are important because you use them to specify the actual audio device you wish to use and you give Shairport Sync important information about the capabilities of the device. The important ALSA arguments are:

* The `-d` option which allows you to specify the audio device to use. Typical values would be `default` (default), `hw:0`, `hw:1`, etc. Those examples are specifying which *soundcard* to use; the actual output device used is the card's default, typically output device 0. You could specify, for example, device 5 on card hw:0 with `-d hw:0,5`.

The following settings are very important for maximum performance. If your audio device has a hardware mixer and volume control, then Shairport Sync can use it to give faster response to volume and mute commands and it can offload some work from the processor.
* The `-m` option allows you specify where the mixer is. By default, the mixer is to be found where you specify with the `-d` option, so you only need to use the `-m` option if the mixer is elsewhere. This can happen if you specify a *device* rather than a *card* with the `-d` option, because normally a mixer is associated with a *card* rather than a device. For example, if you specified that the output device was device 5 of card hw:0 and if the mixer was associated with the card, you would write `-d hw:0,5 -m hw:0`. 
* The `-t` option allows you to specify the type of audio mixer -- `software` (default) or `hardware`.
* The `-c` option allows you to specify the name of the volume control on the hardware mixer.

The init script at `/etc/init.d/shairport-sync` has a bare minimum set of options (see line 60):

`-d`

Basically all it does is put the program in daemon mode, selects the default output device and uses a software volume control.

*Examples*

Here are some examples of complete commands. If you are modifying the init script, you don't need the `shairport-sync` at the start, but you should include the `-d` option, as it puts the program into daemon mode. There are some commented-out examples in the init script -- see lines 61--63.

- `shairport-sync -d -a "Joe's Stereo" -- -d hw:0`

This gives the service a particular name -- "Joe's Stereo" and specifies that audio device hw:0 be used.


For best results -- including getting true mute and instant response to volume control and pause commands -- you should access the hardware volume controls. Use `amixer` or `alsamixer` or similar to discover the name of the volume controller to be used after the `-c` option.

Here is an example for for a Raspberry Pi using its internal soundcard -- device hw:0 -- that drives the headphone jack:

- `shairport-sync -d -a "Mike's Boombox" -- -d hw:0 -t hardware -c PCM`

Here is an example of using soxr-based resampling and driving a Topping TP30 Digital Amplifier, which has an integrated USB DAC and which is connected as audio device `hw:1`:

- `shairport-sync -d -a Kitchen -S soxr -- -d hw:1 -t hardware -c PCM`

For a cheapo "3D Sound" USB card (Stereo output and input only) on a Raspberry Pi:

- `shairport-sync -d -a "Front Room" -- -d hw:1 -t hardware -c Speaker`

For a first generation Griffin iMic on a Raspberry Pi:

- `shairport-sync -d -a "Attic" -- -d hw:1 -t hardware -c PCM`

For an NSLU2, which has no internal soundcard, there appears to be a bug in ALSA -- you can not specify a device other than "default". Thus:

On an NSLU2, to drive a first generation Griffin iMic:

- `shairport-sync -d -a "Den" -- -t hardware -c PCM`

On an NSLU2, to drive the "3D Sound" USB card:

- `shairport-sync -d -a "TV Room" -- -t hardware -c Speaker`

Latency
-------
Latency is the exact time from a sound signal's original timestamp until that signal actually "appears" on the output of the DAC, irrespective of any internal delays, processing times, etc. in the computer. From listening tests, it seems that there are two latencies in current use:
* If the source is iTunes 10 or later, a latency of 99,400 frames seems to bring Shairport Sync into exact synchronisation both with the speakers on the iTunes computer itself and with AirPort Express receivers.
* If the source is an AirPlay device, the latency seems to be exactly 88,200 frames. AirPlay devices include AppleTV, iPod, iPad and iPhone and Quicktime Player on Mac. 
* If the source cannot be identified as AirPlay or as iTunes 10 or later, then the default latency of 88,200 frames seems to work in general. Note that some third party programs masquerade as older versions of iTunes.

Shairport Sync uses the latencies described above as defaults. You shouldn't need to change them, but occasionally problems arise when you are trying to synchronise with speaker systems -- typically surround-sound home theatre systems -- that have their own inherent delays. You can set the default latency with the `-L` or `--latency` option (e.g. `-L 99400` or `--latency=99400`). You can set your own iTunes 10 (or later) latency with the `-i` or `--iTunesLatency` option. Similarly you can set an AirPlay latency with the `-A` or `--AirPlayLatency` option.

Resynchronisation
-------------
Shairport Sync actively maintains synchronisation with the source. 
If synchronisation is lost -- say due to a busy source or a congested network -- Shairport Sync will mute its output and resynchronise. The loss-of-sync threshold is a very conservative 50 ms -- i.e. the actual time and the expected time must differ by more than 50 ms to trigger a resynchronisation. Smaller disparities are corrected by insertions or deletions, as described above.
* You can vary the resync threshold, or turn resync off completely, with the `-r` opton.

Some Statistics
---------------
If you add the option `--statistics`, e.g. as follows for the Raspberry Pi with "3D Sound" card:

`shairport-sync -a "Shairport Sync" --statistics -- -d hw:1 -t hardware -c Speaker`

it will print statistics like this occasionally on the console (or in the logfile if running in daemon mode):

`Sync error: -35.4 (frames); net correction: 24.2 (ppm); corrections: 24.2 (ppm); missing packets 0; late packets 5; too late packets 0; resend requests 6; min DAC queue size 4430.`

"Sync error" is the average deviations from exact synchronisation. The example above indicates that the output is on average 35.4 frames ahead of exact sync. Sync is allowed to wander by ± 88 frames (± 2 milliseconds) before a correction will be made.

"Net correction" is actually the net sum of corrections -- the number of frame insertions less the number of frame deletions -- given as a moving average in parts per million. After an initial settling period, it represents the divergence between the source clock and the sound device's clock. The example above indicates that the output DAC's clock is running 24.2 ppm faster than the source's clock.

"Corrections" is the number of frame insertions plus the number of frame deletions (i.e. the total number of corrections), given as a moving average in parts per million. The closer this is to the absolute value of the drift, the fewer "unnecessary" corrections that are being made. Third party programs tend to have much larger levels of corrections.

For reference, a drift of one second per day is approximately 11.57 ppm. Left uncorrected, even a drift this small between two audio outputs will be audible after a short time. The above sample is from a second-generation iPod driving the Raspberry Pi which is connected over Ethernet.

It's not unusual to have resend requests, late packets and even missing packets if some part of the connection to the Shairport Sync device is over WiFi. Sometimes late packets can be asked for and received multiple times. Sometimes late packets are sent and arrive too late, but have already been sent and received in time, so weren't needed anyway...

"Min DAC queue size" is the minimum size the queue of samples in the output device's hardware buffer was measured at. It is meant to stand at 0.15 seconds = 6,615 samples, and will go low if the processor is very busy. If it goes below about 2,000 then it's a sign that the processor can't really keep up.
