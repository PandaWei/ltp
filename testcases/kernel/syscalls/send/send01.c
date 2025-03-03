/*
 *
 *   Copyright (c) International Business Machines  Corp., 2001
 *   Copyright (c) Cyril Hrubis <chrubis@suse.cz> 2012
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * Test Name: send01
 *
 * Test Description:
 *  Verify that send() returns the proper errno for various failure cases
 *
 * HISTORY
 *	07/2001 Ported by Wayne Boyer
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/un.h>

#include <netinet/in.h>

#include "test.h"
#include "usctest.h"

char *TCID = "send01";
int testno;

static char buf[1024], bigbuf[128 * 1024];
static int s;
static struct sockaddr_in sin1;
static int sfd;			/* shared between do_child and start_server */

struct test_case_t {		/* test case structure */
	int domain;		/* PF_INET, PF_UNIX, ... */
	int type;		/* SOCK_STREAM, SOCK_DGRAM ... */
	int proto;		/* protocol number (usually 0 = default) */
	void *buf;		/* send data buffer */
	int buflen;		/* send's 3rd argument */
	unsigned flags;		/* send's 4th argument */
	int retval;
	int experrno;
	void (*setup) (void);
	void (*cleanup) (void);
	char *desc;
};

static void cleanup(void);
static void do_child(void);
static void setup(void);
static void setup0(void);
static void setup1(void);
static void setup2(void);
static void cleanup0(void);
static void cleanup1(void);

static struct test_case_t tdat[] = {
	{.domain = PF_INET,
	 .type = SOCK_STREAM,
	 .proto = 0,
	 .buf = buf,
	 .buflen = sizeof(buf),
	 .flags = 0,
	 .retval = -1,
	 .experrno = EBADF,
	 .setup = setup0,
	 .cleanup = cleanup0,
	 .desc = "bad file descriptor"}
	,
	{.domain = 0,
	 .type = 0,
	 .proto = 0,
	 .buf = buf,
	 .buflen = sizeof(buf),
	 .flags = 0,
	 .retval = -1,
	 .experrno = ENOTSOCK,
	 .setup = setup0,
	 .cleanup = cleanup0,
	 .desc = "invalid socket"}
	,
#ifndef UCLINUX
	/* Skip since uClinux does not implement memory protection */
	{.domain = PF_INET,
	 .type = SOCK_STREAM,
	 .proto = 0,
	 .buf = (void *)-1,
	 .buflen = sizeof(buf),
	 .flags = 0,
	 .retval = -1,
	 .experrno = EFAULT,
	 .setup = setup1,
	 .cleanup = cleanup1,
	 .desc = "invalid send buffer"}
	,
#endif
	{.domain = PF_INET,
	 .type = SOCK_DGRAM,
	 .proto = 0,
	 .buf = bigbuf,
	 .buflen = sizeof(bigbuf),
	 .flags = 0,
	 .retval = -1,
	 .experrno = EMSGSIZE,
	 .setup = setup1,
	 .cleanup = cleanup1,
	 .desc = "UDP message too big"}
	,
	{.domain = PF_INET,
	 .type = SOCK_STREAM,
	 .proto = 0,
	 .buf = buf,
	 .buflen = sizeof(buf),
	 .flags = 0,
	 .retval = -1,
	 .experrno = EPIPE,
	 .setup = setup2,
	 .cleanup = cleanup1,
	 .desc = "local endpoint shutdown"}
	,
#ifndef UCLINUX
	/* Skip since uClinux does not implement memory protection */
	{.domain = PF_INET,
	 .type = SOCK_DGRAM,
	 .proto = 0,
	 .buf = buf,
	 .buflen = sizeof(buf),
	 .flags = MSG_OOB,
	 .retval = -1,
	 .experrno = EOPNOTSUPP,
	 .setup = setup1,
	 .cleanup = cleanup1,
	 .desc = "invalid flags set"}
#endif
};

int TST_TOTAL = sizeof(tdat) / sizeof(tdat[0]);

int exp_enos[] = { EBADF, ENOTSOCK, EFAULT, EMSGSIZE, EPIPE, EINVAL, 0 };

#ifdef UCLINUX
static char *argv0;
#endif

static pid_t start_server(struct sockaddr_in *sin0)
{
	pid_t pid;
	socklen_t slen = sizeof(*sin0);

	sin0->sin_family = AF_INET;
	sin0->sin_port = 0; /* pick random free port */
	sin0->sin_addr.s_addr = INADDR_ANY;

	sfd = socket(PF_INET, SOCK_STREAM, 0);
	if (sfd < 0) {
		tst_brkm(TBROK | TERRNO, cleanup, "server socket failed");
		return -1;
	}
	if (bind(sfd, (struct sockaddr *)sin0, sizeof(*sin0)) < 0) {
		tst_brkm(TBROK | TERRNO, cleanup, "server bind failed");
		return -1;
	}
	if (listen(sfd, 10) < 0) {
		tst_brkm(TBROK | TERRNO, cleanup, "server listen failed");
		return -1;
	}
	if (getsockname(sfd, (struct sockaddr *)sin0, &slen) == -1)
		tst_brkm(TBROK | TERRNO, cleanup, "getsockname failed");

	switch ((pid = FORK_OR_VFORK())) {
	case 0:
#ifdef UCLINUX
		if (self_exec(argv0, "d", sfd) < 0)
			tst_brkm(TBROK | TERRNO, cleanup,
				 "server self_exec failed");
#else
		do_child();
#endif
		break;
	case -1:
		tst_brkm(TBROK | TERRNO, cleanup, "server fork failed");
	default:
		close(sfd);
		return pid;
	}

	exit(1);
}

static void do_child(void)
{
	fd_set afds, rfds;
	int nfds, cc, fd;
	struct sockaddr_in fsin;

	FD_ZERO(&afds);
	FD_SET(sfd, &afds);

	nfds = getdtablesize();

	/* accept connections until killed */
	while (1) {
		socklen_t fromlen;

		memcpy(&rfds, &afds, sizeof(rfds));

		if (select(nfds, &rfds, NULL, NULL, NULL) < 0)
			if (errno != EINTR)
				exit(1);
		if (FD_ISSET(sfd, &rfds)) {
			int newfd;

			fromlen = sizeof(fsin);
			newfd = accept(sfd, (struct sockaddr *)&fsin, &fromlen);
			if (newfd >= 0)
				FD_SET(newfd, &afds);
		}
		for (fd = 0; fd < nfds; ++fd) {
			if (fd != sfd && FD_ISSET(fd, &rfds)) {
				cc = read(fd, buf, sizeof(buf));
				if (cc == 0 || (cc < 0 && errno != EINTR)) {
					close(fd);
					FD_CLR(fd, &afds);
				}
			}
		}
	}
}

int main(int ac, char *av[])
{
	int lc;
	const char *msg;

	if ((msg = parse_opts(ac, av, NULL, NULL)) != NULL)
		tst_brkm(TBROK, NULL, "OPTION PARSING ERROR - %s", msg);

#ifdef UCLINUX
	argv0 = av[0];
	maybe_run_child(&do_child, "d", &sfd);
#endif

	setup();

	TEST_EXP_ENOS(exp_enos);

	for (lc = 0; TEST_LOOPING(lc); ++lc) {

		tst_count = 0;

		for (testno = 0; testno < TST_TOTAL; ++testno) {
			tdat[testno].setup();

			TEST(send(s, tdat[testno].buf, tdat[testno].buflen,
				  tdat[testno].flags));

			if (TEST_RETURN != -1) {
				tst_resm(TFAIL, "call succeeded unexpectedly");
				continue;
			}

			TEST_ERROR_LOG(TEST_ERRNO);

			if (TEST_ERRNO != tdat[testno].experrno) {
				tst_resm(TFAIL, "%s ; returned"
					 " %ld (expected %d), errno %d (expected"
					 " %d)", tdat[testno].desc,
					 TEST_RETURN, tdat[testno].retval,
					 TEST_ERRNO, tdat[testno].experrno);
			} else {
				tst_resm(TPASS, "%s successful",
					 tdat[testno].desc);
			}
			tdat[testno].cleanup();
		}
	}
	cleanup();

	tst_exit();
}

static pid_t server_pid;

static void setup(void)
{
	TEST_PAUSE;

	server_pid = start_server(&sin1);

	signal(SIGPIPE, SIG_IGN);
}

static void cleanup(void)
{
	kill(server_pid, SIGKILL);
	TEST_CLEANUP;

}

static void setup0(void)
{
	if (tdat[testno].experrno == EBADF)
		s = 400;	/* anything not an open file */
	else if ((s = open("/dev/null", O_WRONLY)) == -1)
		tst_brkm(TBROK | TERRNO, cleanup, "open(/dev/null) failed");
}

static void cleanup0(void)
{
	s = -1;
}

static void setup1(void)
{
	s = socket(tdat[testno].domain, tdat[testno].type, tdat[testno].proto);
	if (s < 0)
		tst_brkm(TBROK | TERRNO, cleanup, "socket setup failed");
	if (connect(s, (const struct sockaddr *)&sin1, sizeof(sin1)) < 0)
		tst_brkm(TBROK | TERRNO, cleanup, "connect failed");
}

static void cleanup1(void)
{
	close(s);
	s = -1;
}

static void setup2(void)
{
	setup1();

	if (shutdown(s, 1) < 0)
		tst_brkm(TBROK | TERRNO, cleanup, "socket setup failed connect "
			 "test %d", testno);
}
