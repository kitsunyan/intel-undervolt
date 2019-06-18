CC=gcc
CFLAGS=
EXTRA_CFLAGS=-Wall -Wextra
PKGCONFIG=pkg-config
DESTDIR=

BINDIR=/usr/bin
SYSCONFDIR=/etc
UNITDIR=$(shell $(PKGCONFIG) systemd --variable=systemdsystemunitdir)

all: \
	intel-undervolt

intel_undervolt_headers = \
	config.h \
	measure.h \
	modes.h \
	power.h \
	scaling.h \
	stat.h \
	undervolt.h \
	util.h

intel_undervolt_sources = \
	config.c \
	measure.c \
	main.c \
	modes.c \
	power.c \
	scaling.c \
	stat.c \
	undervolt.c \
	util.c

intel_undervolt_objects = $(intel_undervolt_sources:.c=.o)

%.o: %.c $(intel_undervolt_headers)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) \
	-DSYSCONFDIR='"'$(SYSCONFDIR)'"' \
	-o $@ -c $<

intel-undervolt: $(intel_undervolt_objects)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

install: all
	install -Dm755 'intel-undervolt' "$(DESTDIR)$(BINDIR)/intel-undervolt"
	install -Dm644 'intel-undervolt.conf' "$(DESTDIR)$(SYSCONFDIR)/intel-undervolt.conf"
	install -Dm644 'intel-undervolt.service' "$(DESTDIR)$(UNITDIR)/intel-undervolt.service"
	install -Dm644 'intel-undervolt.timer' "$(DESTDIR)$(UNITDIR)/intel-undervolt.timer"

clean:
	rm -fv $(intel_undervolt_objects) intel-undervolt
