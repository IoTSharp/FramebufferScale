#define _GNU_SOURCE
#include "fb_config.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *name)
{
    fprintf(stderr,
        "Usage: %s [options] -- program [args...]\n"
        "  --config PATH              Configuration file (default: /etc/framebufferscale.conf)\n"
        "  --virtual-size WxH        Required simulated framebuffer size\n"
        "  --system-size auto|WxH    Physical system size or automatic detection\n"
        "  --mode fit|stretch        Output mode (default: fit)\n"
        "  --fps N                   Paint rate, 1..60 (default: 15)\n"
        "  --seconds N               Trial lifetime; 0 means process lifetime\n",
        name);
}

static int parse_bounded_uint(const char *text, unsigned int maximum, unsigned int *value)
{
    char *end = NULL;
    unsigned long parsed;
    if (text == NULL || value == NULL || *text == '\0') {
        return -1;
    }
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed > maximum) {
        return -1;
    }
    *value = (unsigned int)parsed;
    return 0;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

static int apply_cli(struct fb_config *config, int argc, char **argv, int *command_index, const char **config_path)
{
    int index;
    if (config == NULL || command_index == NULL || config_path == NULL) {
        return -1;
    }
    for (index = 1; index < argc; ++index) {
        const char *option;
        const char *value;
        if (strcmp(argv[index], "--") == 0) {
            *command_index = index + 1;
            return 0;
        }
        if (strcmp(argv[index], "--help") == 0) {
            usage(argv[0]);
            exit(0);
        }
        option = argv[index];
        if (index + 1 >= argc) {
            return -1;
        }
        value = argv[++index];
        if (strcmp(option, "--config") == 0) {
            *config_path = value;
        } else if (strcmp(option, "--virtual-size") == 0) {
            if (fb_parse_size(value, &config->virtual_width, &config->virtual_height) != 0) return -1;
        } else if (strcmp(option, "--system-size") == 0) {
            if (strcmp(value, "auto") == 0) {
                config->system_auto = 1;
                config->system_width = config->system_height = 0;
            } else if (fb_parse_size(value, &config->system_width, &config->system_height) != 0) {
                return -1;
            } else {
                config->system_auto = 0;
            }
        } else if (strcmp(option, "--mode") == 0) {
            if (strcmp(value, "fit") == 0) config->stretch = 0;
            else if (strcmp(value, "stretch") == 0) config->stretch = 1;
            else return -1;
        } else if (strcmp(option, "--fps") == 0) {
            if (parse_bounded_uint(value, 60u, &config->fps) != 0 || config->fps == 0) return -1;
        } else if (strcmp(option, "--seconds") == 0) {
            if (parse_bounded_uint(value, 86400u, &config->seconds) != 0) return -1;
        } else {
            return -1;
        }
    }
    return -1;
}

int main(int argc, char **argv)
{
    struct fb_config config;
    const char *config_path = FB_CONFIG_DEFAULT_PATH;
    const char *system_size;
    char system_text[32];
    char self[PATH_MAX];
    char library[PATH_MAX];
    char *command[128];
    int command_index = 0;
    int command_count;
    int index;
    ssize_t length;
    fb_config_defaults(&config);
    for (index = 1; index < argc && strcmp(argv[index], "--") != 0; ++index) {
        if (strcmp(argv[index], "--config") == 0) {
            if (index + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            config_path = argv[++index];
        }
    }
    if (fb_config_load_file(&config, config_path) != 0 && errno != ENOENT) {
        perror("framebuffer-scale: config");
        return 2;
    }
    if (apply_cli(&config, argc, argv, &command_index, &config_path) != 0 || command_index >= argc) {
        usage(argv[0]);
        return 2;
    }
    command_count = argc - command_index;
    if (command_count >= (int)(sizeof(command) / sizeof(command[0]))) {
        fprintf(stderr, "framebuffer-scale: too many command arguments\n");
        return 2;
    }
    for (index = 0; index < command_count; ++index) {
        command[index] = argv[command_index + index];
    }
    command[command_count] = NULL;
    if (fb_config_validate(&config, 0) != 0) {
        fprintf(stderr, "framebuffer-scale: virtual-size is required and configuration is invalid\n");
        return 2;
    }
    length = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (length <= 0 || length >= (ssize_t)sizeof(self)) {
        perror("framebuffer-scale: executable path");
        return 2;
    }
    self[length] = '\0';
    {
        char *separator = strrchr(self, '/');
        if (separator == NULL) {
            fprintf(stderr, "framebuffer-scale: cannot locate library\n");
            return 2;
        }
        *separator = '\0';
        if (snprintf(library, sizeof(library), "%s/libframebufferscale.so", self) >= (int)sizeof(library)) {
            return 2;
        }
    }
    if (config.system_auto) {
        system_size = "auto";
    } else {
        if (snprintf(system_text, sizeof(system_text), "%ux%u", config.system_width, config.system_height) >= (int)sizeof(system_text)) {
            return 2;
        }
        system_size = system_text;
    }
    if (setenv("LD_PRELOAD", library, 1) != 0 || setenv("SR_ATS_FB_CONFIG", config_path, 1) != 0 ||
        setenv("SR_ATS_FB_PROGRAM", base_name(command[0]), 1) != 0 ||
        setenv("SR_ATS_FB_SYSTEM_SIZE", system_size, 1) != 0 ||
        setenv("SR_ATS_FB_MODE", config.stretch ? "stretch" : "fit", 1) != 0) {
        perror("framebuffer-scale: environment");
        return 2;
    }
    {
        char value[32];
        if (snprintf(value, sizeof(value), "%u", config.virtual_width) >= (int)sizeof(value) ||
            setenv("SR_ATS_FB_VIRTUAL_WIDTH", value, 1) != 0 ||
            snprintf(value, sizeof(value), "%u", config.virtual_height) >= (int)sizeof(value) ||
            setenv("SR_ATS_FB_VIRTUAL_HEIGHT", value, 1) != 0 ||
            snprintf(value, sizeof(value), "%u", config.fps) >= (int)sizeof(value) ||
            setenv("SR_ATS_FB_FPS", value, 1) != 0 ||
            snprintf(value, sizeof(value), "%u", config.seconds) >= (int)sizeof(value) ||
            setenv("SR_ATS_FB_SECONDS", value, 1) != 0) {
            perror("framebuffer-scale: environment");
            return 2;
        }
    }
    if (config.system_auto) {
        unsetenv("SR_ATS_FB_SYSTEM_WIDTH");
        unsetenv("SR_ATS_FB_SYSTEM_HEIGHT");
    } else {
        unsetenv("SR_ATS_FB_SYSTEM_WIDTH");
        unsetenv("SR_ATS_FB_SYSTEM_HEIGHT");
    }
    execvp(command[0], command);
    perror("framebuffer-scale: exec");
    return 127;
}
