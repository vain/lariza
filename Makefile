CFLAGS += -Wall -Wextra -O3

sn: sn.c
	$(CC) $(CFLAGS) $(LDFLAGS) \
		-o $@ $< \
		`pkg-config --cflags --libs gtk+-3.0 webkitgtk-3.0`

clean:
	rm -f sn
