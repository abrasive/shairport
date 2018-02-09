Shairport Sync on OpenBSD using `sndio`
----
This is an initial note about installing Shairport Sync on OpenBSD. Shairport Sync compiles and maybe runs (?) natively on OpenBSD using the `sndio` back end.

General
----
This build was done on a default build of `OpenBSD 6.2 GENERIC.MP#134 amd64`.

First, update everything.
```
TBA
```
Install the Avahi subsystem. 
```
TBA
```
Enable the D-Bus and Avahi subsystems
```
TBA
```
Building
----

Install the packages that are needed for Shairport Sync to be downloaded and built successfully.
```
TBA
```
Now, download Shairport Sync from GitHub:
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
```
Next, configure the build and compile it:

```
$ autoreconf -i -f
$ ./configure  --with-avahi --with-ssl=openssl --with-sndio --with-os=openbsd
$ make
```

There is no make install yet -- you're on your own.

Using the `sndio` backend
----

The `sndio` back end does not have a hardware volume control facility.
You should set the volume to maximum before use, using, for example, the `mixerctl` command.
