ShairPort v0.05
==============
James Laird <jhl@mafipulation.org>
April 13, 2011

What it is
----------
This program emulates an AirPort Express for the purpose of streaming music from iTunes and compatible iPods. It implements a server for the Apple RAOP protocol.
ShairPort does not support AirPlay v2 (video and photo streaming).

It supports multiple simultaneous streams, if your audio output chain (as detected by libao) does so.

How to use it
-------------
`perl shairport.pl`. See INSTALL.md for further information.

The triangle-in-rectangle AirTunes (now AirPlay) logo will appear in the iTunes status bar of any machine on the network, or on iPod play controls screen. Choose your access point name to start streaming to the ShairPort instance.

Thanks
------
Big thanks to David Hammerton for releasing an ALAC decoder, which is reproduced here in full.
Thanks to everyone who has worked to reverse engineer the RAOP protocol - after finding the keys, everything else was pretty much trivial.
Thanks also to Apple for obfuscating the private key in the ROM image, using a scheme that made the deobfuscation code itself stand out like a flare.
Thanks to Ten Thousand Free Men and their Families for having a computer and stuff.
Thanks to wtbw.

Contributors
------------
* [David Hammerton](http://craz.net/)
* [Albert Zeyer](http://www.az2000.de)
* [Preston Marshall](mailto:preston@synergyeoc.com)
* [Mads Mætzke Tandrup](mailto:mads@tandrup.org)
* [Martin Spasov](mailto:mspasov@gmail.com)
* [Oleg Kertanov](mailto:okertanov@gmail.com)
* [Rafał Kwaśny](mailto:mag@entropy.be)
* [Rakuraku Jyo](mailto:jyo.rakuraku@gmail.com)
* [Vincent Gijsen](mailto:vtj.gijsen@gmail.com)
* [lars](mailto:lars@namsral.com)
* [Stuart Shelton](https://blog.stuart.shelton.me/)
* [Andrew Webster](mailto:andywebs@gmail.com)

Known Ports and Tools
---------------------
Java:
    [JAirPort](https://github.com/froks/JAirPort)
    [RPlay](https://github.com/bencall/RPlay).

Windows:
    [shairport4w](http://sf.net/projects/shairport4w)

OS X:
    [ShairportMenu](https://github.com/rcarlsen/ShairPortMenu), a GUI wrapper as a menu widget
    [MacShairport](https://github.com/joshaber/MacShairport)

Changelog
---------
* 0.01  April 5, 2011
    * initial release
* 0.02  April 11, 2011
    * bugfix: libao compatibility
* 0.03  April 11, 2011
    * bugfix: ipv6 didn't work - IO::Socket::INET6 is required too
* 0.04  April 12, 2011
    * cross-platform count_leading_zeros under GCC - will now compile cleanly on ARM and other platforms
* 0.05  April 13, 2011
    * error-handling cleanup in the Perl script including more meaningful error messages, based on common problems seen

