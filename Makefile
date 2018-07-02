CC=gcc
PKGCONFIG=pkg-config
DESTDIR=

BINDIR=/usr/bin
SYSCONFDIR=/etc
UNITDIR=$(shell $(PKGCONFIG) systemd --variable=systemdsystemunitdir)
MODLOADDIR=$(shell $(PKGCONFIG) systemd --variable=modulesloaddir)

all: \
	intel-undervolt

%.o: %.c
	$(CC) $(CFLAGS) \
	-DSYSCONFDIR='"'$(SYSCONFDIR)'"' \
	-o $@ -c $<

intel-undervolt: config.o main.o
	$(CC) $(LDFLAGS) -o $@ $^

install: all
	install -Dm755 'intel-undervolt' "$(DESTDIR)$(BINDIR)/intel-undervolt"
	install -Dm644 'intel-undervolt.conf' "$(DESTDIR)$(SYSCONFDIR)/intel-undervolt.conf"
	install -Dm644 'intel-undervolt.service' "$(DESTDIR)$(UNITDIR)/intel-undervolt.service"
	install -Dm644 'modules-load.conf' "$(DESTDIR)$(MODLOADDIR)/intel-undervolt.conf"
