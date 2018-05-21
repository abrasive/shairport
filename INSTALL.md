Simple Installation Instructions
==
Shairport Sync can be installed in lots of different Linux and Unix machines, and there are lots of variations to consider -- see the [README.md](https://github.com/mikebrady/shairport-sync/blob/master/README.md) page for a fuller discussion. But here are really simple instructions for building and installing it on a Raspberry Pi B, 2B, 3B or 3B+. It is assumed that the Pi is running Raspbian Stretch Lite -- a GUI isn't needed, since Shairport Sync runs as a daemon program.

### Configure and Update
Do the usual update and upgrade:
```
# apt-get update
# apt-get upgrade
# rpi-update
``` 
(Separately, if you haven't done so already, consider using the `raspi-config` tool to expand the file system to use the entire card.)

### Activate the Improved Audio Driver
Check the file `/boot/config.txt` and, if it's not there already, edit it to add the following line:
```
audio_pwm_mode=2
```
Reboot.

### Remove Old Copies
Before you begin building Shairport Sync, it's best to search and remove any existing copies. Use the command `$ which shairport-sync` to find them. For example, if `shairport-sync` has been installed previously, this might happen:
```
$ which shairport-sync
/usr/local/bin/shairport-sync
$ sudo rm /usr/local/bin/shairport-sync
...
```
Do this until no more are found.

### Build and Install
Okay,now let's get the tools and sources for building and installing Shairport Sync.

First, install the packages needed by Shairport Sync:
```
# apt-get install build-essential git xmltoman autoconf automake libtool libdaemon-dev libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev
```
Next, download Shairport Sync, configure it, compile and install it:
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
$ autoreconf -fi
$ ./configure --sysconfdir=/etc --with-alsa --with-avahi --with-ssl=openssl --with-systemd
$ make
$ sudo make install
```

Now to comfigure Shairport Sync.
Here are the important options for the Shairport Sync configuration file at `/etc/shairport-sync.conf`:
```
// Sample Configuration File for Shairport Sync on a Raspberry Pi using the built-in audio DAC
general =
{
  drift_tolerance_in_seconds = 0.010;
  volume_range_db = 50;
};

alsa =
{
	output_device = "hw:0";
  mixer_control_name = "PCM";
};

```
Fourth, enable Shairport Sync to start automatically on boot up:
```
$ sudo systemctl enable shairport-sync
```
Fifth, either reboot the Pi or start the service:
```
$ sudo systemctl start shairport-sync
```
Sixth, enjoy!
