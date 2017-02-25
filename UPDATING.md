
### Updating Shairport Sync
This guide is for updating an installation of Shairport Sync you built yourself. If you installed Shairport Sync from a package, these instructions don't apply. 

To do an update, you basically have to go through the whole process of building Shairport Sync again,
but a few steps are shorter because you've done them before; you won't have to reinstall the build tools or libraries needed, and you won't have to define the user and group or reconfigure the settings in the configuration file.

But before you begin, you should update and upgrade any packages.

Here is the sequence for Raspbian Jessie, which is based on Debian Jessie. The same commands work for Ubuntu, and maybe more. Here, a non-`root` user with `sudo` privileges is assumed.

```
$ sudo apt-get update
$ sudo apt-get upgrade
```
Next, stop playing music to your device.

Now, to update and install Shairport Sync, if you still have the directory in which you previously built Shairport Sync, it will contain the repository you originally downloaded. Navigate your way to it and 'pull' the changes from GitHub:

```
$ git pull
```
Otherwise – say if you deleted the repository – just pull Shairport Sync from GitHub again and move into the new directory:
```
$ git clone https://github.com/mikebrady/shairport-sync.git
$ cd shairport-sync
```
Now, while in the `shairport-sync` directory, perform the following commands (note that there is a choice you must make in there):
```
$ autoreconf -fi
```
Please review the release notes to see if any configuration settings have been changed. For instance, in the transitions from version 2 to version 3, the `--with-ssl=polarssl` has been deprecated in favour of `--with-ssl=mbedtls`.
```
#The following is the standard configuration for a Linux that uses the systemd initialisation system:
$ ./configure --with-alsa --with-avahi --with-ssl=openssl --with-metadata --with-soxr --with-systemd --sysconfdir=/etc
#OR
#The following is the standard configuration for a Linux that uses the older System V initialisation system:
$ ./configure --with-alsa --with-avahi --with-ssl=openssl --with-metadata --with-soxr --with-systemv --sysconfdir=/etc

$ make
$ sudo make install
```
At this point you have downloaded, compiled and installed the updated Shairport Sync. However, the older version is still running. So, you need to do a little more: 

If you are on a `systemd`-based system such as Raspbian Jessie or recent versions of Ubuntu and Debian, execute the following commands:
```
$ sudo systemctl daemon-reload
$ sudo systemctl restart shairport-sync
```
Otherwise execute the following command:
```
$ sudo service shairport-sync restart
```

That's it. Your Shairport Sync should be upgraded now. 
