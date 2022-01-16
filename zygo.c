/*
 * zygo/zygo.c
 *
 * Copyright (c) 2022 hhvn <dev@hhvn.uk>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <stdio.h>
#include "zygo.h"

List *history = NULL;
List *page = NULL;
Elem *current = NULL;

int config[] = {
	[CONF_TLS_VERIFY] = 1,
};

void
error(char *format, ...) {
	va_list ap;

	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
}

/*
 * Memory functions
 */
void *
emalloc(size_t size) {
	void *mem;

	if ((mem = malloc(size)) == NULL) {
		perror("malloc()");
		exit(EXIT_FAILURE);
	}

	return mem;
}

void *
erealloc(void *ptr, size_t size) {
	void *mem;

	if ((mem = realloc(ptr, size)) == NULL) {
		perror("realloc()");
		exit(EXIT_FAILURE);
	}

	return mem;
}

char *
estrdup(const char *str) {
	char *ret;

	if ((ret = strdup(str)) == NULL) {
		perror("strdup()");
		exit(EXIT_FAILURE);
	}

	return ret;
}

void
estrappend(char **s1, const char *s2) {
	size_t len;

	len = strlen(*s1) + strlen(s2) + 1;
	*s1 = erealloc(*s1, len);
	snprintf(*s1 + strlen(*s1), len - strlen(s2), "%s", s2);
}

/*
 * Elem functions
 */
void
elem_free(Elem *e) {
	if (e) {
		free(e->desc);
		free(e->selector);
		free(e->server);
		free(e->port);
		free(e);
	}
}

Elem *
elem_create(char type, char *desc, char *selector, char *server, char *port) {
	Elem *ret;

#define DUP(str) str ? estrdup(str) : NULL
	ret = emalloc(sizeof(Elem));
	ret->type = type;
	ret->desc = DUP(desc);
	ret->selector = DUP(selector);
	ret->server = DUP(server);
	ret->port = DUP(port);
#undef DUP

	return ret;
}

Elem *
elem_dup(Elem *e) {
	if (e)
		return elem_create(e->type, e->desc, e->selector, e->server, e->port);
	else
		return NULL;
}

char *
elemtouri(Elem *e) {
	static char *ret = NULL;
	char type[2] = {0, 0};

	free(ret);
	ret = NULL;

	switch (e->type) {
	case 'T':
	case '8':
		ret = estrdup("teln://");
		break;
	case 'h':
		if (strncmp(e->selector, "URL:", strlen("URL:")) == 0)
			return (ret = estrdup(e->selector + strlen("URL:")));
		else if (strncmp(e->selector, "/URL:", strlen("/URL:")) == 0)
			return (ret = estrdup(e->selector + strlen("/URL:")));
		/* fallthrough */
	default:
		ret = estrdup(e->tls ? "gophers://" : "gopher://");
		break;
	}

	assert(e->server);
	assert(e->port);

	estrappend(&ret, e->server);
	if (strcmp(e->port, "70") != 0) {
		estrappend(&ret, ":");
		estrappend(&ret, e->port);
	}

	type[0] = e->type;
	estrappend(&ret, "/");
	estrappend(&ret, type);

	if (e->selector && *e->selector && strcmp(e->selector, "/") != 0)
		estrappend(&ret, e->selector);

	return ret;
}

Elem *
uritoelem(const char *uri) {
	Elem *ret;
	char *dup = estrdup(uri);
	char *tmp = dup;
	char *p;
	enum {SEGSERVER, SEGPORT, SEGTYPE, SEGSELECTOR};
	int seg;

	ret = emalloc(sizeof(Elem));
	ret->tls = ret->type = 0;
	ret->desc = ret->selector = ret->server = ret->port;

	if (strncmp(tmp, "gopher://", strlen("gopher://")) == 0) {
		tmp += strlen("gopher://");
	} else if (strncmp(tmp, "gophers://", strlen("gophers://")) == 0) {
		ret->tls = 1;
		tmp += strlen("gophers://");
	} else if (strstr(tmp, "://")) {
		error("non-gopher protocol entered");
		free(ret);
		ret = NULL;
		goto end;
	}

	for (p = tmp, seg = SEGSERVER; *p; p++) {
		if (seg == SEGSELECTOR || *p == '\t') {
			ret->selector = estrdup(p);
			switch (seg) {
			case SEGSERVER:
				*p = '\0';
				ret->server = estrdup(tmp);
				break;
			case SEGPORT:
				*p = '\0';
				ret->port = estrdup(tmp);
				break;
			}
			break;
		} else if (seg == SEGSERVER && *p == ':') {
			*p = '\0';
			ret->server = estrdup(tmp);
			tmp = p + 1;
			seg = SEGPORT;
		} else if (seg == SEGSERVER && *p == '/') {
			*p = '\0';
			ret->server = estrdup(tmp);
			tmp = p + 1;
			seg = SEGTYPE;
		} else if (seg == SEGPORT && *p == '/') {
			*p = '\0';
			ret->port = estrdup(tmp);
			tmp = p + 1;
			seg = SEGTYPE;
		} else if (seg == SEGTYPE) {
			ret->type = *p;
			tmp = p + 1;
			break;
		}
	}

	ret->type     = ret->type     ? ret->type     : '1';
	ret->server   = ret->server   ? ret->server   : estrdup(tmp);
	ret->port     = ret->port     ? ret->port     : estrdup("70");
	ret->selector = ret->selector ? ret->selector : estrdup(tmp);

end:
	free(dup);
	return ret;
}

Elem *
gophertoelem(Elem *from, const char *line) {
	Elem *ret;
	char *dup = estrdup(line);
	char *tmp = dup;
	char *p;
	enum {SEGDESC, SEGSELECTOR, SEGSERVER, SEGPORT};
	int seg;

	ret = emalloc(sizeof(Elem));
	ret->tls = from ? from->tls : 0;
	ret->type = *(tmp++);
	ret->desc = ret->selector = ret->server = ret->port = NULL;

	for (p = tmp, seg = SEGDESC; *p; p++) {
		if (*p == '\t') {
			if (seg == SEGPORT)
				goto invalid;
			*p = '\0';
			switch (seg) {
			case SEGDESC:     ret->desc     = estrdup(tmp); break;
			case SEGSELECTOR: ret->selector = estrdup(tmp); break;
			case SEGSERVER:   ret->server   = estrdup(tmp); break;
			}
			tmp = p + 1;
			seg++;
		}
	}

	ret->port = estrdup(tmp);
	return ret;

invalid:
	free(ret->desc);
	free(ret->selector);
	free(ret->server);
	free(ret->port);
	ret->type = '3';
	ret->desc = estrdup("invalid gopher menu element");
	ret->selector = estrdup("Err");
	ret->server = estrdup("Err");
	ret->port = estrdup("Err");
	return ret;
}

/*
 * List functions
 */
void
list_free(List **l) {
	size_t i;

	assert(l);
	if ((*l)) {
		for (i = 0; i < (*l)->len; i++)
			free(*((*l)->elems + i));
		free((*l)->elems);
		free((*l));
		*l = NULL;
	}
}

void
list_append(List **l, Elem *e) {
	assert(l);

	if (!(*l)) {
		(*l) = emalloc(sizeof(List));
		(*l)->len = 0;
	}

	if (!(*l)->elems) {
		(*l)->len = 1;
		(*l)->elems = emalloc(sizeof(Elem *) * (*l)->len);
		*(*l)->elems = elem_dup(e);
		return;
	}

	(*l)->len++;
	(*l)->elems = erealloc((*l)->elems, sizeof(Elem *) * (*l)->len);
	*((*l)->elems + (*l)->len - 1) = elem_dup(e);
}

Elem *
list_get(List **l, size_t elem) {
	if (!l || !(*l) || (*l)->len == 0 || elem >= (*l)->len)
		return NULL;
	return *((*l)->elems + elem);
}

size_t
list_len(List **l) {
	if (!l || !(*l))
		return 0;
	return (*l)->len;
}

int
readline(char *buf, size_t count) {
	size_t i = 0;
	char c = 0;

	do {
		if (net_read(&c, sizeof(char)) != sizeof(char))
			return 0;
		if (c != '\r')
			buf[i++] = c;
	} while (c != '\n' && i < count);

	buf[i - 1] = 0;
	return 1;
}

int
go(Elem *e) {
	char line[BUFLEN];
	Elem *elem;

	list_free(&page);

	if (e->type != '1' && e->type != '7' && e->type != '+') {
		/* TODO: call plumber */
		return -1;
	}

	if (net_connect(e) == -1)
		return -1;

	net_write(e->selector, strlen(e->selector));
	net_write("\r\n", 2);

	while (readline(line, sizeof(line))) {
		elem = gophertoelem(e, line);
		list_append(&page, elem);
		elem_free(elem);
	}
}

int
main(void) {
	Elem *s = uritoelem("gophers://hhvn.uk/1/");
	go(s);
}
