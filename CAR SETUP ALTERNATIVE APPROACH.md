# From [FnasBas](https://github.com/FnasBas)
This is based on [this comment](https://github.com/mikebrady/shairport-sync/issues/482#issuecomment-402543454).

Here's a starting point. I hope it is useful to someone :)

# Alternative car setup approach
This is offered in the hopes it will be useful to someone. The background is that, besides all the advantages of WiFi/AirPlay over Bluetooth,
when travelling my phone is my only internet connection, and I want to be able to share it with other devices.
Like an iPad or laptop. I may also want to be able to send audio to the car Shairport Sync service from my other devices while
simultaneously having internet access on them, via the phone.

So the basic idea is just to turn the table on which device acts as the Access Point for the WiFi. Which works perfectly well.
The prerequisite is that your carrier/contract allows tethering, as otherwise you won't be able to have your phone act as a WiFi Access Point.
I also think this is a bit simpler to set up as you don't need to set up `hostapd` and a DHCP server on the Pi.
The drawback is that you need to do a gesture and tap a button on the phone to make its network discoverable when entering your car.

## Phone configuration
Enable tethering on your iPhone with a secure passphrase. 

## Raspberry Pi configuration
Set up Shairport Sync according to your needs and instructions found elsewhere.

### Network
Configure your Raspberry Pi with only one WiFi network. Through experimentation I found that if I was in the vicinity of my home network,
the Pi would not reliably choose the iPhone's WiFi network even though it was prioritized over the home network.
And it seems `wpa-supplicant` in general is a bit difficult to get to roam between different WiFi networks once it's decided on one.

Example of `/etc/wpa\_supplicant/wpa\_supplicant.conf`

```
ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
update_config=1  
country=SE  
network={  
	priority=3  
	ssid="My unique clever wifi network name"  
	psk="My very secure wifi key"  
}  
```

### Read only root filesystem
By configuring your Pi to have a read only root filesystem, you need not ever worry about an improper shutdown of the OS
causing the Pi to fail to boot. You will also be sure that the Pi always boots to a predetermined state.
And you will eleminate wear on the SD card completely except for when modifying the system.
This will result in a very reliable AirPlay service. 

These instructions are a bit old but a good starting point: (https://openenergymonitor.org/forum-archive/node/5331.html) 

In addition to what's laid out there I've also:

* Added the `/boot` fs to be mounted read-only and modified the rpi-rw/ro scripts and `/etc/fstab` accordingly
* in `/etc/systemd/journald.conf`, set `storage=volatile`
* As the `systemd` `ntp` client is hard-coded to use a specific path for its book keeping, I've disabled that and installed `ntpd`
and set the drift file to `/tmp/drift`
* Your DHCP client will complain that it can't write its lease to a file. You can work around this by setting it to write its lease
to a file in `/tmp` or disregard it as it will work fine in any case.

In my case I do not use a car stereo but go straight Pi>Dac>Power amplifier. So I've also set up EQ with `alsa`, and a relay module to a
GPIO pin and some scripts that run on session start/stop that turns the power amplifier on and off accordingly.
But this could turn in to "How I set up my car audio" rather than offering an alternative to [what's currently suggested
for a car installation](https://github.com/mikebrady/shairport-sync/blob/master/CAR%20INSTALL.md).
So I stop. ;)

## Using it
Make your phone's network discoverable. In iOS 11 there's a shortcut for this – swipe up and hold down on the connectivity section.
There's a button to turn on Internet Sharing.
In my experiments the Pi connects almost instantaneously to this network and a few moments later the AirPlay service offered by Shairport Sync
will become visible.

## Changing and Maintaining it
Make your phone network discoverable. The Pi connects. Connect your laptop to the same network.
The iPhone does not appear to resolve host names from one tethered device to another so you need to `ssh` to the Pi's IP-address.
There's a number of ways to find the Pi's IP-address:

* You can check your laptop's IP and try subtracting or adding from the last octet
* Use a `zeroconf/avahi` browser tool and locate the AirPlay service and find the IP address that way
* Use `nmap` or other similar utility to scan for the Pi

`ssh` to the Pi
```
rpi-rw
change stuff
rpi-ro 
```

### Further thinking
Other Apple devices can wake the WiFi Access Point on the iPhone. My conclusion is that this must be accomplished via Bluetooth,
but Apple doesn't seem to be open spec on this subject as all I've been able to find is that certain devices, when paired with Bluetooth,
connected to iCloud etc. etc. will offer this functionality.
Being able to reverse-engineer what Bluetooth communication is actually done in order to turn on the WiFi Access Point on the phone
would mitigate the drawback of this method, as the Paspberry Pi could be constantly looking for the phone via Bluetooth and
enabling Internet Sharing that way. 
