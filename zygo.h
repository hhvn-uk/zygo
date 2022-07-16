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
#define zygo_assert(expr) (expr ? ((void)0) : (endwin(), assert(expr)))
#define INFO(desc) {0, 'i', desc, NULL, NULL, NULL}
#define LINK(type, desc, selector, server, port) \
	{0, type, desc, selector, server, port}

typedef struct Elem Elem;
struct Elem {
	int tls;
	char type;
	char *desc;
	char *selector;
	char *server;
	char *port;
	size_t id; /* only set when:
		    * - type != 'i'
		    * - in a list */
	/* Following sizes only set when first in list */
	size_t len;
	size_t lastid;
	struct Elem *next;
};

enum { DEFL, EXTR,
	MDH1, MDH2, MDH3, MDH4 };
typedef struct Scheme Scheme;
struct Scheme {
	char type;
	char *name;
	short fg;
	short pair;
};

enum {
	PAIR_BAR = 1,
	PAIR_URI = 2,
	PAIR_CMD = 3,
	PAIR_ARG = 4,
	PAIR_ERR = 5,
	PAIR_EID = 6,
	PAIR_SCHEME = 7,
};

extern Elem *history;
extern Elem *page;
extern Elem *current;
extern int insecure;

/* Memory functions */
void *emalloc(size_t size);
void *erealloc(void *ptr, size_t size);
char *estrdup(const char *str);

/* Elem functions */
void elem_free(Elem *e);
Elem *elem_create(int tls, char type, char *desc, char *selector, char *server, char *port);
Elem *elem_dup(Elem *e);
Elem *uritoelem(const char *uri);
Elem *gophertoelem(Elem *from, const char *line);
char *elemtouri(Elem *e);

/* List functions */
void list_free(Elem **l);
void list_append(Elem **l, Elem *e);
Elem *list_get(Elem **l, size_t elem);
Elem *list_idget(Elem **l, size_t id);
void list_rev(Elem **l);
size_t list_len(Elem **l);

/* Network functions
 * only works with one fd/ctx at 
 * a time, reset at net_connect */
int net_connect(Elem *e, int silent);
int net_read(void *buf, size_t count);
int net_write(void *buf, size_t count);
int net_close(void);

/* UI functions */
void error(char *format, ...);
Scheme *getscheme(Elem *e);
void find(int backward);
int draw_line(Elem *e, int nwidth);
void draw_page(void);
void draw_bar(void);
void input(int c);
char *prompt(char *prompt, size_t count);
Elem *strtolink(char *str);
void pagescroll(int lines);
void idgo(size_t id);
int wantnum(char cmd);
int acceptkey(char cmd, int key);

/* Main loop */
void run(void);

/* Misc */
int readline(char *buf, size_t count);
int go(Elem *e, int mhist, int notls);
int digits(int i);
void sighandler(int signal);
#ifdef ZYGO_STRLCAT
#undef strlcat
size_t strlcat(char *dst, const char *src, size_t dstsize);
#endif
