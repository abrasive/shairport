Simple Installation Instructions
==
Here are really simple instructions for building and installing Shairport Sync on a Raspberry Pi B, 2B, 3B or 3B+. It is assumed that the Pi is running Raspbian Stretch Lite – a GUI isn't needed, since Shairport Sync runs as a daemon program.

### Configure and Update
Do the usual update and upgrade:
```
# apt-get update
# apt-get upgrade
# rpi-update
``` 
(Separately, if you haven't done so already, consider using the `raspi-config` tool to expand the file system to use the entire card.)

### Activate the Improved Audio Driver
Check the file `/boot/config.txt` and, if it's not there already, add the following line:
```
audio_pwm_mode=2
```
Reboot.

### Remove Old Copies
Before you begin building Shairport Sync, it's best to search for and remove any existing copies of the applicatioon, called `shairport-sync`. Use the command `$ which shairport-sync` to find them. For example, if `shairport-sync` has been installed previously, this might happen:
```
$ which shairport-sync
/usr/local/bin/shairport-sync
$ sudo rm /usr/local/bin/shairport-sync
...
```
You can see that the `which` command located a copy of `shairports-sync` in the direcotry `/usr/local/bin` and then we removed it with the `rm` command. Do this until no more copies of `shairport-sync` are found.

### Build and Install
Okay, now let's get the tools and sources for building and installing Shairport Sync.

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

Now to configure Shairport Sync. Here are the important options for the Shairport Sync configuration file at `/etc/shairport-sync.conf`:
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
The next step is to enable Shairport Sync to start automatically on boot up:
```
$ sudo systemctl enable shairport-sync
```
Finally, either reboot the Pi or start the `shairport-sync` service:
```
$ sudo systemctl start shairport-sync
```
The Shairport Sync AirPlay service should now appear on the network with a service name made from the Pi's hostname with the first letter capitalised, e.g. hostname `raspberrypi` gives a service name `Raspberrypi`. Connect to it and enjoy...
