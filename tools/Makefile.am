bin_PROGRAMS = foxcomet-mkpasswd foxcomet-mkfingerprint
AM_CFLAGS=$(WARNFLAGS)
AM_CPPFLAGS = $(DEFAULT_INCLUDES) -I../librb/include -I.

foxcomet_mkpasswd_SOURCES = mkpasswd.c
foxcomet_mkpasswd_LDADD = ../librb/src/librb.la

foxcomet_mkfingerprint_SOURCES = mkfingerprint.c
foxcomet_mkfingerprint_LDADD = ../librb/src/librb.la
