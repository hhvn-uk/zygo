/*
 * zygo/zygo.h
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

#define BUFLEN 2048

typedef struct Elem Elem;
struct Elem {
	int tls;
	char type;
	char *desc;
	char *selector;
	char *server;
	char *port;
};

typedef struct List List;
struct List {
	struct Elem **elems;
	size_t len;
};

enum {
	CONF_TLS_VERIFY = 'k',
};

extern List *history;
extern List *page;
extern Elem *current;
extern int config[];

void error(char *format, ...);
void *emalloc(size_t size);
void *erealloc(void *ptr, size_t size);
char *estrdup(const char *str);
void estrappend(char **s1, const char *s2);

void elem_free(Elem *e);
Elem *elem_create(char type, char *desc, char *selector, char *server, char *port);
Elem *elem_dup(Elem *e);
Elem *uritoelem(const char *uri);
Elem *gophertoelem(Elem *from, const char *line);
char *elemtouri(Elem *e);
int readline(char *buf, size_t count);
int go(Elem *e);

/* only works with one fd/ctx at 
 * a time, reset at net_connect */
int net_connect(Elem *e);
int net_read(void *buf, size_t count);
int net_write(void *buf, size_t count);
int net_close(void);

#ifdef DEBUG
void *elem_put(Elem *e); /* debug */
#endif /* DEBUG */
