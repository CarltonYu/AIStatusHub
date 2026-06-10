#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#define DEFAULT_FPS 60

struct fb_ctx {
	int fd;
	uint8_t *mem;
	size_t bytes;
	struct fb_fix_screeninfo finfo;
	struct fb_var_screeninfo vinfo;
};

static ssize_t read_all(int fd, void *buf, size_t len)
{
	size_t total = 0;

	while (total < len) {
		ssize_t n = read(fd, (uint8_t *)buf + total, len - total);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			break;
		total += (size_t)n;
	}

	return (ssize_t)total;
}

static long long now_us(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}

static void wait_until_us(long long deadline)
{
	while (now_us() < deadline) {
		;
	}
}

static uint32_t parse_u32(const char *s)
{
	char *end;
	unsigned long v = strtoul(s, &end, 10);

	(void)end;
	return (uint32_t)v;
}

static uint32_t chan(uint8_t value, const struct fb_bitfield *field)
{
	if (!field->length)
		return 0;

	return (uint32_t)(value >> (8 - field->length)) << field->offset;
}

static uint32_t pack_color(const struct fb_var_screeninfo *vinfo,
			   uint8_t r, uint8_t g, uint8_t b)
{
	return chan(r, &vinfo->red) |
	       chan(g, &vinfo->green) |
	       chan(b, &vinfo->blue);
}

static int is_rgb565(const struct fb_var_screeninfo *vinfo)
{
	return vinfo->bits_per_pixel == 16 &&
	       vinfo->red.offset == 11 && vinfo->red.length == 5 &&
	       vinfo->green.offset == 5 && vinfo->green.length == 6 &&
	       vinfo->blue.offset == 0 && vinfo->blue.length == 5;
}

static int open_fb(struct fb_ctx *fb, const char *dev)
{
	memset(fb, 0, sizeof(*fb));
	fb->fd = open(dev, O_RDWR);
	if (fb->fd < 0) {
		fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
		return -1;
	}

	if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->finfo) < 0) {
		perror("FBIOGET_FSCREENINFO");
		close(fb->fd);
		return -1;
	}

	if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vinfo) < 0) {
		perror("FBIOGET_VSCREENINFO");
		close(fb->fd);
		return -1;
	}

	if (fb->vinfo.bits_per_pixel != 16 && fb->vinfo.bits_per_pixel != 32) {
		fprintf(stderr, "unsupported framebuffer bpp: %u\n",
			fb->vinfo.bits_per_pixel);
		close(fb->fd);
		return -1;
	}

	fb->bytes = fb->finfo.smem_len;
	if (!fb->bytes)
		fb->bytes = (size_t)fb->finfo.line_length * fb->vinfo.yres;

	fb->mem = mmap(NULL, fb->bytes, PROT_READ | PROT_WRITE, MAP_SHARED,
		       fb->fd, 0);
	if (fb->mem == MAP_FAILED) {
		perror("mmap framebuffer");
		close(fb->fd);
		return -1;
	}

	printf("%s: %ux%u, %u bpp, line_length=%u, smem_len=%zu\n",
	       dev, fb->vinfo.xres, fb->vinfo.yres,
	       fb->vinfo.bits_per_pixel, fb->finfo.line_length, fb->bytes);
	return 0;
}

static void close_fb(struct fb_ctx *fb)
{
	if (fb->mem && fb->mem != MAP_FAILED)
		munmap(fb->mem, fb->bytes);
	if (fb->fd >= 0)
		close(fb->fd);
}

static void draw_frame(struct fb_ctx *fb, const uint8_t *src,
		       uint16_t width, uint16_t height)
{
	const int bytes_per_pixel = fb->vinfo.bits_per_pixel / 8;
	const int xoff = ((int)fb->vinfo.xres - width) / 2;
	const int yoff = ((int)fb->vinfo.yres - height) / 2;
	const int fast_rgb565 = is_rgb565(&fb->vinfo);
	uint16_t x;
	uint16_t y;

	for (y = 0; y < height; y++) {
		uint8_t *dst = fb->mem +
			(size_t)(y + yoff) * fb->finfo.line_length +
			(size_t)xoff * bytes_per_pixel;
		const uint8_t *row = src + (size_t)y * width * 2;

		for (x = 0; x < width; x++) {
			uint16_t rgb565 = ((uint16_t)row[x * 2] << 8) |
					  row[x * 2 + 1];

			if (fast_rgb565) {
				memcpy(dst + (size_t)x * 2, &rgb565, sizeof(rgb565));
			} else {
				uint8_t r = (uint8_t)(((rgb565 >> 11) & 0x1f) * 255 / 31);
				uint8_t g = (uint8_t)(((rgb565 >> 5) & 0x3f) * 255 / 63);
				uint8_t b = (uint8_t)((rgb565 & 0x1f) * 255 / 31);
				uint32_t pixel = pack_color(&fb->vinfo, r, g, b);

				memcpy(dst + (size_t)x * bytes_per_pixel,
				       &pixel, (size_t)bytes_per_pixel);
			}
		}
	}

	msync(fb->mem, fb->bytes, MS_ASYNC);
}

static void clear_fb(struct fb_ctx *fb)
{
	size_t visible_bytes = (size_t)fb->finfo.line_length * fb->vinfo.yres;

	memset(fb->mem, 0, visible_bytes);
	msync(fb->mem, visible_bytes, MS_SYNC);
}

int main(int argc, char **argv)
{
	const char *bin_path = argc > 1 ? argv[1] : "/root/eye_animation.bin";
	const char *fb_path = argc > 2 ? argv[2] : "/dev/fb0";
	uint32_t target_fps = argc > 3 ? parse_u32(argv[3]) : DEFAULT_FPS;
	uint32_t frame_delay_us = target_fps ? 1000000U / target_fps : 0;
	struct fb_ctx fb;
	uint8_t *frame_buf;
	uint32_t frame_count;
	uint32_t frame_size;
	uint16_t width;
	uint16_t height;
	uint32_t frame = 0;
	uint32_t frames_since_report = 0;
	long long last_report;
	long long next_frame_at;
	int anim_fd;

	setbuf(stdout, NULL);
	printf("Framebuffer eye animation player\n");
	printf("Usage: %s [bin_path] [fb_path] [target_fps, 0=uncapped]\n", argv[0]);

	anim_fd = open(bin_path, O_RDONLY);
	if (anim_fd < 0) {
		fprintf(stderr, "open %s: %s\n", bin_path, strerror(errno));
		return 1;
	}

	if (read_all(anim_fd, &frame_count, 4) != 4 ||
	    read_all(anim_fd, &frame_size, 4) != 4 ||
	    read_all(anim_fd, &width, 2) != 2 ||
	    read_all(anim_fd, &height, 2) != 2) {
		fprintf(stderr, "failed to read animation header\n");
		close(anim_fd);
		return 1;
	}

	if (frame_size != (uint32_t)width * height * 2) {
		fprintf(stderr, "invalid animation frame size\n");
		close(anim_fd);
		return 1;
	}

	if (open_fb(&fb, fb_path) < 0) {
		close(anim_fd);
		return 1;
	}

	if (width > fb.vinfo.xres || height > fb.vinfo.yres) {
		fprintf(stderr, "animation %ux%u does not fit framebuffer %ux%u\n",
			width, height, fb.vinfo.xres, fb.vinfo.yres);
		close_fb(&fb);
		close(anim_fd);
		return 1;
	}

	clear_fb(&fb);

	frame_buf = malloc(frame_size);
	if (!frame_buf) {
		perror("malloc frame");
		close_fb(&fb);
		close(anim_fd);
		return 1;
	}

	if (target_fps) {
		printf("Animation: %u frames, %ux%u, target FPS: %u\n",
		       frame_count, width, height, target_fps);
	} else {
		printf("Animation: %u frames, %ux%u, target FPS: uncapped\n",
		       frame_count, width, height);
	}
	printf("Playing. Press Ctrl+C to stop.\n");

	last_report = now_us();
	next_frame_at = last_report;
	while (1) {
		ssize_t n;

		if (frame == 0 && lseek(anim_fd, 12, SEEK_SET) < 0) {
			perror("rewind animation");
			break;
		}

		n = read_all(anim_fd, frame_buf, frame_size);
		if (n != (ssize_t)frame_size) {
			fprintf(stderr, "Frame %u: short read %zd/%u\n",
				frame, n, frame_size);
			break;
		}

		draw_frame(&fb, frame_buf, width, height);
		frame = (frame + 1) % frame_count;
		frames_since_report++;

		if (frame_delay_us) {
			next_frame_at += frame_delay_us;
			if (now_us() > next_frame_at + (long long)frame_delay_us)
				next_frame_at = now_us();
			wait_until_us(next_frame_at);
		}

		if (now_us() - last_report >= 2000000) {
			long long report_now = now_us();
			float actual_fps = frames_since_report * 1000000.0f /
					   (report_now - last_report);
			printf("Actual FPS: %.1f\n", actual_fps);
			frames_since_report = 0;
			last_report = report_now;
		}
	}

	free(frame_buf);
	close_fb(&fb);
	close(anim_fd);
	return 0;
}
