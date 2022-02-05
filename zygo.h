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
};

typedef struct List List;
struct List {
	struct Elem **elems;
	size_t len;
	size_t lastid;
};

enum { EXTR = 1 };
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

extern List *history;
extern List *page;
extern Elem *current;
extern int insecure;

/* Memory functions */
void *emalloc(size_t size);
void *erealloc(void *ptr, size_t size);
char *estrdup(const char *str);

/* Elem functions */
void elem_free(Elem *e);
Elem *elem_dup(Elem *e);
Elem *uritoelem(const char *uri);
Elem *gophertoelem(Elem *from, const char *line);
char *elemtouri(Elem *e);

/* List functions */
void list_free(List **l);
void list_append(List **l, Elem *e);
Elem *list_get(List **l, size_t elem);
Elem *list_idget(List **l, size_t id);
Elem *list_pop(List **l);
size_t list_len(List **l);


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
void syncinput(void);
char *prompt(char *prompt, size_t count);
Elem *strtolink(char *str);
void pagescroll(int lines);

/* Main loop */
void run(void);

/* Misc */
int readline(char *buf, size_t count);
int go(Elem *e, int mhist, int notls);
void sighandler(int signal);
#ifdef ZYGO_STRLCAT
#undef strlcat
size_t strlcat(char *dst, const char *src, size_t dstsize);
#endif
