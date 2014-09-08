#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.50])
AC_INIT([shairport-sync], [2.1], [mikebrady@eircom.net])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([shairport.c])
AC_CONFIG_HEADERS([config.h])

# Derived from the Avahi configure.ac file
#
# Specifying the OS type, defaulting to linux.
#
AC_ARG_WITH(os_type, AS_HELP_STRING([--with-os-type=OSType],[Specify the distribution to target: One of linux darwin]))
if test "z$with_os_type" = "z"; then
  with_os_type="linux"
fi
with_os_type=`echo ${with_os_type} | tr '[[:upper:]]' '[[:lower:]]' `



# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Checks for libraries.
if test "x${with_os_type}" = xlinux; then
 AC_CHECK_LIB([rt],[clock_gettime], , AC_MSG_ERROR(librt needed))
fi

AC_CHECK_LIB([daemon],[daemon_log], , AC_MSG_ERROR(libdaemon needed))
AC_CHECK_LIB([pthread],[pthread_create], , AC_MSG_ERROR(pthread library needed))
AC_CHECK_LIB([m],[exp], , AC_MSG_ERROR(maths library needed))
AC_CHECK_LIB([popt],[poptGetContext], , AC_MSG_ERROR(libpopt needed))

# Look for polarssl flag
AC_ARG_WITH(polarssl, [  --with-polarssl = choose polarssl for encryption services (choose this or --with-openssl)], [
  AC_MSG_RESULT(>>Including polarssl)
  AC_CHECK_LIB([polarssl],[ssl_init], , AC_MSG_ERROR(PolarSSL selected but the library cannot be found!))], )

# Look for openssl flag
AC_ARG_WITH(openssl, [  --with-openssl = choose openssl for encryption services (choose this or --with-polarssl)], [
  AC_MSG_RESULT(>>Including openssl and libcrypto)
  AC_CHECK_LIB([crypto], [MD5_Init], , AC_MSG_ERROR(libcrypto selected, but the library cannot be found!))
  AC_CHECK_LIB([ssl],[SSL_library_init], , AC_MSG_ERROR(openssl selected but the library cannot be found!))], )

# Look for soxr flag
AC_ARG_WITH(soxr, [  --with-soxr = choose libsoxr for high-quality interpolation], [
  AC_MSG_RESULT(>>Including support for soxr-based interpolation)
  AC_CHECK_LIB([soxr],[soxr_create], , AC_MSG_ERROR(soxr support requested but libsoxr not found!))], )

# What follows is a bit messy, because if the relevant library is requested, a compiler flag is defined, a file is included in the compilation
# and the relevant link files are added.

# Look for avahi flag
AC_ARG_WITH(avahi, [  --with-avahi = choose Avahi-based mDNS support], [
  AC_MSG_RESULT(>>Including Avahi mDNS support)
  HAS_AVAHI=1
  AC_DEFINE([CONFIG_AVAHI], 1, [Needed by the compiler.])
  AC_CHECK_LIB([avahi-client], [avahi_client_new], , AC_MSG_ERROR(Avahi support requires the avahi-client library!))
  AC_CHECK_LIB([avahi-common],[avahi_strerror], , AC_MSG_ERROR(Avahi support requires the avahi-common library!))], )
AM_CONDITIONAL([USE_AVAHI], [test "x$HAS_AVAHI" = "x1"])

# Look for tinysvcmdns flag
AC_ARG_WITH(tinysvcmdns, [  --with-tinysvcmdns = choose tinysvcmdns-based mDNS support], [
  AC_MSG_RESULT(>>Including tinysvcmdns mDNS support)
  HAS_TINYSVCMDNS=1
  AC_DEFINE([CONFIG_TINYSVCMDNS], 1, [Needed by the compiler.])], )
AM_CONDITIONAL([USE_TINYSVCMDNS], [test "x$HAS_TINYSVCMDNS" = "x1"])

# Look for ALSA flag
AC_ARG_WITH(alsa, [  --with-alsa = choose native ALSA API support (GNU/Linux only)],
  [AC_MSG_RESULT(>>Including an ALSA back end)
  HAS_ALSA=1
  AC_DEFINE([CONFIG_ALSA], 1, [Needed by the compiler.])
  AC_CHECK_LIB([asound], [snd_pcm_open], , AC_MSG_ERROR(ALSA support requires the asound library!))], )
AM_CONDITIONAL([USE_ALSA], [test "x$HAS_ALSA" = "x1"])

# Look for SNDIO flag
AC_ARG_WITH(sndio, [  --with-sndio = choose native SNDIO API support (FreeBSD) -- probably broken], [
  AC_MSG_RESULT(>>Including a SNDIO back end --- N.B. this is probably broken!)
  HAS_SNDIO=1
  AC_DEFINE([CONFIG_SNDIO], 1, [Needed by the compiler.])
  AC_CHECK_LIB([sndio], [sio_open], , AC_MSG_ERROR(SNDIO support requires the sndio library!))], )
AM_CONDITIONAL([USE_SNDIO], [test "x$HAS_SNDIO" = "x1"])
  
# Look for AO flag
AC_ARG_WITH(ao, [  --with-ao = choose native AO (Audio Output?) API support -- probably broken], [
  AC_MSG_RESULT(>>Including an AO back end --- N.B. this is probably broken!)
  HAS_AO=1
  AC_DEFINE([CONFIG_AO], 1, [Needed by the compiler.])
  AC_CHECK_LIB([ao], [ao_initialize], , AC_MSG_ERROR(AO support requires the ao library!))], )
AM_CONDITIONAL([USE_AO], [test "x$HAS_AO" = "x1"])

# Look for pulseaudio flag
AC_ARG_WITH(pulseaudio, [  --with-pulseaudio = choose native PulseAudio API support -- probably broken], [
  AC_MSG_RESULT(>>Including a PulseAudio back end --- N.B. this is probably broken!)
  HAS_PULSE=1
  AC_DEFINE([CONFIG_PULSE], 1, [Needed by the compiler.])
  AC_CHECK_LIB([pulse-simple], [pa_simple_new], , AC_MSG_ERROR(PulseAudio support requires the pulse-simple library!))], )
AM_CONDITIONAL([USE_PULSE], [test "x$HAS_PULSE" = "x1"])

# Look for dns_sd flag
AC_ARG_WITH(dns_sd, [  --with-dns_sd = choose dns_sd mDNS support], [
  AC_MSG_RESULT(>>Including dns_sd for mDNS support)
  HAS_DNS_SD=1
  AC_DEFINE([CONFIG_HAVE_DNS_SD_H], 1, [Needed by the compiler.])
  AC_CHECK_LIB([dns_sd], [DNSServiceRefDeallocate], , AC_MSG_ERROR(dns_sd support requires the dns_sd library!))], )
AM_CONDITIONAL([USE_DNS_SD], [test "x$HAS_DNS_SD" = "x1"])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([getopt_long.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
