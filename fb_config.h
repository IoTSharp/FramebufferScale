#ifndef FRAMEBUFFER_SCALE_CONFIG_H
#define FRAMEBUFFER_SCALE_CONFIG_H

#include <stddef.h>

#define FB_CONFIG_DEFAULT_PATH "/etc/framebufferscale.conf"
#define FB_CONFIG_PROGRAM_MAX 256u

struct fb_config {
    unsigned int virtual_width;
    unsigned int virtual_height;
    int system_auto;
    unsigned int system_width;
    unsigned int system_height;
    int stretch;
    unsigned int fps;
    unsigned int seconds;
    char program[FB_CONFIG_PROGRAM_MAX];
};

void fb_config_defaults(struct fb_config *config);
int fb_config_load_file(struct fb_config *config, const char *path);
int fb_config_load_environment(struct fb_config *config);
int fb_config_validate(const struct fb_config *config, int require_program);
int fb_parse_size(const char *text, unsigned int *width, unsigned int *height);

#endif
