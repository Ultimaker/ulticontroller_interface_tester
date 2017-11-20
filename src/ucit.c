/*
 * (C) Copyright 2017
 * Olliver Schinagl <o.schinagl@ultimaker.com>
 *
 * SPX-License-Identifier:	AGPL-3.0+
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "version.h"

static void help(char *argv0)
{
	fprintf(stderr, "UltiController Interface Tester version %s\n"
			"Usage: %s <fb_dev>\n"
			"  fb_dev: Framebuffer device node (/dev/fb0 for example)\n",
			UCIT_VERSION, argv0);
}

void draw_white(uint8_t *fb, ssize_t size)
{
	int i;

	for (i = 0; i < size; i += 4) {
		fb[i + 0] = 0xff;
		fb[i + 1] = 0xff;
		fb[i + 2] = 0xff;
	}
}

void draw_red(uint8_t *fb, ssize_t size)
{
	int i;

	for (i = 0; i < size; i += 4) {
		fb[i + 0] = 0xff;
		fb[i + 1] = 0x00;
		fb[i + 2] = 0x00;
	}
}

void draw_green(uint8_t *fb, ssize_t size)
{
	int i;

	for (i = 0; i < size; i += 4) {
		fb[i + 0] = 0x00;
		fb[i + 1] = 0xff;
		fb[i + 2] = 0x00;
	}
}

void draw_blue(uint8_t *fb, ssize_t size)
{
	int i;

	for (i = 0; i < size; i += 4) {
		fb[i + 0] = 0x00;
		fb[i + 1] = 0x00;
		fb[i + 2] = 0xff;
	}
}

void draw_black(uint8_t *fb, ssize_t size)
{
	int i;

	for (i = 0; i < size; i += 4) {
		fb[i + 0] = 0x00;
		fb[i + 1] = 0x00;
		fb[i + 2] = 0x00;
	}
}

int main(int argc, char *argv[])
{
	struct fb_var_screeninfo screen_info;
	struct fb_fix_screeninfo fixed_info;
	int fb_dev;
	int ret;
	ssize_t fb_len;
	uint8_t *fb;
	int bpp;

	errno = 0;

	if (argc < 2) {
		fprintf(stderr, "Insufficient arguments supplied.\n");
		help(argv[0]);

		return -1;
	}

	fb_dev = open(argv[1], O_RDWR);
	if (fb_dev < 0) {
		fprintf(stderr, "Failed to open '%s': %s.\n", argv[1], strerror(fb_dev));

		return -1;
	}

	ret = ioctl(fb_dev, FBIOGET_VSCREENINFO, &screen_info);
	if (ret < 0) {
		fprintf(stderr, "Unable to get screen info: %s.\n", strerror(ret));

		goto err_fb_dev;
	}

	ret = ioctl(fb_dev, FBIOGET_FSCREENINFO, &fixed_info);
	if (ret < 0) {
		fprintf(stderr, "Unable to get fixed info: %s.\n", strerror(ret));

		goto err_fb_dev;
	}

	bpp = screen_info.bits_per_pixel >> 3;
	fb_len = screen_info.xres * screen_info.yres * bpp;
	fb = (uint8_t *)mmap(NULL, fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_dev, 0);
	if (fb == MAP_FAILED) {
		fprintf(stderr, "Failed to map framebuffer: %s.\n", strerror(errno));

		goto err_fb_dev;
	}

	if (strlen(fixed_info.id) > 0)
		printf("Starting test on '%s'.\n", fixed_info.id);
	memset(fb, 0x00, fb_len);

	while (1) {
		draw_white(fb, fb_len);
		sleep(1);
		draw_red(fb, fb_len);
		sleep(1);
		draw_green(fb, fb_len);
		sleep(1);
		draw_blue(fb, fb_len);
		sleep(1);
		draw_black(fb, fb_len);
		sleep(1);
	}

	munmap(fb, fb_len);
err_fb_dev:
	close(fb_dev);

	return 0;
}
