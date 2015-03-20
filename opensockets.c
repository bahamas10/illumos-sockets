/**
 * substitute for pfiles(1) that does as little as
 * possible to list open ports on a machine
 *
 * Author: Dave Eddy <dave@daveeddy.com>
 * Date: March 17, 2015
 * License: MIT
 */

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <libproc.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PROCFS "/proc"

#define DEBUG(...) do { if (opts.level >= 1) printf(__VA_ARGS__); } while (0)
#define TRACE(...) do { if (opts.level >= 2) printf(__VA_ARGS__); } while (0)

// CLI options
static struct {
	int level;  // log level, defaults to 0
	int header; // print header, defaults to true (1)
} opts;

static void show_socket(struct ps_prochandle *Pr, pid_t pid, int fd);
static void process_pid(pid_t pid);
static int is_socket(pid_t pid, int fd);

// print the usage message
static void usage(FILE *stream) {
	fprintf(stream, "usage: opensockets [-h] [-v] [-H] [[pid] ...]\n");
	fprintf(stream, "\n");
	fprintf(stream, "print all ports in use on the current system\n");
	fprintf(stream, "\n");
	fprintf(stream, "options\n");
	fprintf(stream, "  -h       print this message and exit\n");
	fprintf(stream, "  -H       don't print header\n");
	fprintf(stream, "  -v       increase verbosity\n");
}

int main(int argc, char **argv) {
	opts.header = 1;
	opts.level = 0;

	int c;
	while ((c = getopt(argc, argv, "hHv")) != -1) {
		switch (c) {
			case 'h':
				usage(stdout);
				return 0;
			case 'H':
				opts.header = 0;
				break;
			case 'v':
				opts.level++;
				break;
			case '?':
				usage(stderr);
				return 1;
		}
	}

	// header
	if (opts.header)
		printf("%-8s %-12s %-17s %-7s %s\n",
		    "PID", "EXEC", "IP", "PORT", "ARGS");

	pid_t me = getpid();
	DEBUG("pid = %d\n", me);

	// check for arguments (pids)
	int i;
	if (optind < argc) {
		// loop arguments as pids
		for (i = optind; i < argc; i++) {
			pid_t pid = atoi(argv[i]);
			process_pid(pid);
		}
		return 0;
	}

	// no extra arguments, loop all pids in /proc
	TRACE("opening %s\n", PROCFS);
	DIR *d = opendir(PROCFS);
	struct dirent *dp;
	if (!d) {
		fprintf(stderr, "failed to open %s: %s\n",
		    PROCFS, strerror(errno));
		return 1;
	}
	while ((dp = readdir(d))) {
		if (dp->d_name[0] == '.')
			continue;
		pid_t pid = atoi(dp->d_name);

		// skip ourself
		if (pid == me)
			continue;

		process_pid(pid);
	}
	closedir(d);

	return 0;
}

// process a pid
static void process_pid(pid_t pid) {
	struct ps_prochandle *Pr = NULL;
	DEBUG("processing pid %d\n", pid);

	// 1. loop the fds in /proc/<pid>/fd
	// This is done first to check to see if the process has any open
	// sockets.  This way, we can lazily Pgrab the process when the first
	// socket is discovered, and skip this step if the process contains no
	// sockets

	// loop fds in /proc/<pid>/fd
	char procdir[PATH_MAX];
	snprintf(procdir, sizeof procdir, "%s/%d/fd", PROCFS, pid);
	TRACE("opendir(%s)\n", procdir);
	DIR *d = opendir(procdir);
	struct dirent *dp;
	if (!d) {
		DEBUG("failed to open %s: %s\n", procdir, strerror(errno));
		if (errno == EACCES)
			fprintf(stderr, "failed to open %s: %s\n", procdir,
			    strerror(errno));
		return;
	}
	while ((dp = readdir(d))) {
		if (dp->d_name[0] == '.')
			continue;

		int fd = atoi(dp->d_name);
		TRACE("processing fd %d\n", fd);

		// skip if the file is not a socket
		if (!is_socket(pid, fd))
			continue;

		TRACE("%d is a socket\n", fd);

		// if we make it here, the file is a socket, so lazily Pgrab
		// the process and inspect the socket
		if (!Pr) {
			int perr = 0;
			TRACE("Pgrab(%d)\n", pid);
			Pr = Pgrab(pid, PGRAB_NOSTOP, &perr);
			if (perr || !Pr) {
				DEBUG("ps_prochandle for %d: %s\n", pid,
				    Pgrab_error(perr));
				// end this loop completely if we fail this
				goto done;
			}
		}

		show_socket(Pr, pid, fd);
	}

done:
	TRACE("closedir(%s)\n", procdir);
	closedir(d);
	if (Pr) {
		TRACE("Prelease(<%d>)\n", pid);
		Prelease(Pr, 0);
	}
}

// given a pid and fd, return 1 if the fd is a socket
// and 0 if it isn't or an error is encountered
static int is_socket(pid_t pid, int fd) {
	// stat(2) the fd
	char fname[PATH_MAX];
	struct stat sb;
	snprintf(fname, sizeof fname, "%s/%d/fd/%d", PROCFS, pid, fd);
	if (stat(fname, &sb) == -1) {
		DEBUG("failed to stat %s: %s\n", fname, strerror(errno));
		return 0;
	}

	return ((sb.st_mode & S_IFMT) == S_IFSOCK);
}

// process a fd in a pid
static void show_socket(struct ps_prochandle *Pr, pid_t pid, int fd) {
	// A buffer large enough for PATH_MAX size AF_UNIX address
	// this taken from pfiles.c
	long buf[(sizeof (short) + PATH_MAX + sizeof (long) - 1)
		/ sizeof (long)];
	struct sockaddr *sa = (struct sockaddr *)buf;
	socklen_t len = sizeof (buf);

	// get the socket type
	int type;
	int tlen = sizeof (type);
	if (pr_getsockopt(Pr, fd, SOL_SOCKET, SO_TYPE, &type, &tlen) != 0)
		return;

	// only look for TCP (STREAM) sockets
	if (type != SOCK_STREAM)
		return;

	// skip if we have a peer
	if (pr_getpeername(Pr, fd, sa, &len) == 0)
		return;

	// get the socket information
	if (pr_getsockname(Pr, fd, sa, &len) != 0)
		return;

	// only care about ipv4 for now (TODO handle ipv6)
	if (sa->sa_family != AF_INET)
		return;

	// we have a socket we actually care about!
	struct sockaddr_in *sa_in = (struct sockaddr_in *)(void *)sa;
	char *ip = inet_ntoa(sa_in->sin_addr);
	int port = ntohs(sa_in->sin_port);

	// sometimes the port can be 0... idk why but this conditional helps
	// reduce duplicates
	if (!port) {
		DEBUG("pid %d fd %d port is %d\n", pid, fd, port);
		return;
	}

	// get the process info
	const struct psinfo *pinfo = Ppsinfo(Pr);

	// not likely, but Ppsinfo could technically return NULL.
	// because we know that a socket is listening, but we don't know
	// process info, we should print something to the user
	const char *name = "<unknown>";
	const char *args = "<unknown>";
	if (pinfo) {
		name = pinfo->pr_fname;
		args = pinfo->pr_psargs;
	}

	// print what we've found
	printf("%-8d %-12s %-17s %-7d %s\n",
	    pid, name, ip, port, args);
}
