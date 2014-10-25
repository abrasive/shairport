Shairport Sync
=============
What is Shairport Sync?
----------
Shairport Sync emulates an AirPort Express for the purpose of streaming audio from iTunes, iPods, iPhones, iPads and AppleTVs.
Audio played by a Shairport Sync-powered device stays in synchrony with the source and thus with other devices that are playing the same source synchronously. Thus, for example, synchronised multi-room audio is possible without difficulty. 

Shairport Sync does not support AirPlay video and photo streaming.

Version 2.1.8:
-----
* Enhancements
	* (This feature is intended to be useful to integrators.) Shairport Sync now the ability to immediately disconnect and reconnect to the sound output device while continuing to stream audio data from its client.
Send a `SIGUSR2` to the shairport-sync process to disconnect or send it a `SIGHUP` to reconnect. If shairport-sync has been started as a daemon using `shairport-sync -d`, then executing `shairport-sync -D` or `--disconnectFromOutput` will request the daemon to disconnect, and executing `shairport-sync -R` or `--reconnectToOutput` will request it to reconnect.
With this feature, you can allow Shairport Sync always to advertise and provide the streaming service, but still be able to disconnect it locally to enable other audio services to access the output device.
	
* Annoying things you should know about if you're updating from a previous version:
	* Options `--with-openssl`, `--with-polarssl` have been replaced with a new option `--with-ssl=<option>` where `<option>` is either `openssl` or `polarssl`.
	* Option `--with-localstatedir` has been replaced with `--with-piddir`. This compilation option allows you to specify the directory in which the PID file will be written. The directory must exist and be writable. Supercedes the `--with-localstatedir` and describes the intended functionality a little more accurately.

* Bugfixes
	* A small (?) bug in the flush logic has been corrected. Not causing any known problem.

Version 2.1.5:
-----
* Enhancements
	* Adds a `--with-localstatedir` configuration option. When Shairport Sync is running as a daemon, it writes its Process ID (PID) to a file. The file must be stored in part of the file system that is writable. Most build systems choose an appropriate 'local state directory' for this automatically, but some -- notably `buildroot`  -- don't always get it right for an embedded system. This compilation option allows you to specify the local state directory. Supersedes 2.1.4, which tried to do the same thing.

Version 2.1.4:
-----
* Faulty -- withdrawn. 2.1.5 does it properly.


Version 2.1.3:
-----
* Stability Improvements
	* Fixed a bug which prevented Shairport Sync starting on an IPv4-only system.

Version 2.1.2:
-----
* Stability Improvements
	* Improved buffering and flushing control, especially important on poor networks.


Version 2.1.1:
-----
* Enhancements
	* Add new -t or --timeout option. Normally, when playing audio from a source, the Shairport Sync device is unavailable to other devices requesting to play through it -- it returns a "busy" signal to those devices. If the audio source disappears without warning, the play session automatically terminates after a timeout period (default 120 seconds) and the device goes from being "busy" to being available for new play requests again. This option allows you to set that timeout period in seconds.
In addition, setting the timeout period to 0 means that play requests -- say from other devices on the network -- can interrupt and terminate the current session at any time. In other words, the "busy" feature of the device -- refusing connections from other players while playing from an existing source -- is turned off. 
	* Allow -B and -E commands to have arguments, e.g. -B '/usr/bin/logger "Starting to play"' is now legitimate.

* Annoying things you should know about if you're updating from 2.1:
	* Build now depends on the library libpopt -- see "Building and Installing" below.

* Stability Improvements
	* Fixed a bug which would silence output after a few hours.
	* Tightened up management of packet buffers.
	* Improved estimate of lead-in silence to achieve initial synchronisation.

Version 2.1:
-----

* New features:

	* Support for libsoxr, the SoX Resampler library -- see http://sourceforge.net/projects/soxr/. Briefly, Shairport Sync keeps in step with the audio source by deleting or inserting frames of audio into the stream as needed. This "interpolation" is normally inaudible, but it can be heard in some circumstances. Libsoxr allows this interpolation to be done much more smoothly and subtly. You can optionally include libsoxr support when building Shairport Sync. The big problem with libsoxr is that it is very compute intensive -- specifically floating point compute intensive -- and many embedded devices aren't powerful enough. Another issue is libsoxr is not yet in all linux distributions, so you might have to build it yourself. Available via the -S option.
	* Support for running (and optionally waiting for the completion of) programs before and after playing.  See the -B, -E and -w options.
	* A new option to vary or turn off the resync threshold. See the -r option.
	* Version and build options. See the -V option.
	* Renamed program and init script. This is not exactly a big deal, but the name of the application itself and the default init script file have been renamed from "shairport" to "shairport-sync" to avoid confusion with other versions of shairport.
	* PolarSSL can be used in place of OpenSSL and friends.
	
* Other stuff
	* Tinysvcmdns works as an alternative to, say, Avahi, but is now [really] dropped if you don't select it. Saves about 100k.
	* Lots of bug fixes.

* Annoying things you should know about if you're updating from 2.0:
	* Compile options have changed -- see the Building and Installing section below.
	* The name of the program itself has changed from shairport to shairport-sync. You should remove the old version -- you can use `$which shairport` to locate it.
	* The name of the init script file has changed from shairport to shairport-sync. You should remove the old one.

Version 2.0
----

* New features:
 * Audio synchronisation that works. The audio played by a Shairport Sync-powered device will stay in sync with the source. This allows you to synchronise Shairport Sync devices reliably with other devices playing the same source. For example, synchronised multi-room audio is possible without difficulty.
 * True mute and instant response to mute and volume control changes -- this requires hardware mixer support, available on most audio devices. Without hardware mixer support, response is also faster than before -- around 0.15 seconds.
 * Smoother volume control at the top and bottom of the range.
 * Another source can not interrupt an existing source playing via Shairport Sync. it will be given a 'busy' signal.

Overview
------
Shairport Sync allows you to synchronise the audio coming from all your devices. Specifically, Shairport Sync allows you to set the "latency" -- the time between when a sound is sent and when it is played. This allows you to synchronise Shairport Sync devices reliably with other devices playing the same source. For example, synchronised multi-room audio is possible without difficulty.

Shairport Sync is a pretty substantial rewrite of Shairport 1.0 by James Laird and others -- please see https://github.com/abrasive/shairport/blob/master/README.md#contributors-to-version-1x for a list of the contributors to Shairport 1.x and Shairport 0.x. From a "heritage" point of view, Shairport Sync is a fork of Shairport 1.0 and the active branch is called Shairport Sync 2.1.

Shairport Sync works only with Linux and ALSA. The sound card you use must be capable of working with 44,100 samples per second interleaved PCM stereo (you'll get a message in the logfile if there's a problem).

For more about the motivation behind Shairport Sync, please see the wiki at https://github.com/mikebrady/shairport-sync/wiki.

Shairport Sync does Audio Synchronisation
---------------------------
Shairport Sync allows you to set a delay (a "latency") from when music is sent by iTunes or your iOS device to when it is played in the Shairport Sync audio device. The latency can be set to match the latency of other output devices playing the music (or video in the case of the AppleTV or Quicktime), achieving audio synchronisation. Shairport Sync uses extra timing information to stay synchronised with the source's time signals, eliminating "drift", where audio streams slowly drift out of synchronisation.
Shairplay Sync automatically chooses a suitable latency for iTunes and for AirPlay devices such as the AppleTV.

To stay synchronised, if an output device is running slow, Shairport Sync will delete frames of audio to allow it to keep up; if the device is running fast, Shairport Sync will insert frames to keep time. The number of frames inserted or deleted is so small as to be almost  inaudible. Frames are inserted or deleted as necessary at pseudorandom intervals.

Alternatively, with libsoxr support, Shairport Sync can resample the audio feed to ensure the output device can keep up. Resampling is even less obtrusive than insertion and deletion but requires a good deal of processing power -- most embedded devices probably can't support it.

What else?
--------------
* Shairport Sync offers finer control at very top and very bottom of the volume range. See http://tangentsoft.net/audio/atten.html for a good discussion of audio "attenuators", upon which volume control in Shairport Sync is modelled. See also the diagram of the volume transfer function in the documents folder.
* Shairport Sync will mute properly if the hardware supports it.
* If Shairport Sync has to use software volume and mute controls, the response time is shorter than before -- now it responds in 0.15 seconds.
* Shairport Sync sends back a "busy" signal if it's already playing audio from another source, so other sources can't "barge in" on an existing Shairport Sync session. (If a source disappears without warning, the session automatically terminates after two minutes and the device becomes available again.)

Status
------
Shairport Sync works on standard Ubuntu laptops, on the Raspberry Pi with Raspian and OpenWrt, and it runs on a Linksys NSLU2 using OpenWrt. It works with built-in audio and with a variety of USB-connected audio amplifiers and DACs, including a cheapo USB "3D Sound" dongle, a first generation iMic and a Topping TP30 amplifier with a USB DAC input.

Shairport Sync runs well on the Raspberry Pi. It can drive the built-in sound card, and though you wouldn't mistake the output for HiFi, it's really not too shabby. USB-connected sound cards work well on the latest version of Raspian; however older versions of Raspian appear to suffer from a problem -- see http://www.raspberrypi.org/forums/viewtopic.php?t=23544, so it is wise to update. Shairport Sync works very well with the IQAudIO Pi-DAC -- see http://iqaudio.com/wp-content/uploads/2014/06/IQaudIO_Doc.pdf for details.

Shairport Sync runs on Ubuntu and Debian inside VMWare Fusion 6 on a Mac, but synchronisation does not work -- possibly because the soundcard is being emulated.

Shairport Sync works only with the ALSA back end. You can try compiling the other back ends in as you wish, but it definitely will not work properly with them. Maybe someday...

One other difference from other versions of Shairport is that the Shairport Sync build process uses GNU autotools and libtool to examine and configure the build environment -- very important for cross compilation. All previous versions looked in the current system to determine which packages were available, instead of looking at what packages were available in the target system.

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

Debian, Ubuntu and Raspian users can get the basics with:

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
- `--with-avahi` or `--with-tinysvcmdns` for mdns support.
- `--with-ssl=openssl`  or `--with-ssl=polarssl` for encryption and related utilities using either OpenSSL or PolarSSL.
- `--with-soxr` for libsoxr-based resampling.
- `--with-piddir` for specifying where the PID file should be stored. This directory is normally chosen automatically. The directory must be writable. If you use this option, you may have to edit the init script to search for the PID file in your new location.

Here is an example:

`$ ./configure --with-alsa --with-avahi --with-ssl=openssl --with-soxr`

`$ make` 

Run `$sudo make install` to install `shairport-sync` along with an init script which will automatically launch it at startup. The settings in the init script are the most basic defaults, so you will want to edit it -- the file is `/etc/init.d/shairport-sync` -- to give the service a name, use a different card, use the hardware mixer and volume control, etc. -- there are some examples in the script file.

Configuring Shairport Sync
--------
Shairport Sync installs a default configuration at `/etc/init.d/shairport-sync` (it won't replace an existing one) which should work in almost any system with a sound card. If there is a problem, it will be noted in the logfile, normally `/etc/log/syslog`. However, to get the most out of your software and hardware, you need to adjust some of the settings.

To understand what follows, note that settings and parameters are passed to Shairport Sync through command line arguments. The purpose of the init script at `/etc/init.d/shairport-sync` is to launch or terminate Shairport Sync while passing the correct arguments to it. You are perfectly free to remove the init script and launch and terminate Shairport Sync yourself directly; indeed it is useful when you are troubleshooting the program. If you do launch it directly, make sure it isn't running already!

Don't forget you can launch Shairport Sync with the `-h` option to get some help on the options available.

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

* The `-d` option which allows you to specify the audio device to use. Typical values would be `default` (default), `hw:0`, `hw:1`, etc.

The following two settings are very important for maximum performance. If your audio device has a hardware mixer and volume control, then Shairport Sync can use it to give faster response to volume and mute commands and it can offload some work from the processor.

* The `-t` option allows you to specify the type of audio mixer -- `software` (default) or `hardware`.
* The `-c` option allows you to specify the name of the volume control on the hardware mixer.

The init script at `/etc/init.d/shairport-sync` has a bare minimum set of options (see line 60):

`-d`

Basically all it does is put the program in daemon mode, selects the default output device and uses software volume control.

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
* If the source is iTunes, a latency of 99,400 frames seems to bring Shairport Sync into exact synchronisation both with the speakers on the iTunes computer itself and with AirPort Express receivers.
* If the source is an AirPlay device, the latency seems to be exactly 88,200 frames. AirPlay devices include AppleTV, iPod, iPad and iPhone and Quicktime Player on Mac.

Shairport Sync uses these as default latencies for iTunes and AirPlay sources.

You can set your own iTunes latency with the `-i` or `--iTunesLatency` option (e.g. `-i 99400` or `--iTunesLatency=99400`). Similarly you can set an AirPlay latency with the `-A` or `--AirPlayLatency` option. If you set a latency with `-L` it overrides both the AirPlay and iTunes latency values. Basically, you shouldn't use it except for experimentation. It is deprecated and will be removed soon.

Resynchronisation
-------------
Shairport Sync actively maintains synchronisation with the source. 
If synchronisation is lost -- say due to a busy source or a congested network -- Shairport Sync will mute its output and resynchronise. The loss-of-sync threshold is a very conservative 50 ms -- i.e. the actual time and the expected time must differ by more than 50 ms to trigger a resynchronisation. Smaller disparities are corrected by insertions or deletions, as described above.
* You can vary the resync threshold, or turn resync off completely, with the `-r` opton.

Some Statistics
---------------
If you run Shairport Sync from the command line without daemonising it (omit the `-d`), and if you turn on one level of verbosity (include `-v`), e.g. as follows for the Raspberry Pi with "3D Sound" card:

`shairport-sync -a "Shairport Sync" -v -- -d hw:1 -t hardware -c Speaker`

it will print statistics like this occasionally:

`Sync error: -35.4 (frames); net correction: 24.2 (ppm); corrections: 24.2 (ppm); missing packets 0; late packets 5; too late packets 0; resend requests 6; min DAC queue size 4430.`

"Sync error" is the average deviations from exact synchronisation. The example above indicates that the output is on average 35.4 frames ahead of exact sync. Sync is allowed to wander by ± 88 frames (± 2 milliseconds) before a correction will be made.

"Net correction" is actually the net sum of corrections -- the number of frame insertions less the number of frame deletions -- given as a moving average in parts per million. After an initial settling period, it represents the divergence between the source clock and the sound device's clock. The example above indicates that the output DAC's clock is running 24.2 ppm faster than the source's clock.

"Corrections" is the number of frame insertions plus the number of frame deletions (i.e. the total number of corrections), given as a moving average in parts per million. The closer this is to the absolute value of the drift, the fewer "unnecessary" corrections that are being made.

For reference, a drift of one second per day is approximately 11.57 ppm. Left uncorrected, even a drift this small between two audio outputs will be audible after a short time. The above sample is from a second-generation iPod driving the Raspberry Pi which is connected over Ethernet.

It's not unusual to have resend requests, late packets and even missing packets if some part of the connection to the Shairport Sync device is over WiFi. Sometimes late packets can be asked for and received multiple times. Sometimes late packets are sent and arrive too late, but have already been sent and received in time, so weren't needed anyway...

"Min DAC queue size" is the minimum size the queue of samples in the output device's hardware buffer was measured at. It is meant to stand at 0.15 seconds = 6,615 samples, and will go low if the processor is very busy. If it goes below about 2,000 then it's a sign that the processor can't really keep up.
