
PREFIX?=/usr/local

VERSION := $(shell echo `git tag 2>/dev/null | tail -n 1 | grep . || echo v0.0.0`)

TARGETS=blockfs blockfs-nocopy
DOCFILES=blockfs-nocopy.1.gz
CFLAGS=-Wall
LDFLAGS := $(shell pkg-config fuse3 --cflags --libs)


all: $(TARGETS) $(DOCFILES)

install:
	install $(TARGETS) ${DESTDIR}${PREFIX}/bin/
#[c]	install -d ${DESTDIR}${PREFIX}/share/doc/blockfs/examples/
#[c]	install writeoverlay.sh ${DESTDIR}${PREFIX}/share/doc/blockfs/examples/
	install -m 644 $(DOCFILES) ${DESTDIR}${PREFIX}/share/man/man1/

.SECONDEXPANSION:

$(TARGETS): $$@.c
	$(CC) $@.c $(CFLAGS) $(LDFLAGS) -o $@

$(DOCFILES): README.md
	grep -v -e '^- \[' -e '^-------------------------------' README.md | \
	  sed -e 's%Version 0.0.0%Version '$(VERSION)'%;s%^# %%' | \
	  pandoc --standalone -f markdown -t man | \
	  gzip -9c > $@

clean:
	-rm $(TARGETS) $(DOCFILES)

.PHONY: all install clean

