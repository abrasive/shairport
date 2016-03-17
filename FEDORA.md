Fedora Installation Guide
-----

Install the toolchain and pre-requisites, if necessary:
```
% sudo yum install make automake gcc gcc-c++ kernel-devel
% sudo yum install alsa-lib-devel autoconf automake avahi-devel libconfig-devel libdaemon-devel openssl-devel popt-devel soxr-devel
```
Download the tarball from the "releases" tab on github or use `wget` and then use `rpmbuild`. This example is for version 2.6:
```
% wget -O shairport-sync-2.6.tar.gz https://github.com/mikebrady/shairport-sync/archive/2.6.tar.gz
% rpmbuild -ta shairport-sync-2.6.tar.gz
```
The `-ta` means "build all from this tarball".

The RPM will be built in a directory and will have a pathname like, for example, `~/rpmbuild/RPMS/i686/shairport-sync-2.6-1.fc22.i686.rpm` You should then install it with (for this example):
```
%sudo rpm -i ~/rpmbuild/RPMS/i686/shairport-sync-2.6-1.fc22.i686.rpm
```
You may have to manually create the directory `/var/shairport-sync` for the installation to succeed. Having edited the configuration file `/etc/shairport-sync.conf` as appropriate (see "Configuring Shairport Sync" below), enable and start the service with:
```
%sudo systemctl enable shairport-sync.service
%sudo systemctl start shairport-sync.service
```
