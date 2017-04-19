Introduction
----
This is a very quick initial note about installing Shairport Sync on FreeBSD. Some manual installation is required.

The build instructions here install back ends for `sndio` (native to OpenBSD) and ALSA. ALSA is, or course, the Advanced Linux Sound Architecture, so it is not "native" to FreeBSD, but has been ported to some architectures under FreeBSB.

General
----
This build was done on a default build of `FreeBSD 11.0-RELEASE-p9`.

First, update everything:
```
# freebsd-update fetch
# freebsd-update install
```
Next, install the `pkg` package manager and update its lists:

```
# pkg
# pkg update
```

Subsystems
----
Now, install the Avahi subsystem. FYI, `avahi-app` is chosen because it doesn’t require X11 and `nss_mdns` is included to allow FreeBSD to resolve mDNS-originated addresses -- it's not needed by Shairport Sync. Thanks to [reidransom](https://gist.github.com/reidransom/6033227) for this.

```
# pkg install avahi-app nss_mdns
```
Add these lines to `/etc/rc.conf`:
```
dbus_enable="YES"
avahi_daemon_enable="YES"
```
Next, change the `hosts:` line in `/etc/nsswitch.conf` to
```
hosts: files dns mdns
```
Reboot for these changes to take effect.

Building
----

Next, install the packages that are needed for Shairport Sync to be downloaded and build successfully:
```
# pkg install git autotools pkgconf popt libconfig openssl sndio alsa-utils
```
Omit `alsa-utils` if you're not using ALSA. Likewsie, omit `sndio` if you don't intend to use the `sndio` subsystem.

Now, download Shairport Sync from GitHub and check out the `development` branch.
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ git checkout development
```
Next, configure the build and compile it:

```
$ autoreconf -i -f
$ CPPFLAGS="-I/usr/local/include" ./configure  --with-avahi --with-ssl=openssl --with-alsa --with-sndio
$ make
```
Omit `--with-alsa` if you don;t want ot include the ALSA back end and omit the `--with-sndio` if you don't want the `sndio` back end.

Manual Installation
----
After this, you're on your own – the `$ sudo make install` step does not work for FreeBSD. Maybe some day...

To continue, you should create a configuration file at `/usr/local/etc/shairport-sync.conf`. Please see the sample configuration file for more details.

Using the `sndio` backend
----

The `sndio` back end does synchronisation and is still under development. Right now, it seems to work very well. It does not have a mixer control facility, however. You should set the volume to maximum before use, using, for example, the `mixer` command described below.

Setting Overall  Volume
----
Note, the `mixer` command can be used for setting the output device's volume settings. You may hae to experiment to figure out which settings are appropriate.

```
$ mixer vol 100 # sets overall volume
```
