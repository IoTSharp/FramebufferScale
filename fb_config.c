#define _GNU_SOURCE
#include "fb_config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static char *trim(char *text)
{
    char *end;
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return text;
}

static int parse_uint(const char *text, unsigned int *value)
{
    char *end = NULL;
    const char *tail;
    unsigned long parsed;
    if (text == NULL || value == NULL || *text == '\0') {
        return -1;
    }
    errno = 0;
    parsed = strtoul(text, &end, 10);
    tail = end;
    while (*tail != '\0' && isspace((unsigned char)*tail)) {
        ++tail;
    }
    if (errno != 0 || end == text || *tail != '\0' || parsed == 0 || parsed > 8192u) {
        return -1;
    }
    *value = (unsigned int)parsed;
    return 0;
}

static int parse_uint_allow_zero(const char *text, unsigned int maximum, unsigned int *value)
{
    char *end = NULL;
    const char *tail;
    unsigned long parsed;
    if (text == NULL || value == NULL || *text == '\0') {
        return -1;
    }
    errno = 0;
    parsed = strtoul(text, &end, 10);
    tail = end;
    while (*tail != '\0' && isspace((unsigned char)*tail)) {
        ++tail;
    }
    if (errno != 0 || end == text || *tail != '\0' || parsed > maximum) {
        return -1;
    }
    *value = (unsigned int)parsed;
    return 0;
}

int fb_parse_size(const char *text, unsigned int *width, unsigned int *height)
{
    char *end = NULL;
    const char *tail;
    unsigned long parsed_width;
    unsigned long parsed_height;
    if (text == NULL || width == NULL || height == NULL) {
        return -1;
    }
    errno = 0;
    parsed_width = strtoul(text, &end, 10);
    if (errno != 0 || end == text || (*end != 'x' && *end != 'X')) {
        return -1;
    }
    text = end + 1;
    errno = 0;
    parsed_height = strtoul(text, &end, 10);
    tail = end;
    while (*tail != '\0' && isspace((unsigned char)*tail)) {
        ++tail;
    }
    if (errno != 0 || end == text || *tail != '\0' || parsed_width == 0 || parsed_height == 0 ||
        parsed_width > 8192u || parsed_height > 8192u) {
        return -1;
    }
    *width = (unsigned int)parsed_width;
    *height = (unsigned int)parsed_height;
    return 0;
}

void fb_config_defaults(struct fb_config *config)
{
    if (config != NULL) {
        memset(config, 0, sizeof(*config));
        config->system_auto = 1;
        config->fps = 15;
    }
}

static int apply_value(struct fb_config *config, const char *key, const char *value)
{
    unsigned int parsed;
    unsigned int width;
    unsigned int height;
    if (strcmp(key, "virtual_size") == 0) {
        return fb_parse_size(value, &config->virtual_width, &config->virtual_height);
    }
    if (strcmp(key, "virtual_width") == 0) {
        return parse_uint(value, &config->virtual_width);
    }
    if (strcmp(key, "virtual_height") == 0) {
        return parse_uint(value, &config->virtual_height);
    }
    if (strcmp(key, "system_size") == 0 || strcmp(key, "system") == 0) {
        if (strcmp(value, "auto") == 0) {
            config->system_auto = 1;
            config->system_width = 0;
            config->system_height = 0;
            return 0;
        }
        if (fb_parse_size(value, &width, &height) != 0) {
            return -1;
        }
        config->system_auto = 0;
        config->system_width = width;
        config->system_height = height;
        return 0;
    }
    if (strcmp(key, "system_width") == 0) {
        if (parse_uint(value, &config->system_width) != 0) {
            return -1;
        }
        config->system_auto = 0;
        return 0;
    }
    if (strcmp(key, "system_height") == 0) {
        if (parse_uint(value, &config->system_height) != 0) {
            return -1;
        }
        config->system_auto = 0;
        return 0;
    }
    if (strcmp(key, "mode") == 0) {
        if (strcmp(value, "fit") == 0) {
            config->stretch = 0;
            return 0;
        }
        if (strcmp(value, "stretch") == 0) {
            config->stretch = 1;
            return 0;
        }
        return -1;
    }
    if (strcmp(key, "fps") == 0) {
        if (parse_uint(value, &parsed) != 0 || parsed > 60u) {
            return -1;
        }
        config->fps = parsed;
        return 0;
    }
    if (strcmp(key, "seconds") == 0) {
        if (parse_uint_allow_zero(value, 86400u, &parsed) != 0) {
            return -1;
        }
        config->seconds = parsed;
        return 0;
    }
    if (strcmp(key, "program") == 0) {
        if (*value == '\0' || strlen(value) >= sizeof(config->program)) {
            return -1;
        }
        strcpy(config->program, value);
        return 0;
    }
    return 0;
}

int fb_config_load_file(struct fb_config *config, const char *path)
{
    FILE *file;
    char line[512];
    unsigned int line_number = 0;
    if (config == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *entry;
        char *separator;
        char *comment;
        ++line_number;
        comment = strchr(line, '#');
        if (comment != NULL) {
            *comment = '\0';
        }
        entry = trim(line);
        if (*entry == '\0') {
            continue;
        }
        separator = strchr(entry, '=');
        if (separator == NULL) {
            fclose(file);
            errno = EINVAL;
            return -1;
        }
        *separator = '\0';
        if (apply_value(config, trim(entry), trim(separator + 1)) != 0) {
            fclose(file);
            errno = EINVAL;
            return -1;
        }
    }
    if (ferror(file) != 0) {
        fclose(file);
        return -1;
    }
    fclose(file);
    (void)line_number;
    return 0;
}

static int apply_environment_value(struct fb_config *config, const char *name, const char *value)
{
    if (value == NULL) {
        return 0;
    }
    if (strcmp(name, "SR_ATS_FB_VIRTUAL_SIZE") == 0 || strcmp(name, "SR_ATS_FB_SYSTEM_SIZE") == 0) {
        unsigned int width;
        unsigned int height;
        if (strcmp(name, "SR_ATS_FB_SYSTEM_SIZE") == 0 && strcmp(value, "auto") == 0) {
            config->system_auto = 1;
            config->system_width = 0;
            config->system_height = 0;
            return 0;
        }
        if (fb_parse_size(value, &width, &height) != 0) {
            return -1;
        }
        if (strcmp(name, "SR_ATS_FB_VIRTUAL_SIZE") == 0) {
            config->virtual_width = width;
            config->virtual_height = height;
        } else {
            config->system_auto = 0;
            config->system_width = width;
            config->system_height = height;
        }
        return 0;
    }
    if (strcmp(name, "SR_ATS_FB_MODE") == 0 || strcmp(name, "SR_ATS_FB_FPS") == 0 ||
        strcmp(name, "SR_ATS_FB_SECONDS") == 0 || strcmp(name, "SR_ATS_FB_PROGRAM") == 0) {
        char key[32];
        if (strcmp(name, "SR_ATS_FB_MODE") == 0) {
            strcpy(key, "mode");
        } else if (strcmp(name, "SR_ATS_FB_FPS") == 0) {
            strcpy(key, "fps");
        } else if (strcmp(name, "SR_ATS_FB_SECONDS") == 0) {
            strcpy(key, "seconds");
        } else {
            strcpy(key, "program");
        }
        if (strcmp(key, "seconds") == 0) {
            return parse_uint_allow_zero(value, 86400u, &config->seconds);
        }
        return apply_value(config, key, value);
    }
    if (strcmp(name, "SR_ATS_FB_VIRTUAL_WIDTH") == 0) {
        return parse_uint(value, &config->virtual_width);
    }
    if (strcmp(name, "SR_ATS_FB_VIRTUAL_HEIGHT") == 0) {
        return parse_uint(value, &config->virtual_height);
    }
    if (strcmp(name, "SR_ATS_FB_SYSTEM_WIDTH") == 0) {
        config->system_auto = 0;
        return parse_uint(value, &config->system_width);
    }
    if (strcmp(name, "SR_ATS_FB_SYSTEM_HEIGHT") == 0) {
        config->system_auto = 0;
        return parse_uint(value, &config->system_height);
    }
    if (strcmp(name, "SR_ATS_FB_CONFIG") == 0) {
        return 0;
    }
    return 0;
}

int fb_config_load_environment(struct fb_config *config)
{
    static const char *const names[] = {
        "SR_ATS_FB_VIRTUAL_SIZE", "SR_ATS_FB_VIRTUAL_WIDTH", "SR_ATS_FB_VIRTUAL_HEIGHT",
        "SR_ATS_FB_SYSTEM_SIZE", "SR_ATS_FB_SYSTEM_WIDTH", "SR_ATS_FB_SYSTEM_HEIGHT",
        "SR_ATS_FB_MODE", "SR_ATS_FB_FPS", "SR_ATS_FB_SECONDS", "SR_ATS_FB_PROGRAM"
    };
    size_t index;
    if (config == NULL) {
        errno = EINVAL;
        return -1;
    }
    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (apply_environment_value(config, names[index], getenv(names[index])) != 0) {
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

int fb_config_validate(const struct fb_config *config, int require_program)
{
    uint64_t bytes;
    if (config == NULL || config->virtual_width == 0 || config->virtual_height == 0 ||
        config->virtual_width > 8192u || config->virtual_height > 8192u ||
        (!config->system_auto && (config->system_width == 0 || config->system_height == 0)) ||
        (!config->system_auto && (config->system_width > 8192u || config->system_height > 8192u)) ||
        config->fps == 0 || config->fps > 60u || config->seconds > 86400u ||
        (require_program && config->program[0] == '\0')) {
        errno = EINVAL;
        return -1;
    }
    bytes = (uint64_t)config->virtual_width * config->virtual_height * 2u;
    if (bytes == 0 || bytes > 256u * 1024u * 1024u) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
