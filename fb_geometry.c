#include "fb_geometry.h"

#include <string.h>

int fb_compute_output_rect(unsigned int virtual_width, unsigned int virtual_height,
    unsigned int system_width, unsigned int system_height, int stretch,
    struct fb_output_rect *rect)
{
    uint64_t width;
    uint64_t height;
    if (rect == NULL || virtual_width == 0 || virtual_height == 0 || system_width == 0 ||
        system_height == 0 || virtual_width > 8192u || virtual_height > 8192u ||
        system_width > 8192u || system_height > 8192u || (stretch != 0 && stretch != 1)) {
        return 0;
    }
    memset(rect, 0, sizeof(*rect));
    if (stretch != 0) {
        rect->width = system_width;
        rect->height = system_height;
    } else {
        width = system_width;
        height = width * virtual_height / virtual_width;
        if (height > system_height) {
            height = system_height;
            width = height * virtual_width / virtual_height;
        }
        rect->width = (unsigned int)width;
        rect->height = (unsigned int)height;
    }
    if (rect->width == 0 || rect->height == 0) {
        return 0;
    }
    rect->left = (system_width - rect->width) / 2u;
    rect->top = (system_height - rect->height) / 2u;
    return 1;
}
