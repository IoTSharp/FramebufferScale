#ifndef FRAMEBUFFER_SCALE_GEOMETRY_H
#define FRAMEBUFFER_SCALE_GEOMETRY_H

#include <stdint.h>

struct fb_output_rect {
    unsigned int width;
    unsigned int height;
    unsigned int left;
    unsigned int top;
};

int fb_compute_output_rect(unsigned int virtual_width, unsigned int virtual_height,
    unsigned int system_width, unsigned int system_height, int stretch,
    struct fb_output_rect *rect);

#endif
