/*
 *
 *   Copyright (c) International Business Machines  Corp., 2002
 *   Copyright (c) Cyril Hrubis chrubis@suse.cz 2009
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
 *	ftest03.c -- test file I/O with readv and writev (ported from SPIE section2/filesuite/ftest4.c, by Airong Zhang)
 *
 * CALLS
 *	lseek, readv, writev,
 *	truncate, ftruncate, fsync, sync, fstat
 *
 * ALGORITHM
 *	A bitmap is used to map pieces of a file.
 *      Loop: pick a random piece of the file
 *            if we haven't seen it before make sure it is zero,
 *            write pattern
 *            if we have seen it before make sure correct pattern.
 *
 *      This was originally written by rbk - was program tfio.c
 *	Modified by dale to integrate with test suites.
 *	Modified by G. Stevens to use readv and writev.
 *	Modofied by K. Hakim to integrate with SPIES.
 *
 * RESTRICTIONS
 *  1.  Runs a long time with default args - can take others on input
 *	line.  Use with "term mode".
 *	If run on vax the ftruncate will not be random - will always go to
 *	start of file.  NOTE: produces a very high load average!!
 *
 *  2.  The "csize" argument must be evenly divisible by MAXIOVCNT.
 *
 * CAUTION!!
 *	If a file is supplied to this program with the "-f" option
 *	it will be removed with a system("rm -rf filename") call.
 *
 */

#define _XOPEN_SOURCE 500
#include <sys/types.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <inttypes.h>
#include "test.h"
#include "usctest.h"
#include "libftest.h"

char *TCID = "ftest03";
int TST_TOTAL = 1;

#define PASSED 1
#define FAILED 0

static void setup(void);
static void runtest(void);
static void dotest(int, int, int);
static void domisc(int, int, char *);
static void term(int sig);

#define MAXCHILD	25
#define K_1		1024
#define K_2		2048
#define K_4		4096
#define	MAXIOVCNT	16

static int csize;		/* chunk size */
static int iterations;		/* # total iterations */
static int max_size;		/* max file size */
static int misc_intvl;		/* for doing misc things; 0 ==> no */
static int nchild;		/* how many children */
static int fd;			/* file descriptor used by child */
static int parent_pid;
static int pidlist[MAXCHILD];
static char test_name[2];	/* childs test directory name */

static char fuss[MAXPATHLEN];	/* directory to do this in */
static char homedir[MAXPATHLEN];	/* where we started */

static int local_flag;

int main(int ac, char *av[])
{
	int lc;
	const char *msg;

	if ((msg = parse_opts(ac, av, NULL, NULL)) != NULL)
		tst_brkm(TBROK, NULL, "OPTION PARSING ERROR - %s", msg);

	setup();

	for (lc = 0; TEST_LOOPING(lc); lc++) {

		local_flag = PASSED;

		runtest();

		if (local_flag == PASSED) {
			tst_resm(TPASS, "Test passed.");
		} else {
			tst_resm(TFAIL, "Test failed.");
		}

		tst_rmdir();
		/* ??? so we are doing only one loop here ??? */
		tst_exit();
	}

	tst_exit();
}

static void setup(void)
{
	char wdbuf[MAXPATHLEN];

	/*
	 * Make a directory to do this in; ignore error if already exists.
	 * Save starting directory.
	 */
	tst_tmpdir();

	if (getcwd(homedir, sizeof(homedir)) == NULL) {
		tst_brkm(TBROK | TERRNO, NULL, "getcwd() failed");
	}

	parent_pid = getpid();

	if (!fuss[0])
		sprintf(fuss, "%s/ftest03.%d", getcwd(wdbuf, sizeof(wdbuf)),
			getpid());

	mkdir(fuss, 0755);

	if (chdir(fuss) < 0) {
		tst_resm(TBROK, "\tCan't chdir(%s), error %d.", fuss, errno);
		tst_exit();
	}

	/*
	 * Default values for run conditions.
	 */
	iterations = 10;
	nchild = 5;
	csize = K_2;		/* should run with 1, 2, and 4 K sizes */
	max_size = K_1 * K_1;
	misc_intvl = 10;

	if (sigset(SIGTERM, term) == SIG_ERR) {
		perror("sigset failed");
		tst_resm(TBROK, " sigset failed: signo = 15");
		tst_exit();
	}
}

static void runtest(void)
{
	pid_t pid;
	int child, count, i, nwait, status;

	nwait = 0;

	for (i = 0; i < nchild; i++) {

		test_name[0] = 'a' + i;
		test_name[1] = '\0';

		fd = open(test_name, O_RDWR | O_CREAT | O_TRUNC, 0666);

		if (fd < 0) {
			tst_resm(TBROK, "\tError %d creating %s/%s.", errno,
				 fuss, test_name);
			tst_exit();
		}

		if ((child = fork()) == 0) {
			dotest(nchild, i, fd);
			tst_exit();
		}

		close(fd);

		if (child < 0) {
			tst_brkm(TBROK | TERRNO, NULL, "fork failed");
		} else {
			pidlist[i] = child;
			nwait++;
		}
	}

	/*
	 * Wait for children to finish.
	 */
	count = 0;

	while (1) {
		if ((child = wait(&status)) >= 0) {
			//tst_resm(TINFO, "\tTest{%d} exited status = 0x%x", child, status);
			if (status) {
				tst_resm(TFAIL,
					 "\tTest{%d} failed, expected 0 exit.",
					 child);
				local_flag = FAILED;
			}
			++count;
		} else {
			if (errno != EINTR)
				break;
		}
	}

	/*
	 * Should have collected all children.
	 */
	if (count != nwait) {
		tst_resm(TFAIL, "\tWrong # children waited on, count = %d",
			 count);
		local_flag = FAILED;
	}

	chdir(homedir);

	pid = fork();

	if (pid < 0) {
		tst_brkm(TBROK | TERRNO, sync, "fork failed");
		tst_exit();
	}

	if (pid == 0) {
		execl("/bin/rm", "rm", "-rf", fuss, NULL);
		exit(1);
	} else
		wait(&status);

	if (status) {
		tst_resm(TINFO, "CAUTION - ftest03, '%s' may not be removed",
			 fuss);
	}

	sync();
}

/*
 * dotest()
 *	Children execute this.
 *
 * Randomly read/mod/write chunks with known pattern and check.
 * When fill sectors, iterate.
 */

#define	NMISC	4
enum m_type { m_fsync, m_trunc, m_fstat };
char *m_str[] = {
	"fsync", "trunc", "sync", "fstat"
};

int misc_cnt[NMISC];		/* counts # of each kind of misc */
int file_max;			/* file-max size */
int nchunks;
int last_trunc = -1;
int tr_flag;
enum m_type type = m_fsync;

#define	CHUNK(i)	((i) * csize)
#define	NEXTMISC	((rand() % misc_intvl) + 5)

static void dotest(int testers, int me, int fd)
{
	char *bits, *hold_bits;
	char val;
	int chunk, whenmisc, xfr, count, collide, i;

	/* Stuff for the readv call */
	struct iovec r_iovec[MAXIOVCNT];
	int r_ioveclen;

	/* Stuff for the writev call */
	struct iovec val_iovec[MAXIOVCNT];
	struct iovec zero_iovec[MAXIOVCNT];
	int w_ioveclen;

	nchunks = max_size / csize;
	whenmisc = 0;

	if ((bits = malloc((nchunks + 7) / 8)) == 0) {
		tst_resm(TBROK, "\tmalloc failed");
		tst_exit();
	}

	if ((hold_bits = malloc((nchunks + 7) / 8)) == 0) {
		tst_resm(TBROK, "\tmalloc failed");
		tst_exit();
	}

	/*Allocate memory for the iovec buffers and init the iovec arrays */
	r_ioveclen = w_ioveclen = csize / MAXIOVCNT;

	/* Please note that the above statement implies that csize
	 * be evenly divisible by MAXIOVCNT.
	 */
	for (i = 0; i < MAXIOVCNT; i++) {
		if ((r_iovec[i].iov_base = calloc(r_ioveclen, 1)) == 0) {
			tst_brkm(TBROK, NULL, "\tmalloc failed");
			/* tst_exit(); */
		}
		r_iovec[i].iov_len = r_ioveclen;

		/* Allocate unused memory areas between all the buffers to
		 * make things more diffult for the OS.
		 */
		if (malloc((i + 1) * 8) == NULL) {
			tst_brkm(TBROK, NULL, "\tmalloc failed");
		}

		if ((val_iovec[i].iov_base = calloc(w_ioveclen, 1)) == 0) {
			tst_resm(TBROK, "\tmalloc failed");
			tst_exit();
		}

		val_iovec[i].iov_len = w_ioveclen;

		if (malloc((i + 1) * 8) == NULL) {
			tst_resm(TBROK, "\tmalloc failed");
			tst_exit();
		}

		if ((zero_iovec[i].iov_base = calloc(w_ioveclen, 1)) == 0) {
			tst_resm(TBROK, "\tmalloc failed");
			tst_exit();
		}

		zero_iovec[i].iov_len = w_ioveclen;

		if (malloc((i + 1) * 8) == NULL) {
			tst_resm(TBROK, "\tmalloc failed");
			tst_exit();
		}
	}
	/*
	 * No init sectors; allow file to be sparse.
	 */
	val = (64 / testers) * me + 1;

	/*
	 * For each iteration:
	 *      zap bits array
	 *      loop
	 *              pick random chunk, read it.
	 *              if corresponding bit off {
	 *                      verify = 0. (sparse file)
	 *                      ++count;
	 *              } else
	 *                      verify = val.
	 *              write "val" on it.
	 *              repeat unitl count = nchunks.
	 *      ++val.
	 */

	srand(getpid());

	if (misc_intvl)
		whenmisc = NEXTMISC;

	while (iterations-- > 0) {

		for (i = 0; i < NMISC; i++)
			misc_cnt[i] = 0;

		ftruncate(fd, 0);
		file_max = 0;
		memset(bits, 0, (nchunks + 7) / 8);
		memset(hold_bits, 0, (nchunks + 7) / 8);

		/* Have to fill the val and zero iov buffers in a different manner
		 */
		for (i = 0; i < MAXIOVCNT; i++) {
			memset(val_iovec[i].iov_base, val,
			       val_iovec[i].iov_len);
			memset(zero_iovec[i].iov_base, 0,
			       zero_iovec[i].iov_len);

		}

		count = 0;
		collide = 0;

		while (count < nchunks) {
			chunk = rand() % nchunks;
			/*
			 * Read it.
			 */
			if (lseek(fd, CHUNK(chunk), 0) < 0) {
				tst_resm(TFAIL,
					 "\tTest[%d]: lseek(0) fail at %x, errno = %d.",
					 me, CHUNK(chunk), errno);
				tst_exit();
			}
			if ((xfr = readv(fd, &r_iovec[0], MAXIOVCNT)) < 0) {
				tst_resm(TFAIL,
					 "\tTest[%d]: readv fail at %x, errno = %d.",
					 me, CHUNK(chunk), errno);
				tst_exit();
			}
			/*
			 * If chunk beyond EOF just write on it.
			 * Else if bit off, haven't seen it yet.
			 * Else, have.  Verify values.
			 */
			if (CHUNK(chunk) >= file_max) {
				bits[chunk / 8] |= (1 << (chunk % 8));
				++count;
			} else if ((bits[chunk / 8] & (1 << (chunk % 8))) == 0) {
				if (xfr != csize) {
					tst_resm(TFAIL,
						 "\tTest[%d]: xfr=%d != %d, zero read.",
						 me, xfr, csize);
					tst_exit();
				}
				for (i = 0; i < MAXIOVCNT; i++) {
					if (memcmp
					    (r_iovec[i].iov_base,
					     zero_iovec[i].iov_base,
					     r_iovec[i].iov_len)) {
						tst_resm(TFAIL,
							 "\tTest[%d] bad verify @ 0x%x for val %d count %d xfr %d file_max 0x%x, should be 0.",
							 me, CHUNK(chunk), val,
							 count, xfr, file_max);
						tst_resm(TINFO,
							 "\tTest[%d]: last_trunc = 0x%x.",
							 me, last_trunc);
						sync();
						ft_dumpiov(&r_iovec[i]);
						ft_dumpbits(bits,
							    (nchunks + 7) / 8);
						ft_orbits(hold_bits, bits,
							  (nchunks + 7) / 8);
						tst_resm(TINFO, "\tHold ");
						ft_dumpbits(hold_bits,
							    (nchunks + 7) / 8);
						tst_exit();
					}
				}
				bits[chunk / 8] |= (1 << (chunk % 8));
				++count;
			} else {
				if (xfr != csize) {
					tst_resm(TFAIL,
						 "\tTest[%d]: xfr=%d != %d, val read.",
						 me, xfr, csize);
					tst_exit();
				}
				++collide;
				for (i = 0; i < MAXIOVCNT; i++) {
					if (memcmp
					    (r_iovec[i].iov_base,
					     val_iovec[i].iov_base,
					     r_iovec[i].iov_len)) {
						tst_resm(TFAIL,
							 "\tTest[%d] bad verify @ 0x%x for val %d count %d xfr %d file_max 0x%x.",
							 me, CHUNK(chunk), val,
							 count, xfr, file_max);
						tst_resm(TINFO,
							 "\tTest[%d]: last_trunc = 0x%x.",
							 me, last_trunc);
						sync();
						ft_dumpiov(&r_iovec[i]);
						ft_dumpbits(bits,
							    (nchunks + 7) / 8);
						ft_orbits(hold_bits, bits,
							  (nchunks + 7) / 8);
						tst_resm(TINFO, "\tHold ");
						ft_dumpbits(hold_bits,
							    (nchunks + 7) / 8);
						tst_exit();
					}
				}
			}
			/*
			 * Writev it.
			 */
			if (lseek(fd, -xfr, 1) < 0) {
				tst_resm(TFAIL,
					 "\tTest[%d]: lseek(1) fail at %x, errno = %d.",
					 me, CHUNK(chunk), errno);
				tst_exit();
			}
			if ((xfr =
			     writev(fd, &val_iovec[0], MAXIOVCNT)) < csize) {
				if (errno == ENOSPC) {
					tst_resm(TFAIL,
						 "\tTest[%d]: no space, exiting.",
						 me);
					fsync(fd);
					tst_exit();
				}
				tst_resm(TFAIL,
					 "\tTest[%d]: writev fail at %x xfr %d, errno = %d.",
					 me, CHUNK(chunk), xfr, errno);
				tst_exit();
			}
			if (CHUNK(chunk) + csize > file_max)
				file_max = CHUNK(chunk) + csize;
			/*
			 * If hit "misc" interval, do it.
			 */
			if (misc_intvl && --whenmisc <= 0) {
				ft_orbits(hold_bits, bits, (nchunks + 7) / 8);
				domisc(me, fd, bits);
				whenmisc = NEXTMISC;
			}
			if (count + collide > 2 * nchunks)
				break;
		}

		/*
		 * End of iteration, maybe before doing all chunks.
		 */

		fsync(fd);
		++misc_cnt[m_fsync];
		//tst_resm(TINFO, "\tTest{%d} val %d done, count = %d, collide = {%d}",
		//              me, val, count, collide);
		//for (i = 0; i < NMISC; i++)
		//      tst_resm(TINFO, "\t\tTest{%d}: {%d} %s's.", me, misc_cnt[i], m_str[i]);
		++val;
	}
}

/*
 *	Inject misc syscalls into the thing.
 */
static void domisc(int me, int fd, char *bits)
{
	int chunk;
	struct stat sb;

	if (type > m_fstat)
		type = m_fsync;

	switch (type) {
	case m_fsync:
		if (fsync(fd) < 0) {
			tst_resm(TFAIL, "\tTest[%d]: fsync error %d.", me,
				 errno);
			tst_exit();
		}
		break;
	case m_trunc:
		chunk = rand() % (file_max / csize);
		file_max = CHUNK(chunk);
		last_trunc = file_max;
		if (tr_flag) {
			if (ftruncate(fd, file_max) < 0) {
				tst_resm(TFAIL,
					 "\tTest[%d]: ftruncate error %d @ 0x%x.",
					 me, errno, file_max);
				tst_exit();
			}
			tr_flag = 0;
		} else {
			if (truncate(test_name, file_max) < 0) {
				tst_resm(TFAIL,
					 "\tTest[%d]: truncate error %d @ 0x%x.",
					 me, errno, file_max);
				tst_exit();
			}
			tr_flag = 1;
		}
		for (; chunk % 8 != 0; chunk++)
			bits[chunk / 8] &= ~(1 << (chunk % 8));
		for (; chunk < nchunks; chunk += 8)
			bits[chunk / 8] = 0;
		break;
	case m_fstat:
		if (fstat(fd, &sb) < 0) {
			tst_resm(TFAIL, "\tTest[%d]: fstat() error %d.", me,
				 errno);
			tst_exit();
		}
		if (sb.st_size != file_max) {
			tst_resm(TFAIL,
				 "\tTest[%d]: fstat() mismatch; st_size=%"
				 PRIx64 ",file_max=%x.", me,
				 (int64_t) sb.st_size, file_max);
			tst_exit();
		}
		break;
	}

	++misc_cnt[type];
	++type;
}

/*
 * SIGTERM signal handler.
 */
static void term(int sig LTP_ATTRIBUTE_UNUSED)
{
	int i;

	tst_resm(TINFO, "\tterm -[%d]- got sig term.", getpid());

	/*
	 * If run by hand we like to have the parent send the signal to
	 * the child processes.  This makes life easy.
	 */
	if (parent_pid == getpid()) {
		for (i = 0; i < nchild; i++)
			if (pidlist[i])
				kill(pidlist[i], SIGTERM);
		return;
	}

	tst_resm(TINFO, "\tunlinking '%s'", test_name);

	close(fd);

	if (unlink(test_name))
		tst_resm(TBROK, "Unlink of '%s' failed, errno = %d.",
			 test_name, errno);
	else
		tst_resm(TBROK, "Unlink of '%s' successful.", test_name);

	tst_exit();
}
