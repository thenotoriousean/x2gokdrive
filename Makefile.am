# Copyright © 2013 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

SUBDIRS = man

AM_CPPFLAGS = 			\
	@KDRIVE_INCS@		\
	@KDRIVE_CFLAGS@		\
	@X2GOKDRIVE_INCS@	\
	@X2GOKDRIVE_CFLAGS@	\
	-DHAVE_DIX_CONFIG_H	\
	-I$(top_srcdir)		\
	-I$(top_srcdir)/exa

bin_PROGRAMS = x2gokdrive

x2gokdrive_SOURCES = \
	remote.c \
	remote.h \
	x2gokdrive.c \
	x2gokdrivecursor.c \
	x2gokdrive.h \
	x2gokdriveinit.c \
	x2gokdrivelog.h \
	x2gokdriveselection.c \
	x2gokdriveselection.h \
	$()

x2gokdrive_LDADD = 					\
	$(top_builddir)/exa/libexa.la			\
	@KDRIVE_LIBS@					\
	@X2GOKDRIVE_LIBS@

x2gokdrive_DEPENDENCIES = @KDRIVE_LOCAL_LIBS@

x2gokdrive_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG) -lz -ljpeg -lpng -lpthread

relink:
	$(AM_V_at)rm -f $(bin_PROGRAMS) && $(MAKE) $(bin_PROGRAMS)
