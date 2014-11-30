CFLAGS += -Wall -Wextra -Wno-unused-parameter -O3
__NAME__ = lariza
__NAME_UPPERCASE__ = `echo $(__NAME__) | sed 's/.*/\U&/'`
__NAME_CAPITALIZED__ = `echo $(__NAME__) | sed 's/^./\U&\E/'`

.PHONY: all
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

clean:
	rm -f $(__NAME__) we_adblock.so
