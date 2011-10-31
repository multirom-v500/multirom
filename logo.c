/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/fb.h>
#include <linux/kd.h>

#include "init.h"

#ifdef ANDROID
#include <cutils/memory.h>
#else
void android_memset16(void *_ptr, unsigned short val, unsigned count)
{
    unsigned short *ptr = _ptr;
    count >>= 1;
    while(count--)
        *ptr++ = val;
}
#endif

int fb_open(struct FB *fb)
{
    fb->fd = open("/dev/graphics/fb0", O_RDWR);
    if (fb->fd < 0)
        return -1;

    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->fi) < 0)
        goto fail;
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vi) < 0)
        goto fail;

    fb->bits = mmap(0, fb_size(fb), PROT_READ | PROT_WRITE,
                    MAP_SHARED, fb->fd, 0);
    if (fb->bits == MAP_FAILED)
        goto fail;

    return 0;

fail:
    close(fb->fd);
    return -1;
}

void fb_close(struct FB *fb)
{
    munmap(fb->bits, fb_size(fb));
    close(fb->fd);
}

/* there's got to be a more portable way to do this ... */
void fb_update(struct FB *fb)
{
    fb->vi.yoffset = 1;
    ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->vi);
    fb->vi.yoffset = 0;
    ioctl(fb->fd, FBIOPUT_VSCREENINFO, &fb->vi);
}

int vt_set_mode(int graphics)
{
    int fd, r;
    fd = open("/dev/tty0", O_RDWR | O_SYNC);
    if (fd < 0)
        return -1;
    r = ioctl(fd, KDSETMODE, (void*) (graphics ? KD_GRAPHICS : KD_TEXT));
    close(fd);
    return r;
}

/* 565RLE image format: [count(2 bytes), rle(2 bytes)] */

int load_565rle_image(char *fn)
{
    struct FB fb;
    struct stat s;
    unsigned short *data, *bits, *ptr;
    uint32_t rgb32, red, green, blue, alpha;
    unsigned count, max;
    int fd;

    if (vt_set_mode(1)) 
        return -1;

    fd = open(fn, O_RDONLY);
    if (fd < 0) {
        ERROR("cannot open '%s'\n", fn);
        goto fail_restore_text;
    }

    if (fstat(fd, &s) < 0) {
        goto fail_close_file;
    }

    data = mmap(0, s.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
        goto fail_close_file;

    if (fb_open(&fb))
        goto fail_unmap_data;

    max = fb_width(&fb) * fb_height(&fb);
    ptr = data;
    count = s.st_size;
    bits = fb.bits;
    while (count > 3) {
        unsigned n = ptr[0];
        if (n > max)
            break;
                if (fb_bpp(&fb) == 16) {
                        android_memset16(bits, ptr[1], n << 1);
                        bits += n;
                } else {
                        /* convert 16 bits to 32 bits */
                        rgb32 = ((ptr[1] >> 11) & 0x1F);
                        red = (rgb32 << 3) | (rgb32 >> 2);
                        rgb32 = ((ptr[1] >> 5) & 0x3F);
                        green = (rgb32 << 2) | (rgb32 >> 4);
                        rgb32 = ((ptr[1]) & 0x1F);
                        blue = (rgb32 << 3) | (rgb32 >> 2);
                        alpha = 0xff;
                        rgb32 = (alpha << 24) | (blue << 16)
                        | (green << 8) | (red);
                        android_memset32((uint32_t *)bits, rgb32, n << 2);
                        bits += (n * 2);
               }
        max -= n;
        ptr += 2;
        count -= 4;
    }

    munmap(data, s.st_size);
    fb_update(&fb);
    fb_close(&fb);
    close(fd);
    // Why is it even doing this?
    //unlink(fn); 
    return 0;

fail_unmap_data:
    munmap(data, s.st_size);    
fail_close_file:
    close(fd);
fail_restore_text:
    vt_set_mode(0);
    return -1;
}

