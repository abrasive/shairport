Type `make` to build the packet decoder, `hairtunes`.

You need the following installed:

 * openssl
 * libao (if you use homebrew, use brew install libao)
 * avahi (avahi-daemon running and avahi-publish-service on path, no need on Mac OSX)
 * Perl

Debian/Ubuntu users need:

    libssl-dev libcrypt-openssl-rsa-perl libao2 libao-dev libio-socket-inet6-perl libwww-perl avahi-utils

Perl modules (install from CPAN if needed e.g. `perl -MCPAN -e 'install X'`):

 * HTTP::Message
 * Crypt::OpenSSL::RSA
 * IO::Socket::INET6

MacOSX:

  * install XCode
  * install [Homebrew](https://github.com/mxcl/homebrew)
  * type:

        $ export ARCHFLAGS="-arch x86_64"
        $ brew install pkg-config libao
        $ make
        $ perl -MCPAN -e 'install Crypt::OpenSSL::RSA'
        $ perl -MCPAN -e 'install IO::Socket::INET6'
        $ perl shairport.pl

  OSX 10.5 only bundles Perl 5.8, which won't work with shairport.
  After getting a update [here](http://www.perl.org/get.html), it worked.

How to run as a daemon on Mac 10.6
------

    $ cp hairtunes shairport.pl /usr/local/bin
    $ vi /usr/local/bin/shairport.pl, change the path of hairtunes from ./hairtunes to /usr/local/bin/hairtunes
    $ mkdir -p ~/Library/LaunchAgents
    $ cp org.mafipulation.shairport.plist ~/Library/LaunchAgents/
    $ launchctl load org.mafipulation.shairport.plist
    $ launchctl unload org.mafipulation.shairport.plist (to remove)
