Troubleshooting
-----
The installation and setup of Shairport Sync is straightforward on recent Linux distributions. Issues can occasionally arise caused by problems elsewhere in the system, typically WiFi reception and/or the WiFi adapter settings, the network, the router, firewall settings.

In this brief document will be listed some problems and some solutions.

### UFW firewall blocking connections on Raspbian (Raspberry Pi)

**Problem**

You have installed Shairport-sync successfully, the deamon is running, you can see it from your remote terminal but you are unable to play a song.

**Before you change anything to your configuration**

- Type the following command:

  `sudo ufw disable`

- Try to launch a song from your remote device on the Shairport-sync one, if this works, proceed to the next step and follow the ones described below, in the solution section.

- Enable UFW through the following command:

  `sudo ufw enable`

**Solution**

You have to allow connections to your Pi from remote devices. To do so, after re-enabling UFW (see last step of the previous section), enter the following commands in shell:

```
sudo ufw allow from 192.168.1.1/16 to any port 3689 proto tcp
sudo ufw allow from 192.168.1.1/16 to any port 5353
sudo ufw allow from 192.168.1.1/16 to any port 5000:5005 proto tcp
sudo ufw allow from 192.168.1.1/16 to any port 6000:6005 proto udp
sudo ufw allow from 192.168.1.1/16 to any port 35000:65535 proto udp
```

You may have to change the IP adresses range depending on your own local network settings.

You can check UFW config by typing `sudo ufw status` in shell. Please make sure that UFW is active, especially if you have deactivated it previously for testing purpose.

Run your song from your remote device. Enjoy !
