CFLAGS += -Wall -Wextra -Wno-unused-parameter -O3
__NAME__ = lariza
__NAME_UPPERCASE__ = `echo $(__NAME__) | sed 's/.*/\U&/'`
__NAME_CAPITALIZED__ = `echo $(__NAME__) | sed 's/^./\U&\E/'`

INSTALL = install
INSTALL_PROGRAM = $(INSTALL)
INSTALL_DATA = $(INSTALL) -m 644

prefix = /usr/local
exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin
datarootdir = $(prefix)/share
mandir = $(datarootdir)/man
man1dir = $(mandir)/man1


.PHONY: all clean install installdirs

all: $(__NAME__) we_adblock.so

$(__NAME__): browser.c
	$(CC) $(CFLAGS) $(LDFLAGS) \
		-D__NAME__=\"$(__NAME__)\" \
		-D__NAME_UPPERCASE__=\"$(__NAME_UPPERCASE__)\" \
		-D__NAME_CAPITALIZED__=\"$(__NAME_CAPITALIZED__)\" \
		-o $@ $< \
		`pkg-config --cflags --libs gtk+-3.0 glib-2.0 webkit2gtk-4.0`

we_adblock.so: we_adblock.c
	$(CC) $(CFLAGS) $(LDFLAGS) \
		-D__NAME__=\"$(__NAME__)\" \
		-D__NAME_UPPERCASE__=\"$(__NAME_UPPERCASE__)\" \
		-D__NAME_CAPITALIZED__=\"$(__NAME_CAPITALIZED__)\" \
		-shared -o $@ -fPIC $< \
		`pkg-config --cflags --libs glib-2.0 webkit2gtk-4.0`

install: all installdirs
	$(INSTALL_PROGRAM) $(__NAME__) $(DESTDIR)$(bindir)/$(__NAME__)
	$(INSTALL_DATA) man1/$(__NAME__).1 $(DESTDIR)$(man1dir)/$(__NAME__).1
	$(INSTALL_DATA) man1/$(__NAME__).usage.1 \
		$(DESTDIR)$(man1dir)/$(__NAME__).usage.1

installdirs:
	mkdir -p $(DESTDIR)$(bindir) $(DESTDIR)$(man1dir)

clean:
	rm -f $(__NAME__) we_adblock.so
