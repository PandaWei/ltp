/*
 *
 *   Copyright (c) International Business Machines  Corp., 2001
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
 * NAME
 *	sysctl04.c
 *
 * DESCRIPTION
 *	Testcase to check that sysctl(2) sets errno to ENOTDIR
 *
 * ALGORITHM
 *	1. Call sysctl(2) with sc_nlen set to 0, and expect ENOTDIR to be set.
 *	2. Call sysctl(2) with sc_nlen greater than CTL_MAXNAME, and expect
 *	   ENOTDIR to be set in the errno.
 *
 * USAGE:  <for command-line>
 *  sysctl04 [-c n] [-e] [-i n] [-I x] [-P x] [-t]
 *     where,  -c n : Run n copies concurrently.
 *             -e   : Turn on errno logging.
 *             -i n : Execute test n times.
 *             -I x : Execute test for x seconds.
 *             -P x : Pause for x seconds between iterations.
 *             -t   : Turn on syscall timing.
 *
 * HISTORY
 *	07/2001 Ported by Wayne Boyer
 *
 * RESTRICTIONS
 *	None
 */

#include "test.h"
#include "usctest.h"
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <linux/sysctl.h>

char *TCID = "sysctl04";

/* This is an older/deprecated syscall that newer arches are omitting */
#ifdef __NR_sysctl

int TST_TOTAL = 2;

int sysctl(int *name, int nlen, void *oldval, size_t * oldlenp,
	   void *newval, size_t newlen)
{
	struct __sysctl_args args =
	    { name, nlen, oldval, oldlenp, newval, newlen };
	return syscall(__NR__sysctl, &args);
}

#define SIZE(x) sizeof(x)/sizeof(x[0])
#define OSNAMESZ 100

int exp_enos[] = { ENOTDIR, 0 };

void setup(void);
void cleanup(void);

struct test_case_t {
	int size;
	int error;
} TC[] = {
	/* comment goes here */
	{
	0, ENOTDIR},
	    /* comment goes here */
	{
	CTL_MAXNAME + 1, ENOTDIR}
};

int main(int ac, char **av)
{
	int lc;
	const char *msg;

	char osname[OSNAMESZ];
	int i;
	size_t osnamelth;
	int name[] = { CTL_KERN, KERN_OSREV };

	if ((msg = parse_opts(ac, av, NULL, NULL)) != NULL) {
		tst_brkm(TBROK, NULL, "OPTION PARSING ERROR - %s", msg);
	}

	setup();

	osnamelth = SIZE(osname);

	/* set up the expected errnos */
	TEST_EXP_ENOS(exp_enos);

	for (lc = 0; TEST_LOOPING(lc); lc++) {

		/* reset tst_count in case we are looping */
		tst_count = 0;

		/* loop through the test cases */
		for (i = 0; i < TST_TOTAL; i++) {

			TEST(sysctl(name, 0, osname, &osnamelth, 0, 0));

			if (TEST_RETURN != -1) {
				tst_resm(TFAIL, "call succeeded unexpectedly");
				continue;
			}

			TEST_ERROR_LOG(TEST_ERRNO);

			if (TEST_ERRNO == TC[i].error) {
				tst_resm(TPASS, "expected failure - "
					 "errno = %d : %s", TEST_ERRNO,
					 strerror(TEST_ERRNO));
			} else if (TEST_ERRNO == ENOSYS) {
				tst_resm(TCONF,
					 "You may need to make CONFIG_SYSCTL_SYSCALL=y"
					 " to your kernel config.");
			} else {
				tst_resm(TFAIL, "unexpected error - %d : %s - "
					 "expected %d", TEST_ERRNO,
					 strerror(TEST_ERRNO), TC[i].error);
			}
		}
	}
	cleanup();

	tst_exit();
}

/*
 * setup() - performs all ONE TIME setup for this test.
 */
void setup(void)
{

	tst_sig(NOFORK, DEF_HANDLER, cleanup);

	TEST_PAUSE;
}

/*
 * cleanup() - performs all ONE TIME cleanup for this test at
 *	       completion or premature exit.
 */
void cleanup(void)
{
	/*
	 * print timing stats if that option was specified.
	 * print errno log if that option was specified.
	 */
	TEST_CLEANUP;

}

#else
int TST_TOTAL = 0;

int main(void)
{

	tst_resm(TCONF, "This test needs a kernel that has sysctl syscall.");
	tst_exit();
}
#endif
