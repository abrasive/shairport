This is a very quick initial note about installing Shairport Sync on FreeBSD. Some manual installation is required.

The build instrctions here install back ends of `sndio` (native to OpenBSD) and ALSA. ALSA is, or course, the Advanced Linux Sound Architecture, so it is not "native" to FreeBSD. It has, however, been ported, so it should work pretty well.

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

Now, install the ALSA and Avahi subsystems. Note: `avahi-app` is chosen because it doesn’t require X11 and `nss_mdns` allows FreeBSD
to resolve mDNS-originated addresses. Thanks to [reidransom](https://gist.github.com/reidransom/6033227) for this:

```
# pkg install alsa-utils avahi-app nss_mdns
```
To enable these services to load automatically at startup, add these lines to `/etc/rc.conf`:
```
dbus_enable="YES"
avahi_daemon_enable="YES"
```
Next, change the `hosts:` line in `/etc/nsswitch.conf` to
```
hosts: files dns mdns
```
Reboot for these changes to take effect.

Next, install the packages that are needed for Shairport Sync to be downloaded and build successfully:
```
# pkg install git autotools pkgconf popt libconfig openssl sndio
```

Now, download Shairport Sync from GitHub and check out the `development` branch.
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ git checkout development
```
Next, configure the build and compile it:

```
$ autoreconf -i -f
$ CPPFLAGS="-I/usr/local/include" ./configure  --with-alsa --with-avahi --with-sndio --with-ssl=openssl --with-metadata
$ make
```

After this, you're on your own – the `$ sudo make install` step does not work for FreeBSD. Maybe some day...

To continue, you should create a configuration file at `/usr/local/etc/shairport-sync.conf`. Please see the sample configuration file for more details.

