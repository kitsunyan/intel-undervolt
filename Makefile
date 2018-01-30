PKGCONFIG=pkg-config
DESTDIR=

BINDIR=/usr/bin
SYSCONFDIR=/etc
UNITDIR=$(shell $(PKGCONFIG) systemd --variable=systemdsystemunitdir)
MODLOADDIR=$(shell $(PKGCONFIG) systemd --variable=modulesloaddir)

all: \
	intel-undervolt

intel-undervolt:
	cat 'intel-undervolt.in' | sed -e \
	"s,@SYSCONFDIR@,$(SYSCONFDIR),g" \
	> $@

install: all
	install -Dm755 'intel-undervolt' "$(DESTDIR)$(BINDIR)/intel-undervolt"
	install -Dm644 'intel-undervolt.conf' "$(DESTDIR)$(SYSCONFDIR)/intel-undervolt.conf"
	install -Dm644 'intel-undervolt.service' "$(DESTDIR)$(UNITDIR)/intel-undervolt.service"
	install -Dm644 'modules-load.conf' "$(DESTDIR)$(MODLOADDIR)/intel-undervolt.conf"
