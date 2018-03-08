# Shairport Sync for Cars

At the moment, this is just a mess of notes, probably omitting some important steps, with some slightly incorrect information... More later...

If your car audio has an AUX IN, you can get AirPlay in your car using Shairport Sync. AirPlay and an iPhone gives you access to internet radio, YouTube, Apple Music, Spotify, etc. on the move. Your passengers can enjoy movies using the car audio for the sound track. You can even listen to Siri's traffic directions on your car audio. Finally, while it's no substitute for CarPlay, the audio quality is often much better than Bluetooth delivers.

In this example, we are assuming that a Raspberry Pi Zero W and a Pimoroni PHAT DAC are used.

The Basic Idea
=====
The basic idea is to use the Pi to create an isolated local WiFi network for the car and to run Shairport Sync on it to provide an AirPlay service. The audio goes via the DAC to the AUX IN of your car stereo.

The car network is isolated and local to your car, and since it isn't connected to the internet, you don't really need to secure it with a password. Likewise, you don't really have to use a password to connect to the AirPlay service.

When an iPhone or an iPad with cellular capability is connected to an isolated WiFi network like this, it can use the cellular network to connect to the internet.
This means it can connect to internet radio, YouTube, Apple Music, Spotify, etc. over the cellular network and play the audio through the car network to the AirPlay service provided by Shairport Sync.

Note that Android devices can not, so far, do this trick of using the two networks simultaneously.

Details
=====
* Install Stretch Lite
* Update & Upgrade
* Install `hostapd`, `isc-dhcp-server`, `htop`
* Configure the network interfaces to disable the wpa supplicant and to add a static definition for `wlan0`:
```
allow-hotplug wlan0
#iface wlan0 inet manual
#    wpa-conf /etc/wpa_supplicant/wpa_supplicant.conf

allow-hotplug wlan1
#iface wlan1 inet manual
#    wpa-conf /etc/wpa_supplicant/wpa_supplicant.conf

...

iface wlan0 inet static
address 10.0.10.1
netmask 255.255.255.0
#gateway <the-IP-address-of-your-LAN-gateway>
```
* Configure hostapd by editing `/etc/hostapd/hostapd.conf`

(From https://frillip.com/using-your-raspberry-pi-3-as-a-wifi-access-point-with-hostapd/)
```
# This is the name of the WiFi interface we configured above
interface=wlan0

# Use the nl80211 driver with the brcmfmac driver
driver=nl80211

# This is the name of the network
ssid="Shairport Sync on Tour"

# Use the 2.4GHz band
hw_mode=g

# Use channel 6
channel=3

# Enable 802.11n
ieee80211n=1

# Enable WMM
wmm_enabled=1

# Enable 40MHz channels with 20ns guard interval
#ht_capab=[HT40][SHORT-GI-20][DSSS_CCK-40]

# Accept all MAC addresses
macaddr_acl=0

# Use WPA authentication
auth_algs=1

# Require clients to know the network name
ignore_broadcast_ssid=0

# Use WPA2
wpa=2

# Use a pre-shared key
wpa_key_mgmt=WPA-PSK

# The network passphrase
wpa_passphrase=roadgoing

# Use AES, instead of TKIP
rsn_pairwise=CCMP
```

* Next, in `/etc/default/hostapd`, find the line `#DAEMON_CONF=""` and replace it with `DAEMON_CONF="/etc/hostapd/hostapd.conf"`

* Configure dhcp server by editing `/etc/dhcp/dhcpd.conf` to look like this:

```
subnet 10.0.10.0 netmask 255.255.255.0 {
     range 10.0.10.5 10.0.10.150;
     #option routers <the-IP-address-of-your-gateway-or-router>;
     #option broadcast-address <the-broadcast-IP-address-for-your-network>;
}
```

* Edit `/etc/default/isc-dhcp-server` and add:
```
INTERFACES="wlan0"
```

Build Shairport Sync in the normal way, and do the `# make install` step, but do not enable it to autostart --
it will be started by a command in `/etc/rc.local` after the other services it needs have been started up.

Add the following into `/etc/rc.local`:

```
/usr/sbin/hostapd -B -P /run/hostapd.pid /etc/hostapd/hostapd.conf
/usr/sbin/dhcpd -q -cf /etc/dhcp/dhcpd.conf -pf /var/run/dhcpd.pid wlan0
/bin/systemctl start shairport-sync
```
