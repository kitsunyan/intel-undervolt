CC=gcc
PKGCONFIG=pkg-config
DESTDIR=

BINDIR=/usr/bin
PKGLIBDIR=/usr/lib/intel-undervolt
SYSCONFDIR=/etc
UNITDIR=$(shell $(PKGCONFIG) systemd --variable=systemdsystemunitdir)
MODLOADDIR=$(shell $(PKGCONFIG) systemd --variable=modulesloaddir)

all: \
	ioutils \
	intel-undervolt

ioutils: ioutils.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

intel-undervolt: intel-undervolt.in
	cat $< | sed \
	-e "s,@PKGLIBDIR@,$(PKGLIBDIR),g" \
	-e "s,@SYSCONFDIR@,$(SYSCONFDIR),g" \
	> $@

install: all
	install -Dm755 'ioutils' "$(DESTDIR)$(PKGLIBDIR)/ioutils"
	install -Dm755 'intel-undervolt' "$(DESTDIR)$(BINDIR)/intel-undervolt"
	install -Dm644 'intel-undervolt.conf' "$(DESTDIR)$(SYSCONFDIR)/intel-undervolt.conf"
	install -Dm644 'intel-undervolt.service' "$(DESTDIR)$(UNITDIR)/intel-undervolt.service"
	install -Dm644 'modules-load.conf' "$(DESTDIR)$(MODLOADDIR)/intel-undervolt.conf"
