/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Edward Sze-Tyan Wang.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#ifndef lint
static const char sccsid[] = "@(#)forward.c	8.1 (Berkeley) 6/6/93";
#endif

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libcasper.h>
#include <casper/cap_fileargs.h>

#include "extern.h"

static void rlines(FILE *, const char *fn, off_t, struct stat *);
static int show(file_info_t *);

static const file_info_t *last;

/*
 * forward -- display the file, from an offset, forward.
 *
 * There are eight separate cases for this -- regular and non-regular
 * files, by bytes or lines and from the beginning or end of the file.
 *
 * FBYTES	byte offset from the beginning of the file
 *	REG	seek
 *	NOREG	read, counting bytes
 *
 * FLINES	line offset from the beginning of the file
 *	REG	read, counting lines
 *	NOREG	read, counting lines
 *
 * RBYTES	byte offset from the end of the file
 *	REG	seek
 *	NOREG	cyclically read characters into a wrap-around buffer
 *
 * RLINES
 *	REG	mmap the file and step back until reach the correct offset.
 *	NOREG	cyclically read lines into a wrap-around array of buffers
 */
void
forward(FILE *fp, const char *fn, enum STYLE style, off_t off, struct stat *sbp)
{
	int ch;

	switch(style) {
	case FBYTES:
		if (off == 0)
			break;
		if (S_ISREG(sbp->st_mode) && sbp->st_size > 0) {
			if (sbp->st_size < off)
				off = sbp->st_size;
			if (fseeko(fp, off, SEEK_SET) == -1) {
				ierr(fn);
				return;
			}
		} else while (off--)
			if ((ch = getc(fp)) == EOF) {
				if (ferror(fp)) {
					ierr(fn);
					return;
				}
				break;
			}
		break;
	case FLINES:
		if (off == 0)
			break;
		for (;;) {
			if ((ch = getc(fp)) == EOF) {
				if (ferror(fp)) {
					ierr(fn);
					return;
				}
				break;
			}
			if (ch == '\n' && !--off)
				break;
		}
		break;
	case RBYTES:
		if (S_ISREG(sbp->st_mode) && sbp->st_size > 0) {
			if (sbp->st_size >= off &&
			    fseeko(fp, -off, SEEK_END) == -1) {
				ierr(fn);
				return;
			}
		} else if (off == 0) {
			while (getc(fp) != EOF);
			if (ferror(fp)) {
				ierr(fn);
				return;
			}
		} else
			if (bytes(fp, fn, off))
				return;
		break;
	case RLINES:
		if (S_ISREG(sbp->st_mode) && sbp->st_size > 0)
			if (!off) {
				if (fseeko(fp, (off_t)0, SEEK_END) == -1) {
					ierr(fn);
					return;
				}
			} else
				rlines(fp, fn, off, sbp);
		else if (off == 0) {
			while (getc(fp) != EOF);
			if (ferror(fp)) {
				ierr(fn);
				return;
			}
		} else
			if (lines(fp, fn, off))
				return;
		break;
	default:
		break;
	}

	while ((ch = getc(fp)) != EOF)
		if (putchar(ch) == EOF)
			oerr();
	if (ferror(fp)) {
		ierr(fn);
		return;
	}
	(void)fflush(stdout);
}

/*
 * rlines -- display the last offset lines of the file.
 */
static void
rlines(FILE *fp, const char *fn, off_t off, struct stat *sbp)
{
	struct mapinfo map;
	off_t curoff, size;
	int i;

	if (!(size = sbp->st_size))
		return;
	map.start = NULL;
	map.fd = fileno(fp);
	map.mapoff = map.maxoff = size;

	/*
	 * Last char is special, ignore whether newline or not. Note that
	 * size == 0 is dealt with above, and size == 1 sets curoff to -1.
	 */
	curoff = size - 2;
	while (curoff >= 0) {
		if (curoff < map.mapoff && maparound(&map, curoff) != 0) {
			ierr(fn);
			return;
		}
		for (i = curoff - map.mapoff; i >= 0; i--)
			if (map.start[i] == '\n' && --off == 0)
				break;
		/* `i' is either the map offset of a '\n', or -1. */
		curoff = map.mapoff + i;
		if (i >= 0)
			break;
	}
	curoff++;
	if (mapprint(&map, curoff, size - curoff) != 0) {
		ierr(fn);
		exit(1);
	}

	/* Set the file pointer to reflect the length displayed. */
	if (fseeko(fp, sbp->st_size, SEEK_SET) == -1) {
		ierr(fn);
		return;
	}
	if (map.start != NULL && munmap(map.start, map.maplen)) {
		ierr(fn);
		return;
	}
}

static int
show(file_info_t *file)
{
	int ch;

	while ((ch = getc(file->fp)) != EOF) {
		if (last != file) {
			if (vflag || (qflag == 0 && no_files > 1))
				printfn(file->file_name, 1);
			last = file;
		}
		if (putchar(ch) == EOF)
			oerr();
	}
	(void)fflush(stdout);
	if (ferror(file->fp)) {
		fclose(file->fp);
		file->fp = NULL;
		ierr(file->file_name);
		return 0;
	}
	clearerr(file->fp);
	return 1;
}

/*
 * follow -- display the file, from an offset, forward.
 *
 */
void
follow(file_info_t *files, enum STYLE style, off_t off)
{
	int active, i;
	struct stat sb2;
	file_info_t *file;
	FILE *ftmp;

	/* Position each of the files */
	active = 0;
	for (i = 0, file = files; i < no_files; i++, file++) {
		if (!file->fp)
			continue;
		active = 1;
		if (vflag || (qflag == 0 && no_files > 1))
			printfn(file->file_name, 1);
		forward(file->fp, file->file_name, style, off, &file->st);
	}
	if (!Fflag && !active)
		return;

	last = --file;

	for (;;) {
		if (Fflag) {
			for (i = 0, file = files; i < no_files; i++, file++) {
				if (!file->fp) {
					file->fp =
					    fileargs_fopen(fa, file->file_name,
					    "r");
					if (file->fp != NULL &&
					    fstat(fileno(file->fp), &file->st)
					    == -1) {
						fclose(file->fp);
						file->fp = NULL;
					}
					continue;
				}
				if (fileno(file->fp) == STDIN_FILENO)
					continue;
				ftmp = fileargs_fopen(fa, file->file_name, "r");
				if (ftmp == NULL ||
				    fstat(fileno(ftmp), &sb2) == -1) {
					if (errno != ENOENT)
						ierr(file->file_name);
					show(file);
					if (file->fp != NULL) {
						fclose(file->fp);
						file->fp = NULL;
					}
					if (ftmp != NULL) {
						fclose(ftmp);
					}
					continue;
				}

				if (sb2.st_ino != file->st.st_ino ||
				    sb2.st_dev != file->st.st_dev ||
				    sb2.st_nlink == 0) {
					show(file);
					if (file->fp != NULL)
						fclose(file->fp);
					file->fp = ftmp;
					memcpy(&file->st, &sb2,
					    sizeof(struct stat));
				} else {
					fclose(ftmp);
				}
			}
		}

		for (i = 0, file = files; i < no_files; i++, file++)
			if (file->fp) show(file);

		(void) usleep(250000);
	}
}
