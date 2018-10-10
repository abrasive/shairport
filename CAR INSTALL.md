# Shairport Sync for Cars
If your car audio has an AUX input, you can get AirPlay in your car using Shairport Sync. Together, Shairport Sync and an iPhone can give you access to internet radio, YouTube, Apple Music, Spotify, etc. on the move. While Shairport Sync is  no substitute for CarPlay, the audio quality is often much better than Bluetooth. Your passengers can enjoy movies with the soundtrack on the car speakers. You can even listen to Siri's traffic directions on your car audio. 

## The Basic Idea

The basic idea is to use a small Linux computer to create an isolated WiFi network for the car and to run Shairport Sync on it to provide an AirPlay service. The audio goes via a DAC to the AUX input of your car audio system.

The car WiFi network you create is isolated and local to your car, and since it isn't connected to the Internet, you don't really need to secure it with a password. Likewise, you don't really have to use a password to connect to the AirPlay service.

When an iPhone or an iPad with cellular capability is connected to an isolated WiFi network like this, it can use its cellular connection to access the Internet.
This means it can connect to internet radio, YouTube, Apple Music, Spotify, etc. over the cellular network and play the audio through the car network to the AirPlay service provided by Shairport Sync.

Note that Android devices can not, so far, do this trick of using the two networks simultaneously.

## Example

In this example, a Raspberry Pi Zero W and a Pimoroni PHAT DAC are used. This combination has been tested for well over a year. Please note that some of the details of setting up networks are specific to the version of Linux used -- Raspbian Stretch.

### Prepare the initial SD Image
* Download the latest version of Raspbian Lite -- Stretch Lite of 2018-03-13 at the time of writing -- and install it onto an SD Card.
* Mount the card on a Linux machine. Two drives should appear -- a `boot` drive and a `rootfs` drive. Both of these need a little modification.
* Enable SSH service by creating a file called `ssh` on the `boot` drive. To do this, mount the drive and CD to its `boot` partiton (since my username is `mike`, the drive is at `/media/mike/boot`):
```
$ touch ssh
```
* Also in the `boot` drive, edit the `config.txt` file to add the overlay needed for the sound card. This may not be necessary in your case, but in this example a Pimoroni PHAT is being used and it needs the following entry to be added:
```
dtoverlay=hifiberry-dac
```
* Next, some modifications need to be done to the `rootfs` drive to make the Pi connect to your main WiFi network. (This is a temporary measure to enable you to connect the Pi to your main network so that you can do all the software installation and updating of the software necessary. Later, the Pi will be configured to start its own isolated network.) On the `rootfs` drive, edit the file `etc/wpa_supplicant/wpa_supplicant.conf` (you'll need root privileges) and add the name and password of your main WiFi network (substitute your own network name and password in, but keep the quotation marks):
```
network={
    ssid="Network Name"
    psk="Password"
}

```
Close the file and carefully dismount and eject the two drives. Remove the SD card from the Linux machine, insert it into the Pi and reboot. After a short time, the Pi should appear on your network and you can SSH into it. To check that it has appeared on the network, try to ping it at `raspberrypi.local`. It may take a minute or so to appear. Once it has appeared on your network you can SSH into it and configure it.

### Boot, Configure, Update 
The first thing to do on a Pi would be to use the `raspi-config` tool to expand the file system to use the entire card. It might be useful to change the `hostname` too. Next, do the usual update and upgrade:
```
# apt update
# apt upgrade
# rpi-update
``` 
**Note:** If you are following this guide but are using a full-size Rapsberry Pi with the built-in DAC, then, at this time of writing (September 2018) it is a good idea to update the Pi's firmware using `rpi-update` because a [significant improvement](https://github.com/raspberrypi/firmware/commit/200c2f4dd54b2048b5dcb8661ea3f232beb7d81e) has been made to the [timing software](https://github.com/raspberrypi/firmware/issues/1026) of the built-in audio DAC's drivers. It should be incorporated in firmware from Raspbian 4.14.66-v7 onwards.

(Separately, if you haven't done so already, consider using the `raspi-config` tool to expand the file system to use the entire card.)

### Shairport Sync
First, install the packages needed by Shairport Sync:
```
# apt install build-essential git xmltoman autoconf automake libtool libdaemon-dev libpopt-dev libconfig-dev libasound2-dev avahi-daemon libavahi-client-dev libssl-dev
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
SoX interpolaton is not included, as the Pi Zero would not be fast enough. *Do not* enable Shairport Sync to automatically start at boot time -- startup is organised differently.

Third, finish by configuring Shairport Sync.
Here are the important options for the Shairport Sync configuration file at `/etc/shairport-sync.conf`:
```
// Sample Configuration File for Shairport Sync for Car Audio with a Pimoroni PHAT
general =
{
	name = "BMW Radio";
	ignore_volume_control = "yes";
	volume_max_db = -3.00;
};

alsa =
{
	output_device = "hw:1"; // the name of the alsa output device. Use "alsamixer" or "aplay" to find out the names of devices, mixers, etc.
	output_format = "S32"; // can be "U8", "S8", "S16", "S24" or "S32", with be LE or BE depending on the processor, but the device must be capable of it
};

```
Two `general` settings are worth noting. First, the option to ignore the sending device's volume control is enabled -- this means that the car audio's volume control is the only one that affects the audio volume. Of course this is a matter of personal preference.
Second, the maximum output offered by the DAC to the AUX port of the car audio can be reduced if it is overloading the input circuits. Again, that's a matter for personal selection and adjustment.

The `alsa` settings are specific to the Pimoroni PHAT -- it does not have a hardware mixer and it does have a 32-bit capability which is worth enabling.

### Extra Packages
A number of packages to enable the Pi to work as a WiFi base station are needed:
```
# apt install hostapd isc-dhcp-server
```
Disable both of these services from starting at boot time (this is because we will launch them sequentially later on):
```
# systemctl disable hostapd
# systemctl disable isc-dhcp-server
```
#### Configure HostAPD
Configure `hostapd` by creating `/etc/hostapd/hostapd.conf` with the following contents which will set up an open network with the name BMW. You might wish to change the name:
``` 
# This is the name of the WiFi interface we configured above
interface=wlan0

# Use the nl80211 driver with the brcmfmac driver
driver=nl80211

# This is the name of the network -- yours might be different
ssid=BMW

# Use the 2.4GHz band
hw_mode=g

# Use channel 6
channel=9

# Enable 802.11n
ieee80211n=1

# Enable WMM
wmm_enabled=1

# Enable 40MHz channels with 20ns guard interval
#ht_capab=[HT40][SHORT-GI-20][DSSS_CCK-40]

# Accept all MAC addresses
macaddr_acl=0

# Use WPA authentication
#auth_algs=1

# Require clients to know the network name
ignore_broadcast_ssid=0

# Use WPA2
#wpa=2

# Use a pre-shared key
#wpa_key_mgmt=WPA-PSK

# The network passphrase
#wpa_passphrase=none

# Use AES, instead of TKIP
#rsn_pairwise=CCMP
```
#### Configure DHCP server

First,  replace the contents of `/etc/dhcp/dhcpd.conf` with this:
```
subnet 10.0.10.0 netmask 255.255.255.0 {
     range 10.0.10.5 10.0.10.150;
     #option routers <the-IP-address-of-your-gateway-or-router>;
     #option broadcast-address <the-broadcast-IP-address-for-your-network>;
}
```
Second, modify the INTERFACESv4 entry at the end of the file `/etc/default/isc-dhcp-server` to look as follows:
```
INTERFACESv4="wlan0"
INTERFACESv6=""
```
### Set up the Startup Sequence
Configure the startup sequence by adding commands to `/etc/rc.local` to start `hostapd` and the `dhcp` server and then to start `shairport-sync` automatically after startup. Its contents should look like this:
```
#!/bin/sh -e
#
# rc.local
#
# This script is executed at the end of each multiuser runlevel.
# Make sure that the script will "exit 0" on success or any other
# value on error.
#
# In order to enable or disable this script just change the execution
# bits.
#
# By default this script does nothing.

/sbin/iw dev wlan0 set power_save off
/usr/sbin/hostapd -B -P /run/hostapd.pid /etc/hostapd/hostapd.conf
/sbin/ip addr add 10.0.10.1/24 dev wlan0
/bin/sleep 1
/bin/systemctl start isc-dhcp-server
/bin/sleep 2
/bin/systemctl start shairport-sync

exit 0
```
As you can see, the effect of these commands is to start the WiFi transmitter, give the base station the IP address `10.0.10.1`, start a DHCP server and finally start the Shairport Sync service.

### Final Steps
Up to now, if you reboot the Pi, it will reconnect to your WiFi network, ignoring the instructions and settings you have given it to act as a base station. That is because the `wlan0` interface is still under the control of the `dhcpcd` service. So, the final step is to instruct the `dhcpcd` service not to manage `wlan0`. To do this, edit `/etc/dhcpcd.conf` and insert the following line at the start:
```
denyinterfaces wlan0
```
From this point on, at least on the Raspberry Pi, if you reboot the machine, it will not reconnect to your network – instead, it will act as the WiFi base station you have configured with `hostapd` and `isc-dhcp-server`.

### Ready
Install the Raspberry Pi in your car. It should be powered from a source that is switched off when you leave the car, otherwise the slight current drain will eventually flatten the car's battery.

When the power source is switched on, typically when you start the car, it will take maybe a minute for the system to boot up.
### Enjoy!
