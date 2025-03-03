/*
 * Copyright (c) 2014 Fujitsu Ltd.
 * Author: Zeng Linggang <zenglg.jy@cn.fujitsu.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
/*
 * Test Description:
 *  Verify that,
 *   1. lchown() fails with -1 return value and sets errno to ELOOP
 *      if too many symbolic links were encountered in resolving path.
 *   2. lchown() fails with -1 return value and sets errno to EROFS
 *      if the file is on a read-only file system.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/mount.h>

#include "test.h"
#include "usctest.h"
#include "safe_macros.h"

#define DIR_MODE	(S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP| \
			 S_IXGRP|S_IROTH|S_IXOTH)
#define TEST_EROFS	"mntpoint"

static char test_eloop[PATH_MAX] = ".";
static const char *device;
static int mount_flag;

static struct test_case_t {
	char *pathname;
	int exp_errno;
} test_cases[] = {
	{test_eloop, ELOOP},
	{TEST_EROFS, EROFS},
};

char *TCID = "lchown03";
int TST_TOTAL = ARRAY_SIZE(test_cases);
static int exp_enos[] = { ELOOP, EROFS, 0 };

static void setup(void);
static void lchown_verify(const struct test_case_t *);
static void cleanup(void);

int main(int argc, char *argv[])
{
	int lc;
	int i;
	const char *msg;

	msg = parse_opts(argc, argv, NULL, NULL);
	if (msg != NULL)
		tst_brkm(TBROK, NULL, "OPTION PARSING ERROR - %s", msg);

	setup();

	TEST_EXP_ENOS(exp_enos);

	for (lc = 0; TEST_LOOPING(lc); lc++) {
		tst_count = 0;
		for (i = 0; i < TST_TOTAL; i++)
			lchown_verify(&test_cases[i]);
	}

	cleanup();
	tst_exit();
}

static void setup(void)
{
	int i;
	const char *fs_type;

	tst_require_root(NULL);

	tst_sig(NOFORK, DEF_HANDLER, cleanup);

	TEST_PAUSE;

	tst_tmpdir();

	fs_type = tst_dev_fs_type();
	device = tst_acquire_device(cleanup);

	if (!device)
		tst_brkm(TCONF, cleanup, "Failed to acquire device");

	SAFE_MKDIR(cleanup, "test_eloop", DIR_MODE);
	SAFE_SYMLINK(cleanup, "../test_eloop", "test_eloop/test_eloop");
	for (i = 0; i < 43; i++)
		strcat(test_eloop, "/test_eloop");

	tst_mkfs(cleanup, device, fs_type, NULL);
	SAFE_MKDIR(cleanup, TEST_EROFS, DIR_MODE);
	if (mount(device, TEST_EROFS, fs_type, MS_RDONLY, NULL) < 0) {
		tst_brkm(TBROK | TERRNO, cleanup,
			 "mount device:%s failed", device);
	}
	mount_flag = 1;
}

static void lchown_verify(const struct test_case_t *test)
{
	TEST(lchown(test->pathname, geteuid(), getegid()));

	if (TEST_RETURN != -1) {
		tst_resm(TFAIL, "lchown() returned %ld, expected -1, errno=%d",
			 TEST_RETURN, test->exp_errno);
		return;
	}

	TEST_ERROR_LOG(TEST_ERRNO);

	if (TEST_ERRNO == test->exp_errno) {
		tst_resm(TPASS | TTERRNO, "lchown() failed as expected");
	} else {
		tst_resm(TFAIL | TTERRNO,
			 "lchown() failed unexpectedly; expected: %d - %s",
			 test->exp_errno,
			 strerror(test->exp_errno));
	}
}

static void cleanup(void)
{
	TEST_CLEANUP;

	if (mount_flag && umount(TEST_EROFS) < 0)
		tst_resm(TWARN | TERRNO, "umount device:%s failed", device);

	if (device)
		tst_release_device(NULL, device);

	tst_rmdir();
}
