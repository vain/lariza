CFLAGS += -Wall -Wextra -O3

zea: zea.c
	$(CC) $(CFLAGS) $(LDFLAGS) \
		-o $@ $< \
		`pkg-config --cflags --libs gtk+-2.0 webkit-1.0`

clean:
	rm -f zea
