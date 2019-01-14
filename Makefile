CC=gcc
PKGCONFIG=pkg-config
DESTDIR=

BINDIR=/usr/bin
SYSCONFDIR=/etc
UNITDIR=$(shell $(PKGCONFIG) systemd --variable=systemdsystemunitdir)

all: \
	intel-undervolt

intel_undervolt_headers = \
	config.h \
	modes.h \
	undervolt.h \
	util.h

intel_undervolt_sources = \
	config.c \
	main.c \
	modes.c \
	undervolt.c \
	util.c

intel_undervolt_objects = $(intel_undervolt_sources:.c=.o)

%.o: %.c $(intel_undervolt_headers)
	$(CC) $(CFLAGS) \
	-DSYSCONFDIR='"'$(SYSCONFDIR)'"' \
	-o $@ -c $<

intel-undervolt: $(intel_undervolt_objects)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

install: all
	install -Dm755 'intel-undervolt' "$(DESTDIR)$(BINDIR)/intel-undervolt"
	install -Dm644 'intel-undervolt.conf' "$(DESTDIR)$(SYSCONFDIR)/intel-undervolt.conf"
	install -Dm644 'intel-undervolt.service' "$(DESTDIR)$(UNITDIR)/intel-undervolt.service"
	install -Dm644 'intel-undervolt-loop.service' "$(DESTDIR)$(UNITDIR)/intel-undervolt-loop.service"

clean:
	rm -fv $(intel_undervolt_objects) intel-undervolt
