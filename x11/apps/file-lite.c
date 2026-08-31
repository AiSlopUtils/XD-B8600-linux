#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int brief;

static void
print_type(const char *name, const char *type)
{
	if (!brief)
		printf("%s: ", name);
	puts(type);
}

static int
is_text(const unsigned char *data, size_t length)
{
	size_t i;

	for (i = 0; i < length; i++) {
		unsigned char c = data[i];
		if (c == 0)
			return 0;
		if (c < 0x20 && c != '\n' && c != '\r' && c != '\t' &&
		    c != '\f' && c != '\b')
			return 0;
	}
	return 1;
}

static const char *
classify(const unsigned char *p, size_t n)
{
	if (n == 0)
		return "empty";
	if (n >= 20 && !memcmp(p, "\177ELF", 4)) {
		static char elf[96];
		unsigned int machine = p[5] == 2 ?
			((unsigned int)p[18] << 8) | p[19] :
			((unsigned int)p[19] << 8) | p[18];
		snprintf(elf, sizeof(elf), "ELF %s-bit %s%s",
		         p[4] == 2 ? "64" : "32",
		         p[5] == 2 ? "MSB" : "LSB",
		         machine == 42 ? ", Renesas SH" : "");
		return elf;
	}
	if (n >= 2 && p[0] == '#' && p[1] == '!')
		return "executable script, text";
	if (n >= 4 && !memcmp(p, "hsqs", 4))
		return "SquashFS filesystem, little-endian";
	if (n >= 4 && !memcmp(p, "sqsh", 4))
		return "SquashFS filesystem, big-endian";
	if (n >= 6 && !memcmp(p, "\xfd" "7zXZ\0", 6))
		return "XZ compressed data";
	if (n >= 3 && p[0] == 0x1f && p[1] == 0x8b)
		return "gzip compressed data";
	if (n >= 3 && !memcmp(p, "BZh", 3))
		return "bzip2 compressed data";
	if (n >= 4 && p[0] == 0x28 && p[1] == 0xb5 &&
	    p[2] == 0x2f && p[3] == 0xfd)
		return "Zstandard compressed data";
	if (n >= 8 && !memcmp(p, "\x89PNG\r\n\x1a\n", 8))
		return "PNG image data";
	if (n >= 3 && p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff)
		return "JPEG image data";
	if (n >= 6 && (!memcmp(p, "GIF87a", 6) || !memcmp(p, "GIF89a", 6)))
		return "GIF image data";
	if (n >= 5 && !memcmp(p, "%PDF-", 5))
		return "PDF document";
	if (n >= 4 && !memcmp(p, "PK\003\004", 4))
		return "Zip archive data";
	if (n >= 6 && (!memcmp(p, "070701", 6) || !memcmp(p, "070702", 6)))
		return "ASCII cpio archive";
	if (n >= 512 && p[510] == 0x55 && p[511] == 0xaa)
		return "DOS/MBR boot sector";
	return is_text(p, n) ? "text" : "data";
}

static int
inspect(const char *name)
{
	unsigned char data[2048];
	struct stat status;
	ssize_t count;
	int fd;

	if (lstat(name, &status) < 0) {
		if (!brief)
			printf("%s: ", name);
		printf("cannot open (%s)\n", strerror(errno));
		return 1;
	}
	if (S_ISLNK(status.st_mode)) {
		char target[256];
		count = readlink(name, target, sizeof(target) - 1);
		if (count < 0)
			return 1;
		target[count] = '\0';
		if (!brief)
			printf("%s: ", name);
		printf("symbolic link to %s\n", target);
		return 0;
	}
	if (S_ISDIR(status.st_mode)) {
		print_type(name, "directory");
		return 0;
	}
	if (!S_ISREG(status.st_mode)) {
		print_type(name, S_ISCHR(status.st_mode) ? "character special" :
		                 S_ISBLK(status.st_mode) ? "block special" :
		                 S_ISFIFO(status.st_mode) ? "fifo" : "special");
		return 0;
	}
	fd = open(name, O_RDONLY);
	if (fd < 0) {
		if (!brief)
			printf("%s: ", name);
		printf("cannot open (%s)\n", strerror(errno));
		return 1;
	}
	count = read(fd, data, sizeof(data));
	close(fd);
	if (count < 0) {
		if (!brief)
			printf("%s: ", name);
		printf("cannot read (%s)\n", strerror(errno));
		return 1;
	}
	print_type(name, classify(data, (size_t)count));
	return 0;
}

int
main(int argc, char **argv)
{
	int first = 1;
	int result = 0;
	int i;

	if (argc > 1 && !strcmp(argv[1], "-b")) {
		brief = 1;
		first = 2;
	}
	if (first == argc) {
		fprintf(stderr, "usage: file [-b] FILE...\n");
		return 2;
	}
	for (i = first; i < argc; i++)
		result |= inspect(argv[i]);
	return result;
}
