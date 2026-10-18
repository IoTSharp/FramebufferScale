#define _GNU_SOURCE
#include "fb_config.h"
#include "fb_geometry.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static pthread_once_t config_once = PTHREAD_ONCE_INIT;
static pthread_once_t display_once = PTHREAD_ONCE_INIT;
static struct fb_config config;
static struct fb_output_rect output;
static pthread_t painter;
static int config_ok;
static int initialized_ok;
static int painter_created;
static int initialization_error = ENODEV;
static int actual_fd = -1;
static int shadow_fd = -1;
static int running;
static unsigned char *actual_memory;
static unsigned char *saved_memory;
static uint16_t *shadow_memory;
static struct fb_fix_screeninfo actual_fix;
static struct fb_var_screeninfo actual_var;
static struct stat shadow_identity;
static size_t source_bytes;
static uint32_t native_colors[65536];
static unsigned int *horizontal_source;

static void load_configuration(void)
{
    const char *path = getenv("SR_ATS_FB_CONFIG");
    fb_config_defaults(&config);
    if (path == NULL || *path == '\0') {
        path = FB_CONFIG_DEFAULT_PATH;
    }
    if (fb_config_load_file(&config, path) != 0 && errno != ENOENT) {
        return;
    }
    if (fb_config_load_environment(&config) != 0 || fb_config_validate(&config, 0) != 0) {
        return;
    }
    config_ok = 1;
}

static int selected_program(void)
{
    const char *expected;
    char executable[4096];
    ssize_t length;
    const char *name;
    pthread_once(&config_once, load_configuration);
    expected = getenv("SR_ATS_FB_PROGRAM");
    if (expected == NULL || *expected == '\0') {
        expected = config.program;
    }
    length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (!config_ok || expected == NULL || *expected == '\0' || length <= 0) {
        return 0;
    }
    executable[length] = '\0';
    name = strrchr(executable, '/');
    return strcmp(name == NULL ? executable : name + 1, expected) == 0;
}

static uint32_t channel(unsigned int value, unsigned int maximum, struct fb_bitfield field)
{
    uint32_t encoded = 0;
    if (field.length != 0) {
        encoded = (uint32_t)((value * ((1u << field.length) - 1u) + maximum / 2u) / maximum);
        encoded <<= field.offset;
    }
    return encoded;
}

static void virtual_information(struct fb_var_screeninfo *var, struct fb_fix_screeninfo *fix)
{
    if (var != NULL) {
        memset(var, 0, sizeof(*var));
        var->xres = var->xres_virtual = config.virtual_width;
        var->yres = var->yres_virtual = config.virtual_height;
        var->bits_per_pixel = 16;
        var->red.offset = 11;
        var->red.length = 5;
        var->green.offset = 5;
        var->green.length = 6;
        var->blue.offset = 0;
        var->blue.length = 5;
        var->width = var->height = (uint32_t)-1;
    }
    if (fix != NULL) {
        memset(fix, 0, sizeof(*fix));
        memcpy(fix->id, "FramebufferScale", sizeof(fix->id));
        fix->smem_len = (uint32_t)source_bytes;
        fix->type = FB_TYPE_PACKED_PIXELS;
        fix->visual = FB_VISUAL_TRUECOLOR;
        fix->line_length = config.virtual_width * 2u;
        fix->accel = FB_ACCEL_NONE;
    }
}

static void restore_display(void)
{
    struct fb_var_screeninfo current;
    if (actual_memory != NULL && saved_memory != NULL &&
        syscall(SYS_ioctl, actual_fd, FBIOGET_VSCREENINFO, &current) == 0 &&
        current.xres == actual_var.xres && current.yres == actual_var.yres &&
        current.bits_per_pixel == actual_var.bits_per_pixel && current.xoffset == actual_var.xoffset &&
        current.yoffset == actual_var.yoffset) {
        memcpy(actual_memory, saved_memory, actual_fix.smem_len);
    }
}

static void *paint_frames(void *unused)
{
    struct timespec now;
    struct timespec deadline;
    struct timespec next;
    uint64_t frame;
    uint64_t maximum_frames = config.seconds == 0 ? UINT64_MAX : (uint64_t)config.fps * config.seconds;
    unsigned int x;
    unsigned int y;
    unsigned int bytes_per_pixel = actual_var.bits_per_pixel / 8u;
    unsigned char *row = malloc((size_t)output.width * bytes_per_pixel);
    uint16_t *snapshot = malloc(source_bytes);
    (void)unused;
    if (row != NULL && snapshot != NULL) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        deadline = now;
        deadline.tv_sec += config.seconds;
        next = now;
        for (y = 0; y < actual_var.yres; ++y) {
            memset(actual_memory + (y + actual_var.yoffset) * actual_fix.line_length +
                actual_var.xoffset * bytes_per_pixel, 0, actual_var.xres * bytes_per_pixel);
        }
        for (frame = 0; frame < maximum_frames && __atomic_load_n(&running, __ATOMIC_ACQUIRE); ++frame) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (config.seconds != 0 && (now.tv_sec > deadline.tv_sec ||
                (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec))) {
                break;
            }
            if (frame == 0 || memcmp(snapshot, shadow_memory, source_bytes) != 0) {
                memcpy(snapshot, shadow_memory, source_bytes);
                for (y = 0; y < output.height; ++y) {
                    unsigned int source_y = (unsigned int)((uint64_t)y * config.virtual_height / output.height);
                    for (x = 0; x < output.width; ++x) {
                        uint32_t pixel = native_colors[snapshot[source_y * config.virtual_width + horizontal_source[x]]];
                        if (bytes_per_pixel == 4) {
                            ((uint32_t *)row)[x] = pixel;
                        } else if (bytes_per_pixel == 2) {
                            ((uint16_t *)row)[x] = (uint16_t)pixel;
                        } else {
                            memcpy(row + x * bytes_per_pixel, &pixel, bytes_per_pixel);
                        }
                    }
                    memcpy(actual_memory + (y + output.top + actual_var.yoffset) * actual_fix.line_length +
                        (output.left + actual_var.xoffset) * bytes_per_pixel, row,
                        (size_t)output.width * bytes_per_pixel);
                }
            }
            next.tv_nsec += 1000000000L / config.fps;
            while (next.tv_nsec >= 1000000000L) {
                next.tv_nsec -= 1000000000L;
                ++next.tv_sec;
            }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        }
    }
    free(snapshot);
    free(row);
    restore_display();
    return NULL;
}

static void initialize_display(void)
{
    unsigned int index;
    unsigned int bytes_per_pixel;
    pthread_once(&config_once, load_configuration);
    if (!config_ok) {
        initialization_error = EINVAL;
    } else {
        source_bytes = (size_t)config.virtual_width * config.virtual_height * 2u;
        actual_fd = (int)syscall(SYS_openat, AT_FDCWD, "/dev/fb0", O_RDWR | O_CLOEXEC, 0);
        if (actual_fd < 0) {
            initialization_error = errno;
        } else if (syscall(SYS_ioctl, actual_fd, FBIOGET_FSCREENINFO, &actual_fix) != 0 ||
            syscall(SYS_ioctl, actual_fd, FBIOGET_VSCREENINFO, &actual_var) != 0) {
            initialization_error = errno;
        } else if (!config.system_auto && (actual_var.xres != config.system_width || actual_var.yres != config.system_height)) {
            initialization_error = EINVAL;
        } else if (actual_fix.type != FB_TYPE_PACKED_PIXELS || actual_fix.visual != FB_VISUAL_TRUECOLOR ||
            actual_var.grayscale != 0 || actual_var.nonstd != 0 ||
            (actual_var.bits_per_pixel != 16 && actual_var.bits_per_pixel != 24 && actual_var.bits_per_pixel != 32) ||
            actual_var.xres == 0 || actual_var.yres == 0 || actual_var.xres > 8192 || actual_var.yres > 8192 ||
            actual_fix.smem_len == 0 || actual_fix.smem_len > 256u * 1024u * 1024u ||
            (uint64_t)(actual_var.xoffset + actual_var.xres) * (actual_var.bits_per_pixel / 8u) > actual_fix.line_length ||
            ((uint64_t)actual_var.yoffset + actual_var.yres) * actual_fix.line_length > actual_fix.smem_len) {
            initialization_error = ENOTSUP;
        } else if (actual_var.red.length > 8 || actual_var.green.length > 8 || actual_var.blue.length > 8 ||
            actual_var.transp.length > 8 || actual_var.red.offset + actual_var.red.length > actual_var.bits_per_pixel ||
            actual_var.green.offset + actual_var.green.length > actual_var.bits_per_pixel ||
            actual_var.blue.offset + actual_var.blue.length > actual_var.bits_per_pixel ||
            actual_var.transp.offset + actual_var.transp.length > actual_var.bits_per_pixel ||
            actual_var.red.msb_right || actual_var.green.msb_right || actual_var.blue.msb_right || actual_var.transp.msb_right) {
            initialization_error = ENOTSUP;
        } else if (!fb_compute_output_rect(config.virtual_width, config.virtual_height, actual_var.xres,
            actual_var.yres, config.stretch, &output)) {
            initialization_error = EINVAL;
        } else {
            bytes_per_pixel = actual_var.bits_per_pixel / 8u;
            horizontal_source = calloc(output.width, sizeof(*horizontal_source));
            actual_memory = mmap(NULL, actual_fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, actual_fd, 0);
            saved_memory = malloc(actual_fix.smem_len);
            shadow_fd = (int)syscall(SYS_memfd_create, "framebuffer-scale-shadow", 1u);
            if (horizontal_source == NULL || actual_memory == MAP_FAILED || saved_memory == NULL ||
                shadow_fd < 0 || ftruncate(shadow_fd, (off_t)source_bytes) != 0 ||
                fstat(shadow_fd, &shadow_identity) != 0) {
                initialization_error = errno == 0 ? ENOMEM : errno;
                if (actual_memory == MAP_FAILED) {
                    actual_memory = NULL;
                }
            } else {
                shadow_memory = mmap(NULL, source_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, shadow_fd, 0);
                if (shadow_memory == MAP_FAILED) {
                    shadow_memory = NULL;
                    initialization_error = errno;
                } else {
                    memcpy(saved_memory, actual_memory, actual_fix.smem_len);
                    for (index = 0; index < output.width; ++index) {
                        horizontal_source[index] = (unsigned int)((uint64_t)index * config.virtual_width / output.width);
                    }
                    for (index = 0; index < 65536; ++index) {
                        native_colors[index] = channel((index >> 11) & 31u, 31u, actual_var.red) |
                            channel((index >> 5) & 63u, 63u, actual_var.green) |
                            channel(index & 31u, 31u, actual_var.blue) |
                            channel(1, 1, actual_var.transp);
                    }
                    __atomic_store_n(&running, 1, __ATOMIC_RELEASE);
                    initialization_error = pthread_create(&painter, NULL, paint_frames, NULL);
                    if (initialization_error == 0) {
                        painter_created = initialized_ok = 1;
                    }
                }
            }
            (void)bytes_per_pixel;
        }
    }
    if (!initialized_ok) {
        fprintf(stderr, "framebuffer-scale: initialization failed errno=%d\n", initialization_error);
    }
}

static int virtual_descriptor(int fd)
{
    struct stat identity;
    return initialized_ok && fstat(fd, &identity) == 0 && identity.st_dev == shadow_identity.st_dev &&
        identity.st_ino == shadow_identity.st_ino;
}

static int open_device(const char *path, int flags, mode_t mode)
{
    int result;
    if (path != NULL && strcmp(path, "/dev/fb0") == 0 && selected_program()) {
        pthread_once(&display_once, initialize_display);
        if (initialized_ok) {
            result = fcntl(shadow_fd, (flags & O_CLOEXEC) != 0 ? F_DUPFD_CLOEXEC : F_DUPFD, 3);
        } else {
            errno = initialization_error;
            result = -1;
        }
    } else {
        result = (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
    }
    return result;
}

int open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    int result;
    if ((flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    result = open_device(path, flags, mode);
    return result;
}

int open64(const char *path, int flags, ...)
{
    mode_t mode = 0;
    int result;
    if ((flags & O_CREAT) != 0 || (flags & O_TMPFILE) == O_TMPFILE) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    result = open_device(path, flags | O_LARGEFILE, mode);
    return result;
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    void *argument;
    int result = -1;
    va_start(args, request);
    argument = va_arg(args, void *);
    va_end(args);
    if (!virtual_descriptor(fd)) {
        result = (int)syscall(SYS_ioctl, fd, request, argument);
    } else if (argument == NULL && request != FBIOBLANK) {
        errno = EFAULT;
    } else if (request == FBIOGET_VSCREENINFO) {
        virtual_information(argument, NULL);
        result = 0;
    } else if (request == FBIOGET_FSCREENINFO) {
        virtual_information(NULL, argument);
        result = 0;
    } else if (request == FBIOPUT_VSCREENINFO || request == FBIOPAN_DISPLAY) {
        struct fb_var_screeninfo *var = argument;
        if (var->xres == config.virtual_width && var->yres == config.virtual_height && var->bits_per_pixel == 16 &&
            var->xres_virtual == config.virtual_width && var->yres_virtual == config.virtual_height &&
            var->xoffset == 0 && var->yoffset == 0) {
            virtual_information(var, NULL);
            result = 0;
        } else {
            errno = EINVAL;
        }
    } else if (request == FBIOBLANK || request == FBIOPUTCMAP) {
        result = 0;
    } else {
        errno = ENOTTY;
    }
    return result;
}

__attribute__((destructor)) static void release_display(void)
{
    if (painter_created) {
        __atomic_store_n(&running, 0, __ATOMIC_RELEASE);
        pthread_join(painter, NULL);
    }
    if (shadow_memory != NULL) {
        munmap(shadow_memory, source_bytes);
    }
    if (shadow_fd >= 0) {
        close(shadow_fd);
    }
    free(horizontal_source);
    free(saved_memory);
    if (actual_memory != NULL) {
        munmap(actual_memory, actual_fix.smem_len);
    }
    if (actual_fd >= 0) {
        close(actual_fd);
    }
}
