EXTRA_DIST = ptp-pack.c

lib_LTLIBRARIES = libptp2.la

libptp2_la_SOURCES = ptp.c ptp.h properties.c
libptp2_la_LDFLAGS = -version-info @LIBPTP2_VERSION_INFO@

libptp2includedir = $(includedir)/libptp2
libptp2include_HEADERS = ptp.h libptp-endian.h libptp-stdint.h

DISTCLEANFILES = libptp-stdint.h libptp-endian.h

# changed name from ptpcam to make it clear
# we are using a modified program
if PTPCAM
bin_PROGRAMS = theta
if LINUX_OS
theta_SOURCES = ptpcam.c ptpcam.h myusb.c
else 
theta_SOURCES = ptpcam.c ptpcam.h
endif
theta_LDADD = -lptp2 @PTPCAM_LDFLAGS@
theta_DEPENDENCIES = libptp2.la
theta_CFLAGS = @PTPCAM_CFLAGS@
endif
