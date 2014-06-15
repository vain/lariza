CFLAGS += -Wall -Wextra -O3
__NAME__ = zea

$(__NAME__): browser.c
	$(CC) $(CFLAGS) $(LDFLAGS) \
		-D__NAME__=\"$(__NAME__)\" \
		-o $@ $< \
		`pkg-config --cflags --libs gtk+-2.0 glib-2.0 webkit-1.0`

clean:
	rm -f $(__NAME__)
