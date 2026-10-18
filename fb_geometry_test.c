#include "fb_geometry.h"

#include <stdio.h>

int main(void)
{
    static const unsigned int cases[][10] = {
        {1280, 720, 1280, 1024, 0, 1280, 720, 0, 152, 1},
        {1280, 720, 1280, 1024, 1, 1280, 1024, 0, 0, 1},
        {1024, 768, 1024, 768, 0, 1024, 768, 0, 0, 1},
        {1920, 1080, 1024, 768, 0, 1024, 576, 0, 96, 1},
        {800, 600, 1920, 1080, 0, 1440, 1080, 240, 0, 1}
    };
    unsigned int passed = 0;
    unsigned int index;
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        struct fb_output_rect rect;
        int valid = fb_compute_output_rect(cases[index][0], cases[index][1], cases[index][2], cases[index][3],
            (int)cases[index][4], &rect);
        if (valid && rect.width == cases[index][5] && rect.height == cases[index][6] &&
            rect.left == cases[index][7] && rect.top == cases[index][8]) {
            passed++;
        }
    }
    {
        struct fb_output_rect rect;
        if (!fb_compute_output_rect(0, 720, 1280, 1024, 0, &rect) &&
            !fb_compute_output_rect(1280, 720, 1280, 1024, 2, &rect) &&
            !fb_compute_output_rect(8193, 720, 1280, 1024, 0, &rect)) {
            passed += 3;
        }
    }
    printf("GEOMETRY_ASSERTIONS=%u RESULT=%s NO_DEVICE_ACCESS=YES\n", passed,
        passed == sizeof(cases) / sizeof(cases[0]) + 3u ? "PASS" : "FAIL");
    return passed == sizeof(cases) / sizeof(cases[0]) + 3u ? 0 : 1;
}
