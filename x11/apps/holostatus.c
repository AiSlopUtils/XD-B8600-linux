#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NOTICE_PATH "/tmp/holo-notify"

static void
read_memory(long *available_kib, long *swap_total_kib, long *swap_free_kib)
{
	FILE *stream = fopen("/proc/meminfo", "r");
	char line[128];
	long value;

	*available_kib = *swap_total_kib = *swap_free_kib = 0;
	if (stream == NULL)
		return;
	while (fgets(line, sizeof(line), stream) != NULL) {
		if (sscanf(line, "MemAvailable: %ld kB", &value) == 1)
			*available_kib = value;
		else if (sscanf(line, "SwapTotal: %ld kB", &value) == 1)
			*swap_total_kib = value;
		else if (sscanf(line, "SwapFree: %ld kB", &value) == 1)
			*swap_free_kib = value;
	}
	fclose(stream);
}

static int
read_notice(char *message, size_t size)
{
	FILE *stream = fopen(NOTICE_PATH, "r");
	size_t length;

	if (stream == NULL)
		return 0;
	if (fgets(message, (int)size, stream) == NULL)
		message[0] = '\0';
	fclose(stream);
	(void)unlink(NOTICE_PATH);
	length = strlen(message);
	while (length > 0 && (message[length - 1] == '\n' ||
	                      message[length - 1] == '\r'))
		message[--length] = '\0';
	return length != 0;
}

static int
sd_mounted(void)
{
	FILE *stream = fopen("/proc/mounts", "r");
	char line[256];
	char source[96];
	char target[96];
	char type[32];
	int found = 0;

	if (stream == NULL)
		return 0;
	while (fgets(line, sizeof(line), stream) != NULL &&
	       sscanf(line, "%95s %95s %31s", source, target, type) == 3) {
		if (strcmp(target, "/mnt/sd") == 0) {
			found = 1;
			break;
		}
	}
	fclose(stream);
	return found;
}

int
main(void)
{
	char notice[96] = "";
	int notice_cycles = 0;

	(void)signal(SIGPIPE, SIG_DFL);
	(void)setvbuf(stdout, NULL, _IOLBF, 0);
	for (;;) {
		long available;
		long swap_total;
		long swap_free;
		time_t now;
		struct tm local;

		if (read_notice(notice, sizeof(notice)))
			notice_cycles = 2;
		if (notice_cycles > 0) {
			if (printf("! %.72s\n", notice) < 0)
				break;
			notice_cycles--;
		} else {
			read_memory(&available, &swap_total, &swap_free);
			now = time(NULL);
			if (localtime_r(&now, &local) == NULL)
				memset(&local, 0, sizeof(local));
			if (printf("RAM %ldM  SWP %ld/%ldM  %s%02d:%02d\n",
			           available / 1024,
			           (swap_total - swap_free) / 1024,
			           swap_total / 1024,
			           sd_mounted() ? "SD  " : "",
			           local.tm_hour, local.tm_min) < 0)
				break;
		}
		if (fflush(stdout) == EOF && errno == EPIPE)
			break;
		sleep(3);
	}
	return 0;
}
