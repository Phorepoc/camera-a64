/* Copyright The Phorepoc Authors
 * SPDX-License-Identifier: GPL-2.0-only */

/* phe_lib.c -- the freestanding string and formatting helpers of phe_rt.h: strlen/strcmp/strncmp/memcmp/memdup and a
 * vsnprintf with the conversions the vendored code prints (%d %i %u %x %X %o %c %s %p %%, flags - 0 + space #,
 * width, precision, h hh l ll z t j). Allocation, locks, time and logging are the kernel side's (glue/phe_kern.cpp). */
#include "../include/phe_rt.h"
#include <stdbool.h>

size_t shim_strlen(const char *s) { const char *p = s; while (*p) ++p; return (size_t)(p - s); }
int shim_strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) { ++a; ++b; }
	return (unsigned char)*a - (unsigned char)*b;
}
int shim_strncmp(const char *a, const char *b, size_t n)
{
	for (; n; --n, ++a, ++b)
		if (*a != *b || !*a)
			return (unsigned char)*a - (unsigned char)*b;
	return 0;
}
char *shim_strncpy(char *d, const char *s, size_t n)
{
	size_t i = 0;
	for (; i < n && s[i]; ++i) d[i] = s[i];
	for (; i < n; ++i) d[i] = 0;
	return d;
}
int shim_memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *x = a, *y = b;
	for (; n; --n, ++x, ++y)
		if (*x != *y)
			return *x - *y;
	return 0;
}
void *shim_memdup(const void *s, size_t n)
{
	unsigned char *d = shim_alloc(n);
	size_t i;
	if (d)
		for (i = 0; i < n; ++i) d[i] = ((const unsigned char *)s)[i];
	return d;
}

struct out { char *buf; size_t n, pos; };
static void put(struct out *o, char c) { if (o->pos + 1 < o->n) o->buf[o->pos] = c; o->pos++; }

static void put_num(struct out *o, unsigned long long v, int neg, unsigned base, bool upper, int width, int prec,
	bool left, bool zero, char sign, bool alt)
{
	char tmp[24];
	const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	int len = 0, pad, i, pre = 0;
	char pfx[3];
	if (!v && prec != 0)
		tmp[len++] = '0';
	while (v) { tmp[len++] = dig[v % base]; v /= base; }
	if (neg) pfx[pre++] = '-'; else if (sign) pfx[pre++] = sign;
	if (alt && base == 16) { pfx[pre++] = '0'; pfx[pre++] = upper ? 'X' : 'x'; }
	if (prec < len) prec = len;
	pad = width - prec - pre;
	if (!left && !zero) for (; pad > 0; --pad) put(o, ' ');
	for (i = 0; i < pre; ++i) put(o, pfx[i]);
	if (!left && zero) for (; pad > 0; --pad) put(o, '0');
	for (i = prec - len; i > 0; --i) put(o, '0');
	while (len) put(o, tmp[--len]);
	for (; pad > 0; --pad) put(o, ' ');
}

int shim_vsnprintf(char *buf, size_t n, const char *f, va_list ap)
{
	struct out o = { buf, n, 0 };
	for (; *f; ++f) {
		bool left = false, zero = false, alt = false;
		char sign = 0;
		int width = 0, prec = -1, lng = 0;
		if (*f != '%') { put(&o, *f); continue; }
		for (++f;; ++f) {
			if (*f == '-') left = true; else if (*f == '0') zero = true; else if (*f == '+') sign = '+';
			else if (*f == ' ') { if (!sign) sign = ' '; } else if (*f == '#') alt = true; else break;
		}
		if (*f == '*') { width = va_arg(ap, int); ++f; } else while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
		if (*f == '.') { ++f; prec = 0; if (*f == '*') { prec = va_arg(ap, int); ++f; } else while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
		for (;; ++f) {
			if (*f == 'l') lng++; else if (*f == 'h') lng = lng > 0 ? lng : -1; else if (*f == 'z' || *f == 't' || *f == 'j') lng = (*f == 'j') ? 2 : 1; else break;
		}
		switch (*f) {
		case 'd': case 'i': {
			long long v = lng >= 2 ? va_arg(ap, long long) : lng == 1 ? va_arg(ap, long) : va_arg(ap, int);
			put_num(&o, v < 0 ? -(unsigned long long)v : (unsigned long long)v, v < 0, 10, false, width, prec, left, zero && prec < 0, sign, false);
			break; }
		case 'u': case 'x': case 'X': case 'o': {
			unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long) : lng == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
			put_num(&o, v, 0, *f == 'o' ? 8 : *f == 'u' ? 10 : 16, *f == 'X', width, prec, left, zero && prec < 0, 0, alt);
			break; }
		case 'p': {
			void *p = va_arg(ap, void *);
			while (f[1] >= 'A' && f[1] <= 'z' && ((f[1] >= 'a' && f[1] <= 'z') || (f[1] >= 'A' && f[1] <= 'Z'))) ++f;	/* %pOF etc. */
			put_num(&o, (unsigned long)p, 0, 16, false, width, prec, left, zero, 0, true);
			break; }
		case 'c': put(&o, (char)va_arg(ap, int)); break;
		case 's': {
			const char *s = va_arg(ap, const char *);
			int len = 0, pad;
			if (!s) s = "(null)";
			while (s[len] && (prec < 0 || len < prec)) ++len;
			pad = width - len;
			if (!left) for (; pad > 0; --pad) put(&o, ' ');
			for (int i = 0; i < len; ++i) put(&o, s[i]);
			for (; pad > 0; --pad) put(&o, ' ');
			break; }
		case '%': put(&o, '%'); break;
		case 0: --f; break;
		default: put(&o, '%'); put(&o, *f); break;
		}
	}
	if (n) buf[o.pos < n ? o.pos : n - 1] = 0;
	return (int)o.pos;
}

int shim_snprintf(char *buf, size_t n, const char *fmt, ...)
{
	va_list ap;
	int r;
	va_start(ap, fmt);
	r = shim_vsnprintf(buf, n, fmt, ap);
	va_end(ap);
	return r;
}
