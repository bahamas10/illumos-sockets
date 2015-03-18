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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PROCFS "/proc"

// CLI options
struct {
	int debug;
	int header;
} opts;

static int show_file(struct ps_prochandle *Pr, pid_t pid, int fd);
static void dopid(pid_t pid);

// function like printf that is turned on with the debug CLI switch
void debug(const char *fmt, ...) {
	if (!opts.debug)
		return;

	// printf like normal
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

// print the usage message
void usage(FILE *stream) {
	fprintf(stream, "usage: opensockets [-h] [-v] [-H] [[pid] ...]\n");
	fprintf(stream, "\n");
	fprintf(stream, "print all ports in use on the current system\n");
	fprintf(stream, "\n");
	fprintf(stream, "options\n");
	fprintf(stream, "  -d       turn on debug output\n");
	fprintf(stream, "  -h       print this message and exit\n");
	fprintf(stream, "  -H       don't print header\n");
}

int main(int argc, char **argv) {
	opts.header = 1;
	opts.debug = 0;

	int c;
	while ((c = getopt(argc, argv, "dhH")) != -1) {
		switch (c) {
			case 'd':
				opts.debug = 1;
				break;
			case 'h':
				usage(stdout);
				return 0;
			case 'H':
				opts.header = 0;
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
	debug("pid = %d\n", me);

	int i;
	// loop arguments as pids
	if (optind < argc) {
		for (i = optind; i < argc; i++) {
			pid_t pid = atoi(argv[i]);
			dopid(pid);
		}
		return 0;
	}

	// loop pids in /proc
	debug("opening %s\n", PROCFS);
	DIR *d = opendir(PROCFS);
	struct dirent *dp;
	if (!d) {
		debug("failed to open %s: %s\n", PROCFS, strerror(errno));
		return 1;
	}
	while ((dp = readdir(d))) {
		if (dp->d_name[0] == '.')
			continue;
		pid_t pid = atoi(dp->d_name);
		if (pid == me)
			continue;
		dopid(pid);
	}
	closedir(d);

	return 0;
}

// process a pid
static void dopid(pid_t pid) {
	debug("processing pid %d\n", pid);

	int perr = 0;
	// grab the process
	struct ps_prochandle *Pr = Pgrab(pid, PGRAB_NOSTOP, &perr);
	if (perr || !Pr) {
		debug("ps_prochandle for %d: %s\n", pid, Pgrab_error(perr));
		if (perr == G_PERM)
			fprintf(stderr, "pid %d: %s\n", pid, Pgrab_error(perr));
		return;
	}

	// loop fds in /proc/<pid>/fd
	char procdir[1024];
	snprintf(procdir, sizeof procdir, "%s/%d/fd", PROCFS, pid);

	DIR *d = opendir(procdir);
	struct dirent *dp;
	if (!d) {
		debug("failed to open %s: %s\n", procdir, strerror(errno));
		goto done;
	}
	while ((dp = readdir(d))) {
		if (dp->d_name[0] == '.')
			continue;
		int fd = atoi(dp->d_name);
		show_file(Pr, pid, fd);
	}
	closedir(d);

done:
	Prelease(Pr, 0);
}

// process a fd in a pid
static int show_file(struct ps_prochandle *Pr, pid_t pid, int fd) {
	// stat(2) the fd
	char fname[1024];
	struct stat sb;
	snprintf(fname, sizeof fname, "%s/%d/fd/%d", PROCFS, pid, fd);
	if (stat(fname, &sb) == -1) {
		debug("failed to stat %s: %s\n", fname, strerror(errno));
		return 0;
	}

	// only look for sockets
	if ((sb.st_mode & S_IFMT) != S_IFSOCK)
		return 0;

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
		return 0;

	// only look for TCP (STREAM) sockets
	if (type != SOCK_STREAM)
		return 0;

	// skip if we have a peer
	if (pr_getpeername(Pr, fd, sa, &len) == 0)
		return 0;

	// call getsockname on the fd inside the process
	if (pr_getsockname(Pr, fd, sa, &len) != 0)
		return 0;

	// only care about ipv4 for now
	if (sa->sa_family != AF_INET)
		return 0;

	// we have a socket we actually care about!
	struct sockaddr_in *sa_in = (struct sockaddr_in *)(void *)sa;
	char *ip = inet_ntoa(sa_in->sin_addr);
	int port = ntohs(sa_in->sin_port);

	// sometimes the port can be 0... idk why but this helps reduce dups
	if (!port) {
		debug("pid %d fd %d port is %d\n", pid, fd, port);
		return 0;
	}

	// get the process info
	const struct psinfo *pinfo = Ppsinfo(Pr);

	// print what we found
	printf("%-8d %-12s %-17s %-7d %s\n",
	    pid, pinfo->pr_fname, ip, port, pinfo->pr_psargs);

	return 0;
}
