/*
 * (C) Copyright 2017
 * Olliver Schinagl <o.schinagl@ultimaker.com>
 *
 * SPX-License-Identifier:	AGPL-3.0+
 *
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libevdev/libevdev.h>
#include <limits.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "version.h"

#define DISPLAY_MIN_XRES	800
#define DISPLAY_MIN_YRES	320
#define DISPLAY_MIN_BPP		(32 / CHAR_BIT)
#define DISPLAY_FRAME_RATE	60
#define DISPLAY_BG_CYCLE	60

#define INPUT_DEFAULT_XSIZE	50
#define INPUT_DEFAULT_YSIZE	40
#define INPUT_DEFAULT_FADE	2
#define INPUT_MAX_FADE		64

#define TEST_PATTERN_BORDER	1

#define DEV_INPUT_EVENT "/dev/input"
#define EVENT_DEV_NAME "event"
#define DEV_FB "/dev"
#define FB_DEV_NAME "fb"

#define CHAN_A		3
#define CHAN_R		2
#define CHAN_G		1
#define CHAN_B		0


static volatile bool renderloop_stop = false;

/**
 * siginit_handler - interrupt signal handler
 *
 * @signo:	interrupt signal causing the signal
 *
 * Whenever an interrupt signal is emitted (ctrl-c etc) this will tell the
 * renderloop to stop and cleanup.
 *
 */
void sigint_handler(int signo) {
	renderloop_stop = true;
}

/**
 * FPS() - convert a given framerate to milliseconds
 *
 * @__fps:	framerate in Hz to convert
 *
 * Return:	framerate in milliseconds
 */
#define FPS(__fps)	((1 * 1000) / ((__fps) ? (__fps) : 1))

/**
 * ARRAY_SIZE() - helper macro to get the number of elements in an array
 *
 * @__array:	array to get the size of
 *
 * Return:	the number of elements in an array.
 */
#define ARRAY_SIZE(__array) (sizeof(__array) / sizeof((__array)[0]))

/**
 * clamp() - helper macro to clamp a value
 *
 * @__val:	value to clamp
 * @__int:	interval to clamp at
 *
 * This helper macro is used to clamp an input @__val within a certain @__int.
 * For example, if @__val would be 125, then with a @__int of 50 the macro
 * will return the nearest down rounded interval, 100 in this case.
 *
 * In other words:
 *   __val
 * (   /   ) * __int
 *   __int
 *
 *
 * Return:	clamped value.
 */
#define clamp(__val, __int) \
	(((__val) / ((__int) > 0 ? (__int) : 1)) * (__int))

/**
 * sat_sub() - saturated subtraction
 *
 * @from:	input to subtract from
 * @val:	value to subtract
 *
 * This helper function does a branch-free subtraction that does not underflow.
 *
 * Return: saturated subtraction
 */
uint8_t sat_sub(uint8_t from, uint8_t val)
{
	uint8_t res;

	res = from - val;
	res &= -(res <= from);

	return res;
}

/**
 * sat_add() - saturated addition
 *
 * @from:	input to add to
 * @val:	value to add
 *
 * This helper function does a branch-free addition that does not overflow.
 *
 * Return: saturated addition
 */
uint8_t sat_add(uint8_t to, uint8_t val)
{
	uint8_t res;

	res = to + val;
	res |= -(res < to);

	return res;
}

/**
 * struct display_info - framebuffer display information structure
 *
 * @fb_dev:		file descriptor to a framebuffer device
 * @fb:			pointer to a memory mapped framebuffer
 * @id:			identifier/name of the framebuffer
 * @xres:		current resolution along the X-axis of the framebuffer
 * @yres:		current resolution along the Y-axis of the framebuffer
 * @bpp:		current bytes per pixel of the framebuffer
 * @fb_len:		number of bytes of the current framebuffer
 * @line_length:	the length, in bytes, of a line of the current framebuffer
 */
struct display_info {
	int fb_dev;
	uint8_t *fb;
	char *id;
	uint32_t xres;
	uint32_t yres;
	uint8_t bpp;
	size_t fb_len;
	uint32_t line_length;
};

/**
 * version() - prints the program version string
 */
static void version(void)
{
	printf("%s\n", UCIT_VERSION);
};

/**
 * usage() - prints program usage
 *
 * @argv0:	string indicating program name
 */
static void usage(char *argv0)
{
	printf("Usage: %s [OPTION] ... [<fb_dev> [<ev_dev>]]\n"
	       "  -a, --abort				abort touch test ok\n"
	       "  -e, --evdev=<event_dev>		force event device <ev_dev>t\n"
	       "  -f, --fbdev=<fb_dev>			force framebuffer device <fb_dev>\n"
	       "  -t, --touchsize=<X[xY]>		input size X x Y of test pattern (default %ux%u)\n"
	       "  -s, --fadespeed <speed>		input fadeout speed (default %u)\n"
	       "  -b, --banding				enable banding of the background\n"
	       "  -v, --version				display program version and exit\n"
	       "  -h, --help				display this help and exit\n"
	       "\n"
	       "  fb_dev: Framebuffer device node (/dev/fb0 for example)\n"
	       "  event_dev:  Event device node (/dev/input/event0 for example)\n",
	       argv0, INPUT_DEFAULT_XSIZE, INPUT_DEFAULT_YSIZE, INPUT_DEFAULT_FADE);
}

/**
 * struct background_color - struct defining the primary colors
 *
 * @r:	8-bit red component
 * @g:	8-bit green component
 * @b:	8-bit blue component
 */
static struct color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} background_colors[] = {
	/* White */
	{ .r = UINT8_MAX, .g = UINT8_MAX, .b = UINT8_MAX },
	/* Black */
	{ .r =      0x00, .g =      0x00, .b =      0x00 },
	/* Red */
	{ .r = UINT8_MAX, .g =      0x00, .b =      0x00 },
	/* Green */
	{ .r =      0x00, .g = UINT8_MAX, .b =      0x00 },
	/* Blue */
	{ .r =      0x00, .g =      0x00, .b = UINT8_MAX },
};


/**
 * band_pixel() - perform line banding on the background
 *
 * @line_length:	visible length of a line
 * @addr:		address of the pixel (e.g. x*y*bpp)
 * @color:		color index of the background to band with
 * @red:		red color component to band
 * @green:		green color component to band
 * @blue:		blue color component to band
 *
 * If a display cannot show all colors properly an effect called banding
 * becomes visible. Two common examples is low bit-depth displays and broken
 * driver lines to the display. In both cases, colors do not have a smooth
 * gradient, but snap from one color to the next.
 *
 * This function will smooth course of the pixel over the length of the line
 * by splitting the line up in small bands where each band moves to the next
 * gradient.
 *
 * For example, with a band_width of 3 pixels on a line of total 9 pixels we get
 * | rgb rgb rgb | rgb-1 rgb-1 rgb-1 | rgb-2 rgb-2 rgb-2 |
 * | rgb rgb rgb | rgb-1 rgb-1 rgb-1 | rgb-2 rgb-2 rgb-2 |
 */
static inline void band_pixel(const uint32_t line_length, const uint32_t addr, const uint8_t color,
			      uint8_t *red, uint8_t *green, uint8_t *blue)
{
	static uint8_t band = UINT8_MAX;
	uint32_t band_width = (line_length / UINT8_MAX);

	/*
	 * Dragons be here. To accomplish banding we need to fade
	 * each line from 0 - background_colors[]. This is easily
	 * done by just subtracting the banding factor from the actual
	 * color up until the color is reached.
	 *
	 * Because the modulus will match on the very first line,
	 * we have to deal with a pre-mature addition. To work around
	 * that we start the band at '-1'. But to ensure our band does
	 * not do the addition after the reset, the order here matters.
	 */
	if ((addr % band_width) == 0)
		band = sat_add(band, 1);
	if ((addr % line_length) == 0)
		band = 0;

	*red   = sat_sub(background_colors[color].r, band);
	*green = sat_sub(background_colors[color].g, band);
	*blue  = sat_sub(background_colors[color].b, band);
}

/**
 * background_draw() - render the background with input events
 *
 * @buffer:	buffer to render the background and input events onto
 * @mask:	mask buffer to render input events into
 * @disp:	pointer to a valid and initialized display_info struct
 * @colorize:	indicator whether to change the background color
 *
 * This function combines a (predefined static) background color from
 * @background_colors and the input mask buffer. The combining operation is
 * to invert the mask onto the background.
 *
 * Note that it is currently assumed that there is no virtual space beyond
 * the xres and yres.
 *
 * Note that the @buffer and @mask buffer need to be the same size as the
 * framebuffer (e.g. fb_len as size for both).
 */
static void background_draw(uint8_t *buffer, const uint8_t *mask, struct display_info *disp, const bool banding, const bool colorize)
{
	static uint8_t c = 0;
	uint32_t i = 0;

	for (i = 0; i < disp->fb_len; i += disp->bpp) {
		uint8_t r = background_colors[c].r;
		uint8_t g = background_colors[c].g;
		uint8_t b = background_colors[c].b;

		if (banding)
			band_pixel(disp->line_length, i, c, &r, &g, &b);

		buffer[i + CHAN_R] = r ^ mask[i + CHAN_R];
		buffer[i + CHAN_G] = g ^ mask[i + CHAN_G];
		buffer[i + CHAN_B] = b ^ mask[i + CHAN_B];
		buffer[i + CHAN_A] = 0x00;
	}

	if (colorize) {
		c++;
		c %= ARRAY_SIZE(background_colors);
	}
}

/**
 * input_matrix_check() - check whether all grid coordinates where activated
 *
 * @matrix:		input verification matrix
 * @matrix_size:	number of elements in @matrix
 *
 * Checks if all rectangles in the @matrix have been activated. When so, print
 * this to stdout and reset the matrix.
 *
 * Return:		true if all elements where activated, false otherwise.
 */
static bool input_matrix_check(bool *matrix, const size_t matrix_size)
{
	size_t size = matrix_size;

	while (--size) {
		if (!matrix[size])
			return false;
	}

	puts("Input test: success");
	memset(matrix, false, matrix_size);

	return true;
}

/**
 * input_mark() - mark received input events
 *
 * @mask:	mask buffer to render input events into
 * @matrix:	input matrix buffer to mark input events into
 * @disp:	pointer to a valid and initialized display_info struct
 * @x:		x coordinate of input event to render
 * @y:		y coordinate of input event to render
 * @xsize:	size along the X-axis for the test pattern
 * @ysize:	size along the Y-axis for the test pattern
 *
 * This function will render a test pattern as input event into @buffer of
 * size <xsize>x<ysize> clamped to those sizes, separated by a border of
 * TEST_PATTERN_BORDER size. For potential automatic test verification the
 * input event within the square grid is also stored in @matrix.
 *
 * Note that the @mask buffer needs to be the same size as the framebuffer.
 */
static void input_draw(uint8_t *mask, bool *matrix, const struct display_info *disp,
		       int32_t x, int32_t y, uint32_t xsize, uint32_t ysize)
{
	uint32_t row = 0;

	x = clamp(x, xsize);
	y = clamp(y, ysize);

	row = (disp->line_length / disp->bpp / xsize);
	matrix[(row * (y / ysize)) + (x / xsize)] = true;

	xsize -= TEST_PATTERN_BORDER;
	ysize -= TEST_PATTERN_BORDER;

	for (row = y; row < (y + ysize); row++) {
		uint32_t col;

		for (col = x; col < (x + xsize); col++) {
			uint32_t coord;

			coord = (col * disp->bpp) + (row * disp->line_length);
			if ((coord + 3) > disp->fb_len)
				break;

			mask[coord + CHAN_R] = UINT8_MAX;
			mask[coord + CHAN_G] = UINT8_MAX;
			mask[coord + CHAN_B] = UINT8_MAX;
			mask[coord + CHAN_A] = 0x00;
		}
	}
}

/**
 * input_fade() - helper function to fade the input events away
 *
 * @mask:	mask buffer to render input events into
 * @mask_len:	size of mask buffer
 * @speed:	speed of fade (decay) of the test pattern
 *
 * Note that the mask buffer needs to be the same size as the framebuffer.
 *
 * Function to remove any input events using @speed as a sort of decay
 * speed.
 */
static void input_fade(uint8_t *mask, size_t mask_len, uint8_t speed)
{
	while (mask_len--)
		mask[mask_len] = sat_sub(mask[mask_len], speed);
}

/**
 * renderloop() - main render loop and input handling
 *
 * @evdev:	pointer to a valid and initialized libevdev struct
 * @disp:	pointer to a valid, initialized and mmaped display_info struct
 * @xsize:	size along the X-axis for the test pattern
 * @ysize:	size along the Y-axis for the test pattern
 * @fade:	speed of fade (decay) of the test pattern
 * @banding:	enable banding of the background test pattern
 * @abort:	abort if touch test is ok
 *
 * This function takes the supplied parameters and uses these to render the
 * main application to @disp. The input itself is rendered into a buffer
 * and then inverts the main background. This so that the test pattern remains
 * visible after it has been asserted. In the mainloop a frame is copied from
 * the backbuffer to the framebuffer once every DISPLAY_FRAME_RATE. The rest
 * of the time is used to scan for input.
 *
 * Return:	0 on success, an error code otherwise.
 */
static int renderloop(struct libevdev *evdev, struct display_info *disp,
		      uint32_t xsize, uint32_t ysize, uint32_t fade,
		      const bool banding, const bool abort)
{
	bool frame_drawn = false;
	bool update_input = false;
	clock_t offset = 0;
	size_t matrix_size = (disp->xres / xsize) * (disp->yres / ysize);
	bool matrix[matrix_size];
	uint32_t elapsed = 0;
	uint8_t *backbuffer = NULL, *touchmask = NULL;

	memset(disp->fb, 0x00, disp->fb_len);

	memset(matrix, false, matrix_size);

	backbuffer = (uint8_t *)calloc(disp->fb_len, sizeof(uint8_t));
	if (!backbuffer)
		return -ENOMEM;

	touchmask = (uint8_t *)calloc(disp->fb_len, sizeof(uint8_t));
	if (!touchmask)
		return -ENOMEM;

	offset = clock();

	while (!renderloop_stop) {
		int next_event;
		int x, y;
		struct input_event event;
		uint32_t msec;

		msec = (clock() - offset) * 1000 / CLOCKS_PER_SEC;

		next_event = libevdev_next_event(evdev, LIBEVDEV_READ_FLAG_NORMAL, &event);
		if (next_event == LIBEVDEV_READ_STATUS_SUCCESS) {
			libevdev_fetch_event_value(evdev, EV_ABS, ABS_X, &x);
			libevdev_fetch_event_value(evdev, EV_ABS, ABS_Y, &y);
			update_input = true;
		}

		if ((msec % FPS(DISPLAY_FRAME_RATE)) != 0) {
			frame_drawn = false;
		} else {
			if (!frame_drawn) {
				bool bg_cycle_color = (elapsed > DISPLAY_BG_CYCLE);

				memcpy(disp->fb, backbuffer, disp->fb_len);
				frame_drawn = true;

				input_fade(touchmask, disp->fb_len, fade);

				background_draw(backbuffer, touchmask, disp, banding, bg_cycle_color);

				if (bg_cycle_color)
					elapsed = 0;
				else
					elapsed++;
			}
			if (update_input) {
				input_mark(touchmask, matrix, disp, x, y, xsize, ysize);

				if (input_matrix_check(matrix, matrix_size) && abort)
					break;

				update_input = false;
			}
		}
	}

	free(backbuffer);
	free(touchmask);

	printf("\nTest finished.\n");

	return 0;
}

/**
 * evdev_open() - open an input event device node
 *
 * @path:	required parameter to a event device node path
 *
 * Note that the caller is responsible for calling libedev_free() when done
 * using the returned device pointer.
 *
 * Return:	a valid pointer to a libevdev structure on success, NULL otherwise.
 */
static struct libevdev *evdev_open(const char *path)
{
	struct libevdev *evdev = NULL;
	int ret = -1;
	int fd = -1;

	fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "Unable to open '%s': %s\n", path, strerror(errno));
		return NULL;
	}

	ret = libevdev_new_from_fd(fd, &evdev);
	if (ret) {
		fprintf(stderr, "Failed to create evdev for '%s': %s\n", path, strerror(ret));
		close(fd);
	}

	return evdev;
}

/**
 * is_event_device - scandir helper to find an event device
 *
 * @dir:	pointer to a dirent structure
 *
 * Return:	1 if dir->d_name contains EVENT_DEV_NAME, 0 otherwise.
 */
static int is_event_device(const struct dirent *dir)
{
	return (strncmp(EVENT_DEV_NAME, dir->d_name, sizeof(EVENT_DEV_NAME) - 1) == 0);
}

/**
 * evdev_get_device() - get an event device
 *
 * @path:	optional parameter to a unix file path (/dev/event/input0 for ex.)
 *
 * If path is not NULL, this function will try to open the supplied device,
 * otherwise it will try to find and open a input event device in DEV_INPUT_EVENT
 * that has the type EV_ABS, ABS_X and ABS_Y axis.
 *
 * Note that the caller is responsible for calling libedev_free() when done
 * using the returned device pointer.
 *
 * Return:	a valid pointer to a libevdev structure on success, NULL otherwise.
 */
static struct libevdev *evdev_get_device(char *path)
{
	struct libevdev *evdev = NULL;

	if (!path) {
		int i;
		int ndev;
		struct dirent **namelist;

		ndev = scandir(DEV_INPUT_EVENT, &namelist, is_event_device, versionsort);
		if (ndev <= 0) {
			fprintf(stderr, "Failed to find event device in " DEV_INPUT_EVENT ": %s\n", strerror(errno));
			return NULL;
		}

		for (i = 0; i < ndev; i++) {
			int ret;

			ret = asprintf(&path, DEV_INPUT_EVENT "/%s", namelist[i]->d_name);
			free(namelist[i]);
			if (ret < 0) {
				fprintf(stderr, "Failed to create path for device %d: %s\n", i, strerror(errno));
				continue;
			}

			evdev = evdev_open(path);
			if (evdev) {
				if ((libevdev_has_event_type(evdev, EV_ABS)) &&
				    (libevdev_has_event_code(evdev, EV_ABS, ABS_X)) &&
				    (libevdev_has_event_code(evdev, EV_ABS, ABS_Y))) {
					break;
				} else {
					fprintf(stderr, "Skipping invalid touch UI device '%s' (%s).\n", path, libevdev_get_name(evdev));
					libevdev_free(evdev);
					evdev = NULL;
				}
			}
		}
		free(namelist);
	} else {
		evdev = evdev_open(path);
	}
	if ((!evdev) || (!path))
		goto err_out;

	printf("Found capable device at '%s'.\n", path);
	printf("Input device name: '%s'\n", libevdev_get_name(evdev));
	printf("Input device ID: bus %#x vendor %#x product %#x\n",
	       libevdev_get_id_bustype(evdev),
	       libevdev_get_id_vendor(evdev),
	       libevdev_get_id_product(evdev));
	printf("Evdev version: %x\n", libevdev_get_driver_version(evdev));
	printf("Phys location: %s\n", libevdev_get_phys(evdev));
	printf("Uniq identifier: %s\n", libevdev_get_uniq(evdev));

	free(path);

	return evdev;

err_out:
	if (evdev)
		libevdev_free(evdev);
	if (path)
		free(path);

	return NULL;
}

/**
 * disp_free() - free display_info structure
 *
 * @disp:	a pointer to a display_info structure
 *
 * Cleans up and free's the display_info structure.
 */
static void disp_free(struct display_info *disp)
{
	if (!disp)
		return;

	if (disp->id)
		free(disp->id);

	if (disp->fb)
		munmap(disp->fb, disp->fb_len);

	close(disp->fb_dev);

	free(disp);
}

/**
 * is_fb_device - scandir helper to find a framebuffer device
 *
 * @dir:	pointer to a dirent structure
 *
 * Return:	1 if dir->d_name contains FB_DEV_NAME, 0 otherwise.
 */
static int is_fb_device(const struct dirent *dir)
{
	return (strncmp(FB_DEV_NAME, dir->d_name, sizeof(FB_DEV_NAME) - 1) == 0);
}

/**
 * disp_open() - open a framebuffer device node and initialize display_info
 *
 * @path:	required parameter to a framebuffer device node path
 *
 * This function opens the supplied framebuffer device path and queries certain
 * parameters from it to initialize struct display_info with.
 *
 * Note that the caller is responsible for calling disp_free() when done
 * using the returned device pointer.
 *
 * Return:	an initialized display_info structure pointer on success,
 * 		NULL otherwise.
 */
static struct display_info *disp_open(const char *path)
{
	struct fb_var_screeninfo var_info = { 0 };
	struct fb_fix_screeninfo fix_info = { 0 };
	struct display_info *disp = NULL;
	int ret;

	if (!path) {
		fprintf(stderr, "Missing device name\n");
		return NULL;
	}

	disp = calloc(1, sizeof(struct display_info));
	if (!disp) {
		fprintf(stderr, "Failed to allocate memory: %s\n", strerror(errno));
		return NULL;
	}

	disp->fb_dev = open(path, O_RDWR);
	if (disp->fb_dev < 0) {
		fprintf(stderr, "Failed to open '%s': %s.\n", path, strerror(disp->fb_dev));
		goto err_mem;
	}

	ret = ioctl(disp->fb_dev, FBIOGET_VSCREENINFO, &var_info);
	if (ret < 0) {
		fprintf(stderr, "Unable to get var info: %s.\n", strerror(ret));
		goto err_fb_dev;
	}
	disp->xres = var_info.xres;
	disp->yres = var_info.yres;
	disp->bpp = var_info.bits_per_pixel / CHAR_BIT;

	ret = ioctl(disp->fb_dev, FBIOGET_FSCREENINFO, &fix_info);
	if (ret < 0) {
		fprintf(stderr, "Unable to get fixed info: %s.\n", strerror(ret));
		goto err_fb_dev;
	}
	disp->id = strlen(fix_info.id) ? strdup(fix_info.id) : strdup("(null)");
	disp->fb_len = fix_info.smem_len;
	disp->line_length = fix_info.line_length;

	return disp;

err_fb_dev:
	close(disp->fb_dev);
err_mem:
	free(disp);

	return NULL;
}

/**
 * disp_get_device() - get a display device
 *
 * @path:	optional parameter to a unix file path (/dev/fb0 for ex.)
 *
 * If path is not NULL, this function will try to open the supplied device,
 * otherwise it will try to find and open a framebuffer device in DEV_FB that
 * is at least DISPLAY_MIN_XRES by DISPLAY_MIN_YRES with exactly DISPLAY_MIN_BPP
 * pixel byte depth.
 *
 * Note that the caller is responsible for calling disp_free() when done using
 * the returned device pointer.
 *
 * Return:	a valid and mmapped display_info structure pointer on success,
 * 		NULL otherwise.
 */
static struct display_info *disp_get_device(char *path)
{
	struct display_info *disp = NULL;

	if (!path) {
		int i;
		int ndev;
		struct dirent **namelist;

		ndev = scandir(DEV_FB, &namelist, is_fb_device, versionsort);

		if (ndev <= 0) {
			fprintf(stderr, "Failed to find valid framebuffer device: %s\n", strerror(errno));
			return NULL;
		}

		for (i = 0; i < ndev; i++) {
			int ret;

			ret = asprintf(&path, DEV_FB "/%s", namelist[i]->d_name);
			free(namelist[i]);
			if (ret < 0) {
				fprintf(stderr, "Failed to create path for device %d: %s\n", i, strerror(errno));
				continue;
			}

			disp = disp_open(path);
			if (disp) {
				if ((disp->xres >= DISPLAY_MIN_XRES) &&
				    (disp->yres >= DISPLAY_MIN_YRES) &&
				    (disp->bpp == DISPLAY_MIN_BPP)) {
						break;
				} else {
					fprintf(stderr, "Skipping invalid display device '%s' (%s).\n", path, disp->id);
					disp_free(disp);
					disp = NULL;
				}
			}
		}
		free(namelist);
	} else {
		disp = disp_open(path);
	}
	if (!disp || !path)
		goto err_out;

	disp->fb = (uint8_t *)mmap(NULL, disp->fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, disp->fb_dev, 0);
	if (disp->fb == MAP_FAILED) {
		fprintf(stderr, "Failed to map framebuffer: %s.\n", strerror(errno));
		disp->fb = NULL;
		goto err_out;
	}

	printf("Found capable device at '%s'.\n", path);
	printf("Display device name: '%s'\n", disp->id);
	printf("Display resolution: '%d x %d @%dbpp'.\n", disp->xres, disp->yres, disp->bpp * CHAR_BIT);

	free(path);

	return disp;

err_out:
	if (disp)
		disp_free(disp);
	if (path)
		free(path);

	return NULL;
}

/**
 * parse_opts() - parses command line argument options
 *
 * @argc:	argument count, as passed from main()
 * @argv:	argument list, as passed from main()
 * @abort:	returns the abort on test success setting
 * @fbpath:	returns the frame buffer device supplied via -f or NULL
 * @evpath:	returns the input event device supplied via -e or NULL
 * @xsize:	returns the size along the X-axis for the test pattern
 * @ysize:	returns the size along the Y-axis for the test pattern
 * @fadespeed:	returns the speed of fade (decay) of the test pattern
 *
 * This function parses the command line arguments as supplied to the program,
 * tests some for validity and returns these values. Invalid parameters cause
 * defaults to be set where appropriate. Note that --version and --help will
 * exit() the program.
 *
 * Return:	0 on success or an error otherwise.
 */
static int parse_opts(int argc, char *argv[], bool *abort, char **fbpath, char **evpath, uint32_t *xsize, uint32_t *ysize, uint32_t *fadespeed, bool *banding)
{
	int c;
	int option_index = 0;
	static struct option long_options[] = {
		{ "abort",	no_argument,		NULL, 'a' },
		{ "fb", 	required_argument,	NULL, 'f' },
		{ "evdev",	required_argument,	NULL, 'e' },
		{ "touchsize",	required_argument,	NULL, 't' },
		{ "fadespeed",	required_argument,	NULL, 's' },
		{ "banding",	no_argument,		NULL, 'b' },
		{ "version",	no_argument,		NULL, 'v' },
		{ "help",	no_argument,		NULL, 'h' },
		{ NULL,		0,			NULL, 0 }
	};

	*abort = false;
	*banding = false;
	*evpath = NULL;
	*fadespeed = INPUT_DEFAULT_FADE;
	*fbpath = NULL;
	*xsize = INPUT_DEFAULT_XSIZE;
	*ysize = INPUT_DEFAULT_YSIZE;
	while ((c = getopt_long(argc, argv, "ae:f:t:s:bvh", long_options, &option_index)) != -1) {
		switch(c) {
		case 'a':
			*abort = true;
			break;
		case 'e':
			*evpath = strdup(optarg);
			break;
		case 'f':
			*fbpath = strdup(optarg);
			break;
		case 't':
			if (sscanf(optarg, "%ux%u", xsize, ysize) != 2) {
				*xsize = atoi(optarg);
				*ysize = *xsize;
			}
			if (*ysize == 0)
				*ysize = INPUT_DEFAULT_YSIZE;
			if (*xsize == 0)
				*xsize = INPUT_DEFAULT_XSIZE;
			break;
		case 's':
			*fadespeed = atoi(optarg);
			if (*fadespeed > INPUT_MAX_FADE)
				*fadespeed = INPUT_MAX_FADE;
			break;
		case 'b':
			*banding = true;
			break;
		case 'v':
			version();
			exit(EXIT_SUCCESS);
			break;
		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;
		case ':':
			/* fall through */
		case '?':
			/* fall through */
		default:
			exit(EXIT_FAILURE);
			break;
		}
	}
	if (optind < argc) {
		*fbpath = strdup(argv[optind]);
		optind++;
	}
	if (optind < argc) {
		*evpath = strdup(argv[optind]);
	}

	return 0;
}

int main(int argc, char *argv[])
{
	bool abort = false;
	bool banding = false;
	char *evpath = NULL;
	char *fbpath = NULL;
	int ret = EXIT_SUCCESS;
	struct display_info *disp = NULL;
	struct libevdev *evdev = NULL;
	struct sigaction act = { 0 };
	uint32_t fade = INPUT_DEFAULT_FADE;
	uint32_t xsize = INPUT_DEFAULT_XSIZE;
	uint32_t ysize = INPUT_DEFAULT_YSIZE;

	act.sa_handler = sigint_handler;
	sigaction(SIGINT, &act, NULL);

	ret = parse_opts(argc, argv, &abort, &fbpath, &evpath, &xsize, &ysize, &fade, &banding);
	if (ret)
		return EXIT_FAILURE;

	disp = disp_get_device(fbpath);
	if (!disp)
		return EXIT_FAILURE;

	evdev = evdev_get_device(evpath);
	if (!evdev) {
		ret = EXIT_FAILURE;
		goto err_disp;
	}

	renderloop(evdev, disp, xsize, ysize, fade, banding, abort);

	libevdev_free(evdev);

err_disp:
	disp_free(disp);

	if (fbpath)
		free(fbpath);
	if (evpath)
		free(evpath);

	return ret;
}
