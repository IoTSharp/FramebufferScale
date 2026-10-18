CC ?= gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
LDFLAGS ?=
LDLIBS ?=

all: libframebufferscale.so framebuffer-scale fb_geometry_test fb_scale_test

libframebufferscale.so: fb_virtual.c fb_config.c fb_geometry.c fb_config.h fb_geometry.h
	$(CC) $(CFLAGS) -fPIC -shared -o $@ fb_virtual.c fb_config.c fb_geometry.c -pthread $(LDFLAGS) $(LDLIBS)

framebuffer-scale: framebuffer-scale.c fb_config.c fb_config.h
	$(CC) $(CFLAGS) -o $@ framebuffer-scale.c fb_config.c $(LDFLAGS) $(LDLIBS)

fb_geometry_test: fb_geometry_test.c fb_geometry.c fb_geometry.h
	$(CC) $(CFLAGS) -o $@ fb_geometry_test.c fb_geometry.c $(LDFLAGS) $(LDLIBS)

fb_scale_test: fb_scale_test.c fb_geometry.c fb_geometry.h
	$(CC) $(CFLAGS) -o $@ fb_scale_test.c fb_geometry.c $(LDFLAGS) $(LDLIBS)

check: fb_geometry_test
	./fb_geometry_test

clean:
	rm -f libframebufferscale.so framebuffer-scale fb_geometry_test fb_scale_test

.PHONY: all check clean
