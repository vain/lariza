CFLAGS += -Wall -Wextra -Wno-unused-parameter -O3
__NAME__ = lariza
__NAME_UPPERCASE__ = `echo $(__NAME__) | sed 's/.*/\U&/'`
__NAME_CAPITALIZED__ = `echo $(__NAME__) | sed 's/^./\U&\E/'`

$(__NAME__): browser.c
	$(CC) $(CFLAGS) $(LDFLAGS) \
		-D__NAME__=\"$(__NAME__)\" \
		-D__NAME_UPPERCASE__=\"$(__NAME_UPPERCASE__)\" \
		-D__NAME_CAPITALIZED__=\"$(__NAME_CAPITALIZED__)\" \
		-o $@ $< \
		`pkg-config --cflags --libs gtk+-2.0 glib-2.0 webkit-1.0`

clean:
	rm -f $(__NAME__)
