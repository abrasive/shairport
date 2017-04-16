
The Raspbian image at the time of writing is the `May 2016` version, with the release date of `2016-05-10`. It does not include `libsoxr`, but it is available as a package via `apt-get`.

Alternatively, `libsoxr` is very easy to compile. Here are very brief instructions to download, compile and install it:

* Install `cmake`. This is used in the building of libsoxr. On Linuxes such as Debian/Ubuntu/Raspbian:
```
# apt-get install cmake
```
On FreeBSD:
```
# pkg install cmake
```

* Download the `libsoxr source`:
```
$ git clone git://git.code.sf.net/p/soxr/code libsoxr
```
* `cd` into the `libsoxr` directory and start the build process:
```
$ cd libsoxr
$ ./go
```
Be patient! This takes a long time on a Raspberry Pi -- it looks like it gets stuck around 40% or 50%, but it will finish if you let it.

Having compiled `libsoxr`, it must now must be installed:
```
$ cd Release
# make install
```
Finally, for Shairport Sync to be able to locate `libsoxr` during compilation, you need to tell `ld` about it.  Be careful here if you are on FreeBSD -- the following instructions for Linux would mess up your FreeBSD system.

On Linuxes such as Debian/Ubuntu/Raspbian: 
```
# ldconfig -v
```
On FreeBSD you must add the location of the `soxr.pc` file to the `PKG_CONFIG_PATH`, if it exists, and define it otherwise. Here is what you do if it doesn't already exist:
```
$ PKG_CONFIG_PATH="/usr/local/lib/pkgconfig"
$ export PKG_CONFIG_PATH
```
That's it. Now you can select the `--with-soxr` option when you're building Shairport Sync.
