/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2014 Gary Mills
 * Copyright 2011, Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 1994 Powerdog Industries.  All rights reserved.
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 *
 * Portions of this software were developed by David Chisnall
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY POWERDOG INDUSTRIES ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE POWERDOG INDUSTRIES BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Powerdog Industries.
 */

#include <sys/cdefs.h>
#ifndef lint
#ifndef NOID
static char copyright[] __unused =
"@(#) Copyright (c) 1994 Powerdog Industries.  All rights reserved.";
static char sccsid[] __unused = "@(#)strptime.c	0.1 (Powerdog) 94/03/27";
#endif /* !defined NOID */
#endif /* not lint */
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <langinfo.h>

#define DAYSPERWEEK 7
#define MONSPERYEAR 12
#define	TM_SUNDAY 0
#define TM_MONDAY 1
#define TM_YEAR_BASE 1900

#define isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))

static char * _strptime(const char *, const char *, struct tm *, int *);

#define	asizeof(a)	(sizeof(a) / sizeof((a)[0]))

#define	FLAG_NONE	(1 << 0)
#define	FLAG_YEAR	(1 << 1)
#define	FLAG_MONTH	(1 << 2)
#define	FLAG_YDAY	(1 << 3)
#define	FLAG_MDAY	(1 << 4)
#define	FLAG_WDAY	(1 << 5)

/*
 * Gauss's algorithm for the day of the week of the first day of any year
 * in the Gregorian calendar.
 */
static int
first_wday_of(int year)
{
	return ((1 +
	    5 * ((year - 1) % 4) +
	    4 * ((year - 1) % 100) +
	    6 * ((year - 1) % 400)) % 7);
}

static char *
_strptime(const char *buf, const char *fmt, struct tm *tm, int *GMTp)
{
	char	c;
	const char *ptr, *ex;
	int	day_offset = -1, wday_offset;
	int week_offset;
	int	i, len;
	int flags;
	int Ealternative, Oalternative;
	int century, year;
	static int start_of_month[2][13] = {
		{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
		{0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366}
	};

	flags = FLAG_NONE;
	century = -1;
	year = -1;

	ptr = fmt;
	while (*ptr != 0) {
		c = *ptr++;

		if (c != '%') {
			if (isspace((unsigned char)c))
				while (*buf != 0 && 
				       isspace((unsigned char)*buf))
					buf++;
			else if (c != *buf++)
				return (NULL);
			continue;
		}

		Ealternative = 0;
		Oalternative = 0;
label:
		c = *ptr++;
		switch (c) {
		case '%':
			if (*buf++ != '%')
				return (NULL);
			break;

		case '+':
#ifdef _DATE_FMT
			buf = _strptime(buf, nl_langinfo(_DATE_FMT), tm, GMTp);
#else
			buf = _strptime(buf, "%a %b %e %H:%M:%S %Z %Y", tm, GMTp);
#endif
			if (buf == NULL)
				return (NULL);
			flags |= FLAG_WDAY | FLAG_MONTH | FLAG_MDAY | FLAG_YEAR;
			break;

		case 'C':
			if (!isdigit((unsigned char)*buf))
				return (NULL);

			/* XXX This will break for 3-digit centuries. */
			len = 2;
			for (i = 0; len && *buf != 0 &&
			     isdigit((unsigned char)*buf); buf++) {
				i *= 10;
				i += *buf - '0';
				len--;
			}

			century = i;
			flags |= FLAG_YEAR;

			break;

		case 'c':
			buf = _strptime(buf, nl_langinfo(D_T_FMT), tm, GMTp);
			if (buf == NULL)
				return (NULL);
			flags |= FLAG_WDAY | FLAG_MONTH | FLAG_MDAY | FLAG_YEAR;
			break;

		case 'D':
			buf = _strptime(buf, "%m/%d/%y", tm, GMTp);
			if (buf == NULL)
				return (NULL);
			flags |= FLAG_MONTH | FLAG_MDAY | FLAG_YEAR;
			break;

		case 'E':
			if (Ealternative || Oalternative)
				break;
			Ealternative++;
			goto label;

		case 'O':
			if (Ealternative || Oalternative)
				break;
			Oalternative++;
			goto label;

		case 'F':
			buf = _strptime(buf, "%Y-%m-%d", tm, GMTp);
			if (buf == NULL)
				return (NULL);
			flags |= FLAG_MONTH | FLAG_MDAY | FLAG_YEAR;
			break;

		case 'R':
			buf = _strptime(buf, "%H:%M", tm, GMTp);
			if (buf == NULL)
				return (NULL);
			break;

		case 'r':
			buf = _strptime(buf, nl_langinfo(T_FMT_AMPM), tm, GMTp);
			if (buf == NULL)
				return (NULL);
			break;

		case 'T':
			buf = _strptime(buf, "%H:%M:%S", tm, GMTp);
			if (buf == NULL)
				return (NULL);
			break;

		case 'X':
			buf = _strptime(buf, nl_langinfo(T_FMT), tm, GMTp);
			if (buf == NULL)
				return (NULL);
			break;

		case 'x':
			buf = _strptime(buf, nl_langinfo(D_FMT), tm, GMTp);
			if (buf == NULL)
				return (NULL);
			flags |= FLAG_MONTH | FLAG_MDAY | FLAG_YEAR;
			break;

		case 'j':
			if (!isdigit((unsigned char)*buf))
				return (NULL);

			len = 3;
			for (i = 0; len && *buf != 0 &&
			     isdigit((unsigned char)*buf); buf++){
				i *= 10;
				i += *buf - '0';
				len--;
			}
			if (i < 1 || i > 366)
				return (NULL);

			tm->tm_yday = i - 1;
			flags |= FLAG_YDAY;

			break;

		case 'M':
		case 'S':
			if (*buf == 0 ||
				isspace((unsigned char)*buf))
				break;

			if (!isdigit((unsigned char)*buf))
				return (NULL);

			len = 2;
			for (i = 0; len && *buf != 0 &&
				isdigit((unsigned char)*buf); buf++){
				i *= 10;
				i += *buf - '0';
				len--;
			}

			if (c == 'M') {
				if (i > 59)
					return (NULL);
				tm->tm_min = i;
			} else {
				if (i > 60)
					return (NULL);
				tm->tm_sec = i;
			}

			break;

		case 'H':
		case 'I':
		case 'k':
		case 'l':
			/*
			 * %k and %l specifiers are documented as being
			 * blank-padded.  However, there is no harm in
			 * allowing zero-padding.
			 *
			 * XXX %k and %l specifiers may gobble one too many
			 * digits if used incorrectly.
			 */

			len = 2;
			if ((c == 'k' || c == 'l') &&
			    isblank((unsigned char)*buf)) {
				buf++;
				len = 1;
			}

			if (!isdigit((unsigned char)*buf))
				return (NULL);

			for (i = 0; len && *buf != 0 &&
			     isdigit((unsigned char)*buf); buf++) {
				i *= 10;
				i += *buf - '0';
				len--;
			}
			if (c == 'H' || c == 'k') {
				if (i > 23)
					return (NULL);
			} else if (i == 0 || i > 12)
				return (NULL);

			tm->tm_hour = i;

			break;

		case 'p':
			/*
			 * XXX This is bogus if parsed before hour-related
			 * specifiers.
			 */
			if (tm->tm_hour > 12)
				return (NULL);

			ex = nl_langinfo(AM_STR);
			len = strlen(ex);
			if (strncasecmp(buf, ex, len) == 0) {
				if (tm->tm_hour == 12)
					tm->tm_hour = 0;
				buf += len;
				break;
			}

			ex = nl_langinfo(PM_STR);
			len = strlen(ex);
			if (strncasecmp(buf, ex, len) == 0) {
				if (tm->tm_hour != 12)
					tm->tm_hour += 12;
				buf += len;
				break;
			}

			return (NULL);

		case 'A':
		case 'a':
			for (i = 0; i < DAYSPERWEEK; i++) {
				ex = nl_langinfo(DAY_1 + i);
				len = strlen(ex);
				if (strncasecmp(buf, ex, len) == 0)
					break;
				ex = nl_langinfo(ABDAY_1 + i);
				len = strlen(ex);
				if (strncasecmp(buf, ex, len) == 0)
					break;
			}
			if (i == DAYSPERWEEK)
				return (NULL);

			buf += len;
			tm->tm_wday = i;
			flags |= FLAG_WDAY;
			break;

		case 'U':
		case 'W':
			/*
			 * XXX This is bogus, as we can not assume any valid
			 * information present in the tm structure at this
			 * point to calculate a real value, so just check the
			 * range for now.
			 */
			if (!isdigit((unsigned char)*buf))
				return (NULL);

			len = 2;
			for (i = 0; len && *buf != 0 &&
			     isdigit((unsigned char)*buf); buf++) {
				i *= 10;
				i += *buf - '0';
				len--;
			}
			if (i > 53)
				return (NULL);

			if (c == 'U')
				day_offset = TM_SUNDAY;
			else
				day_offset = TM_MONDAY;


			week_offset = i;

			break;

		case 'u':
		case 'w':
			if (!isdigit((unsigned char)*buf))
				return (NULL);

			i = *buf++ - '0';
			if (i < 0 || i > 7 || (c == 'u' && i < 1) ||
			    (c == 'w' && i > 6))
				return (NULL);

			tm->tm_wday = i % 7;
			flags |= FLAG_WDAY;

			break;

		case 'e':
			/*
			 * With %e format, our strftime(3) adds a blank space
			 * before single digits.
			 */
			if (*buf != 0 &&
			    isspace((unsigned char)*buf))
			       buf++;
			/* FALLTHROUGH */
		case 'd':
			/*
			 * The %e specifier was once explicitly documented as
			 * not being zero-padded but was later changed to
			 * equivalent to %d.  There is no harm in allowing
			 * such padding.
			 *
			 * XXX The %e specifier may gobble one too many
			 * digits if used incorrectly.
			 */
			if (!isdigit((unsigned char)*buf))
				return (NULL);

			len = 2;
			for (i = 0; len && *buf != 0 &&
			     isdigit((unsigned char)*buf); buf++) {
				i *= 10;
				i += *buf - '0';
				len--;
			}
			if (i == 0 || i > 31)
				return (NULL);

			tm->tm_mday = i;
			flags |= FLAG_MDAY;

			break;

		case 'B':
		case 'b':
		case 'h':
			for (i = 0; i < MONSPERYEAR; i++) {
				if (Oalternative) {
					if (c == 'B') {
						ex = nl_langinfo(MON_1 + i);
						len = strlen(ex);
						if (strncasecmp(buf, ex, len) == 0)
							break;
					}
				} else {
					ex = nl_langinfo(MON_1 + i);
					len = strlen(ex);
					if (strncasecmp(buf, ex, len) == 0)
						break;
				}
			}
			/*
			 * Try the abbreviated month name if the full name
			 * wasn't found and Oalternative was not requested.
			 */
			if (i == MONSPERYEAR && !Oalternative) {
				for (i = 0; i < MONSPERYEAR; i++) {
					ex = nl_langinfo(ABMON_1 + i);
					len = strlen(ex);
					if (strncasecmp(buf, ex, len) == 0)
						break;
				}
			}
			if (i == MONSPERYEAR)
				return (NULL);

			tm->tm_mon = i;
			buf += len;
			flags |= FLAG_MONTH;

			break;

		case 'm':
			if (!isdigit((unsigned char)*buf))
				return (NULL);

			len = 2;
			for (i = 0; len && *buf != 0 &&
			     isdigit((unsigned char)*buf); buf++) {
				i *= 10;
				i += *buf - '0';
				len--;
			}
			if (i < 1 || i > 12)
				return (NULL);

			tm->tm_mon = i - 1;
			flags |= FLAG_MONTH;

			break;

		case 's':
			{
			char *cp;
			int sverrno;
			long n;
			time_t t;

			sverrno = errno;
			errno = 0;
			n = strtol(buf, &cp, 10);
			if (errno == ERANGE || (long)(t = n) != n) {
				errno = sverrno;
				return (NULL);
			}
			errno = sverrno;
			buf = cp;
			if (gmtime_r(&t, tm) == NULL)
				return (NULL);
			*GMTp = 1;
			flags |= FLAG_YDAY | FLAG_WDAY | FLAG_MONTH |
			    FLAG_MDAY | FLAG_YEAR;
			}
			break;

		case 'Y':
		case 'y':
			if (*buf == 0 ||
			    isspace((unsigned char)*buf))
				break;

			if (!isdigit((unsigned char)*buf))
				return (NULL);

			len = (c == 'Y') ? 4 : 2;
			for (i = 0; len && *buf != 0 &&
			     isdigit((unsigned char)*buf); buf++) {
				i *= 10;
				i += *buf - '0';
				len--;
			}
			if (c == 'Y')
				century = i / 100;
			year = i % 100;

			flags |= FLAG_YEAR;

			break;

		case 'Z':
			{
			const char *cp;
			char *zonestr;

			for (cp = buf; *cp &&
			     isupper((unsigned char)*cp); ++cp) {
				/*empty*/}
			if (cp - buf) {
				zonestr = alloca(cp - buf + 1);
				strncpy(zonestr, buf, cp - buf);
				zonestr[cp - buf] = '\0';
				tzset();
				if (0 == strcmp(zonestr, "GMT") ||
				    0 == strcmp(zonestr, "UTC")) {
				    *GMTp = 1;
				} else if (0 == strcmp(zonestr, tzname[0])) {
				    tm->tm_isdst = 0;
				} else if (0 == strcmp(zonestr, tzname[1])) {
				    tm->tm_isdst = 1;
				} else {
				    return (NULL);
				}
				buf += cp - buf;
			}
			}
			break;

		case 'z':
			{
			int sign = 1;

			if (*buf != '+') {
				if (*buf == '-')
					sign = -1;
				else
					return (NULL);
			}

			buf++;
			i = 0;
			for (len = 4; len > 0; len--) {
				if (isdigit((unsigned char)*buf)) {
					i *= 10;
					i += *buf - '0';
					buf++;
				} else if (len == 2) {
					i *= 100;
					break;
				} else
					return (NULL);
			}

			if (i > 1400 || (sign == -1 && i > 1200) ||
			    (i % 100) >= 60)
				return (NULL);
			tm->tm_hour -= sign * (i / 100);
			tm->tm_min  -= sign * (i % 100);
			*GMTp = 1;
			}
			break;

		case 'n':
		case 't':
			while (isspace((unsigned char)*buf))
				buf++;
			break;

		default:
			return (NULL);
		}
	}

	if (century != -1 || year != -1) {
		if (year == -1)
			year = 0;
		if (century == -1) {
			if (year < 69)
				year += 100;
		} else
			year += century * 100 - TM_YEAR_BASE;
		tm->tm_year = year;
	}

	if (!(flags & FLAG_YDAY) && (flags & FLAG_YEAR)) {
		if ((flags & (FLAG_MONTH | FLAG_MDAY)) ==
		    (FLAG_MONTH | FLAG_MDAY)) {
			tm->tm_yday = start_of_month[isleap(tm->tm_year +
			    TM_YEAR_BASE)][tm->tm_mon] + (tm->tm_mday - 1);
			flags |= FLAG_YDAY;
		} else if (day_offset != -1) {
			int tmpwday, tmpyday, fwo;

			fwo = first_wday_of(tm->tm_year + TM_YEAR_BASE);
			/* No incomplete week (week 0). */
			if (week_offset == 0 && fwo == day_offset)
				return (NULL);

			/* Set the date to the first Sunday (or Monday)
			 * of the specified week of the year.
			 */
			tmpwday = (flags & FLAG_WDAY) ? tm->tm_wday :
			    day_offset;
			tmpyday = (7 - fwo + day_offset) % 7 +
			    (week_offset - 1) * 7 +
			    (tmpwday - day_offset + 7) % 7;
			/* Impossible yday for incomplete week (week 0). */
			if (tmpyday < 0) {
				if (flags & FLAG_WDAY)
					return (NULL);
				tmpyday = 0;
			}
			tm->tm_yday = tmpyday;
			flags |= FLAG_YDAY;
		}
	}

	if ((flags & (FLAG_YEAR | FLAG_YDAY)) == (FLAG_YEAR | FLAG_YDAY)) {
		if (!(flags & FLAG_MONTH)) {
			i = 0;
			while (tm->tm_yday >=
			    start_of_month[isleap(tm->tm_year +
			    TM_YEAR_BASE)][i])
				i++;
			if (i > 12) {
				i = 1;
				tm->tm_yday -=
				    start_of_month[isleap(tm->tm_year +
				    TM_YEAR_BASE)][12];
				tm->tm_year++;
			}
			tm->tm_mon = i - 1;
			flags |= FLAG_MONTH;
		}
		if (!(flags & FLAG_MDAY)) {
			tm->tm_mday = tm->tm_yday -
			    start_of_month[isleap(tm->tm_year + TM_YEAR_BASE)]
			    [tm->tm_mon] + 1;
			flags |= FLAG_MDAY;
		}
		if (!(flags & FLAG_WDAY)) {
			wday_offset = first_wday_of(tm->tm_year + TM_YEAR_BASE);
			tm->tm_wday = (wday_offset + tm->tm_yday) % 7;
			flags |= FLAG_WDAY;
		}
	}

	return ((char *)buf);
}

char *
strptime_bsd(const char * __restrict buf, const char * __restrict fmt,
    struct tm * __restrict tm)
{
	char *ret;
	int gmt;

	gmt = 0;
	ret = _strptime(buf, fmt, tm, &gmt);
	if (ret && gmt) {
		time_t t = timegm(tm);

		localtime_r(&t, tm);
	}

	return (ret);
}
