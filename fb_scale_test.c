#define _GNU_SOURCE
#include "fb_geometry.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static uint32_t encode_channel(unsigned int value, unsigned int maximum, struct fb_bitfield field)
{
    if (field.length == 0) {
        return 0;
    }
    return ((value * ((1u << field.length) - 1u) + maximum / 2u) / maximum) << field.offset;
}

int main(void)
{
    int result = 1;
    int fd = -1;
    int actual_fd = -1;
    int duplicate = -1;
    int assertions = 0;
    struct fb_var_screeninfo logical;
    struct fb_var_screeninfo actual;
    struct fb_fix_screeninfo logical_fix;
    struct fb_fix_screeninfo actual_fix;
    uint16_t *pixels = NULL;
    unsigned char *actual_pixels = NULL;
    struct fb_output_rect rect;
    struct timespec start;
    struct timespec pause = {0, 300000000};
    const char *mode = getenv("SR_ATS_FB_MODE");
    int stretch = mode != NULL && strcmp(mode, "stretch") == 0;
    do {
        if ((mode != NULL && !stretch && strcmp(mode, "fit") != 0) ||
            (mode != NULL && strcmp(mode, "fit") != 0 && strcmp(mode, "stretch") != 0)) {
            break;
        }
        fd = open("/dev/fb0", O_RDWR);
        if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &logical) != 0 ||
            ioctl(fd, FBIOGET_FSCREENINFO, &logical_fix) != 0) {
            break;
        }
        if (logical.xres == 0 || logical.yres == 0 || logical.bits_per_pixel != 16 ||
            logical_fix.line_length != logical.xres * 2u) {
            break;
        }
        assertions++;
        if (ioctl(fd, FBIOPUT_VSCREENINFO, &logical) != 0) {
            break;
        }
        assertions++;
        logical.xres = logical.xres / 2u;
        errno = 0;
        if (ioctl(fd, FBIOPUT_VSCREENINFO, &logical) != -1 || errno != EINVAL) {
            break;
        }
        assertions++;
        duplicate = dup(fd);
        if (duplicate < 0 || ioctl(duplicate, FBIOGET_VSCREENINFO, &logical) != 0 || logical.bits_per_pixel != 16) {
            break;
        }
        assertions++;
        pixels = mmap(NULL, logical_fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (pixels == MAP_FAILED) {
            pixels = NULL;
            break;
        }
        for (unsigned int y = 0; y < logical.yres; ++y) {
            for (unsigned int x = 0; x < logical.xres; ++x) {
                unsigned int band = (unsigned int)((uint64_t)x * 4u / logical.xres);
                pixels[y * logical.xres + x] = (uint16_t[]){0xf800, 0x07e0, 0x001f, 0xffff}[band > 3u ? 3u : band];
            }
        }
        actual_fd = (int)syscall(SYS_openat, AT_FDCWD, "/dev/fb0", O_RDWR | O_CLOEXEC, 0);
        if (actual_fd < 0 || ioctl(actual_fd, FBIOGET_VSCREENINFO, &actual) != 0 ||
            ioctl(actual_fd, FBIOGET_FSCREENINFO, &actual_fix) != 0) {
            break;
        }
        actual_pixels = mmap(NULL, actual_fix.smem_len, PROT_READ, MAP_SHARED, actual_fd, 0);
        if (actual_pixels == MAP_FAILED) {
            actual_pixels = NULL;
            break;
        }
        if (!fb_compute_output_rect(logical.xres, logical.yres, actual.xres, actual.yres, stretch, &rect)) {
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &start);
        nanosleep(&pause, NULL);
        for (unsigned int index = 0; index < 4; ++index) {
            uint32_t observed = 0;
            uint32_t expected;
            unsigned int x = rect.left + rect.width * (index * 2u + 1u) / 8u;
            unsigned int y = rect.top + rect.height / 2u;
            unsigned int components[4][3] = {{31, 0, 0}, {0, 63, 0}, {0, 0, 31}, {31, 63, 31}};
            expected = encode_channel(components[index][0], 31, actual.red) |
                encode_channel(components[index][1], 63, actual.green) |
                encode_channel(components[index][2], 31, actual.blue);
            if (actual.transp.length != 0) {
                expected |= encode_channel(1, 1, actual.transp);
            }
            memcpy(&observed, actual_pixels + (y + actual.yoffset) * actual_fix.line_length +
                (x + actual.xoffset) * (actual.bits_per_pixel / 8u), actual.bits_per_pixel / 8u);
            printf("COLOR_%u observed=%08x expected=%08x\n", index, observed, expected);
            if (observed != expected) {
                break;
            }
            assertions++;
        }
        if (assertions < 8) {
            break;
        }
        printf("ASSERTIONS=%d VIRTUAL=%ux%ux%u ACTUAL=%ux%ux%u OUTPUT=%ux%u+%u+%u\n", assertions,
            logical.xres, logical.yres, logical.bits_per_pixel, actual.xres, actual.yres, actual.bits_per_pixel,
            rect.width, rect.height, rect.left, rect.top);
        result = 0;
    } while (0);
    if (actual_pixels != NULL) {
        munmap(actual_pixels, actual_fix.smem_len);
    }
    if (pixels != NULL) {
        munmap(pixels, logical_fix.smem_len);
    }
    if (duplicate >= 0) {
        close(duplicate);
    }
    if (actual_fd >= 0) {
        close(actual_fd);
    }
    if (fd >= 0) {
        close(fd);
    }
    printf("SELFTEST=%s\n", result == 0 ? "PASS" : "FAIL");
    return result;
}
