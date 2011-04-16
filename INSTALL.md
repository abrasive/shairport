Type `make` to build the packet decoder, `hairtunes`.

You need the following installed:

 * openssl
 * libao
 * Perl
 * Linux: avahi
 * Windows/Mac OS X: Bonjour

Perl modules (install from CPAN if needed e.g. `perl -MCPAN -e 'install X'`):

 * HTTP::Message
 * Crypt::OpenSSL::RSA
 * IO::Socket::INET6

## Debian/Ubuntu:

    apt-get install libssl-dev libcrypt-openssl-rsa-perl libao2 libao-dev libio-socket-inet6-perl libwww-perl avahi-utils
    make
    perl shairport.pl

## Mac OS X:

  * install XCode
  * install [Homebrew](https://github.com/mxcl/homebrew) or [Macports](http://www.macports.org/)
  * type:

        $ export ARCHFLAGS="-arch $HOSTTYPE"
        $ brew/port install pkg-config libao
        $ make
        $ perl -MCPAN -e 'install Crypt::OpenSSL::RSA'
        $ perl -MCPAN -e 'install IO::Socket::INET6'
        $ perl shairport.pl

  Users of OS X 10.5 and below will need to install a newer perl by running `port/brew install perl`

### How to run as a daemon on Mac 10.6

    $ cp hairtunes shairport.pl /usr/local/bin
    $ vi /usr/local/bin/shairport.pl, change the path of hairtunes from ./hairtunes to /usr/local/bin/hairtunes
    $ mkdir -p ~/Library/LaunchAgents
    $ cp org.mafipulation.shairport.plist ~/Library/LaunchAgents/
    $ launchctl load org.mafipulation.shairport.plist
    $ launchctl unload org.mafipulation.shairport.plist (to remove)

## Windows

 * Download and install Cygwin.
 * During setup, select
    * openssl-devel
    * openssl
    * libao
    * libao-devel
    * gcc4
    * make
    * pkg-config
    * perl
 * Install [Bonjour for Windows](http://support.apple.com/kb/DL999)
 * Launch the Cygwin Bash shell
 * type:

        $ make
        $ perl -MCPAN -e 'install Crypt::OpenSSL::RSA'
        $ perl -MCPAN -e 'install IO::Socket::INET6'
        $ perl shairport.pl
