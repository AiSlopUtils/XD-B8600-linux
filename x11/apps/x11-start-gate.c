/*
 * Timerless Xfbdev launch gate for the Casio EX-word SH7305 Linux port.
 *
 * The immutable, known-good initramfs starts Xfbdev in the background and
 * then polls for its socket.  This machine has no reliable scheduler tick;
 * that runnable poller can repeatedly win each voluntary scheduling point
 * and starve the X server.  Pause only that parent shell until Xorg's native
 * -displayfd readiness notification arrives, then let the original script
 * continue unchanged.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define REAL_XFBDEV "/opt/x11/usr/bin/Xfbdev.real"
#define MAX_ARGC 96

static char *server_argv[MAX_ARGC + 3];

static void resume_parent(pid_t parent)
{
	if (parent > 1)
		(void)kill(parent, SIGCONT);
}

static void wait_for_x_ready(int fd, pid_t parent)
{
	char buffer[32];

	for (;;) {
		ssize_t count = read(fd, buffer, sizeof(buffer));
		ssize_t index;

		if (count > 0) {
			for (index = 0; index < count; index++) {
				if (buffer[index] == '\n')
					goto finished;
			}
			continue;
		}
		if (count < 0 && errno == EINTR)
			continue;
		/* EOF or an error also releases startx to run its normal checks. */
		break;
	}

finished:
	close(fd);
	resume_parent(parent);
	_exit(0);
}

int main(int argc, char **argv)
{
	char fd_number[16];
	pid_t parent = getppid();
	pid_t notifier;
	pid_t detached;
	int ready_pipe[2];
	int saved_errno;
	int notifier_status;
	int fd_length;
	int index;

	if (argc < 1 || argc > MAX_ARGC) {
		fprintf(stderr, "Xfbdev gate: invalid argument count\n");
		return 125;
	}
	server_argv[0] = (char *)REAL_XFBDEV;
	for (index = 1; index < argc; index++) {
		if (!strcmp(argv[index], "-displayfd")) {
			fprintf(stderr, "Xfbdev gate: duplicate -displayfd\n");
			return 125;
		}
		server_argv[index] = argv[index];
	}

	if (parent <= 1) {
		fprintf(stderr, "Xfbdev gate: unsafe parent pid %ld\n",
			(long)parent);
		return 125;
	}
	if (pipe(ready_pipe) < 0) {
		perror("Xfbdev gate: pipe");
		return 125;
	}
	fd_length = snprintf(fd_number, sizeof(fd_number), "%d", ready_pipe[1]);
	if (fd_length < 1 || fd_length >= (int)sizeof(fd_number)) {
		close(ready_pipe[0]);
		close(ready_pipe[1]);
		fprintf(stderr, "Xfbdev gate: invalid readiness descriptor\n");
		return 125;
	}
	server_argv[argc] = (char *)"-displayfd";
	server_argv[argc + 1] = fd_number;
	server_argv[argc + 2] = NULL;

	if (kill(parent, SIGSTOP) < 0) {
		perror("Xfbdev gate: stop startx");
		close(ready_pipe[0]);
		close(ready_pipe[1]);
		return 125;
	}

	notifier = fork();
	if (notifier < 0) {
		perror("Xfbdev gate: fork");
		close(ready_pipe[0]);
		close(ready_pipe[1]);
		resume_parent(parent);
		return 125;
	}
	if (notifier == 0) {
		/*
		 * Detach the waiter so the real X server neither receives its
		 * SIGCHLD nor retains a zombie for the lifetime of the desktop.
		 * BusyBox init adopts and reaps this short-lived grandchild.
		 */
		detached = fork();
		if (detached < 0) {
			close(ready_pipe[0]);
			close(ready_pipe[1]);
			resume_parent(parent);
			_exit(125);
		}
		if (detached > 0) {
			close(ready_pipe[0]);
			close(ready_pipe[1]);
			_exit(0);
		}
		close(ready_pipe[1]);
		wait_for_x_ready(ready_pipe[0], parent);
	}
	do {
		detached = waitpid(notifier, &notifier_status, 0);
	} while (detached < 0 && errno == EINTR);
	if (detached != notifier || !WIFEXITED(notifier_status) ||
	    WEXITSTATUS(notifier_status) != 0) {
		close(ready_pipe[0]);
		close(ready_pipe[1]);
		resume_parent(parent);
		return 125;
	}
	close(ready_pipe[0]);

	execv(REAL_XFBDEV, server_argv);
	saved_errno = errno;
	close(ready_pipe[1]);
	resume_parent(parent);
	errno = saved_errno;
	fprintf(stderr, "Xfbdev gate: cannot exec %s: %s\n",
		REAL_XFBDEV, strerror(errno));
	return 127;
}
