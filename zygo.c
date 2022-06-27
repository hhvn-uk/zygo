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

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <libgen.h>
#include <assert.h>
#include <locale.h>
#include <signal.h>
#include <unistd.h>
#include <limits.h>
#include <regex.h>
#include <ctype.h>
#include <stdio.h>
#include <wchar.h>
#include <sys/wait.h>
#include "zygo.h"
#include "config.h"

Elem *history = NULL;
Elem *page = NULL;
Elem *current = NULL;
int insecure = 0;

#define TLSOPTS "ku"

struct {
	int scroll;
	int wantinput; /* 0 - no
			* 1 - yes (with cmd)
			* 2 - yes (id) */
	wchar_t input[BUFLEN];
	char cmd;
	char arg[BUFLEN * 4]; /* UTF8 max char size: 4 bytes. 4x sizeof(input) */
	int search;
	regex_t regex;
	int error;
	char errorbuf[BUFLEN];
} ui = {.scroll = 0,
	.wantinput = 0,
	.search = 0,
	.error = 0};

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
elem_create(int tls, char type, char *desc, char *selector, char *server, char *port) {
	Elem *ret = emalloc(sizeof(Elem));
	ret->tls = tls;
#define DUP(str) str ? estrdup(str) : NULL
	ret->type = type;
	ret->desc = DUP(desc);
	ret->selector = DUP(selector);
	ret->server = DUP(server);
	ret->port = DUP(port);
	ret->id = ret->len = ret->lastid = 0;
	ret->next = NULL;
#undef DUP
	return ret;
}

Elem *
elem_dup(Elem *e) {
	return (e ? elem_create(e->tls, e->type, e->desc, e->selector, e->server, e->port) : NULL);
}

char *
elemtouri(Elem *e) {
	static char ret[BUFLEN];
	char type[2] = {0, 0};

	ret[0] = '\0';

	switch (e->type) {
	case 'T':
	case '8':
		strlcat(ret, "teln://", sizeof(ret));
		break;
	case 'h':
		if (strncmp(e->selector, "URL:", strlen("URL:")) == 0) {
			strlcat(ret, e->selector + strlen("URL:"), sizeof(ret));
			return ret;
		}
		else if (strncmp(e->selector, "/URL:", strlen("/URL:")) == 0) {
			strlcat(ret, e->selector + strlen("URL:"), sizeof(ret));
			return ret;
		}
		/* fallthrough */
	default:
		strlcat(ret, e->tls ? "gophers://" : "gopher://", sizeof(ret));
		break;
	}

	zygo_assert(e->server);
	zygo_assert(e->port);

	strlcat(ret, e->server, sizeof(ret));
	if (strcmp(e->port, "70") != 0) {
		strlcat(ret, ":", sizeof(ret));
		strlcat(ret, e->port, sizeof(ret));
	}

	type[0] = e->type;
	strlcat(ret, "/", sizeof(ret));
	strlcat(ret, type, sizeof(ret));

	if (e->selector && *e->selector && strcmp(e->selector, "/") != 0)
		strlcat(ret, e->selector, sizeof(ret));

	return ret;
}

Elem *
uritoelem(const char *uri) {
	Elem *ret;
	char *dup = estrdup(uri);
	char *tmp = dup;
	char *serv = NULL;
	char *p;
	enum {SEGSERVER, SEGTYPE, SEGSELECTOR};
	int seg;

	ret = elem_create(0, '1', NULL, NULL, NULL, NULL);

	if (strncmp(tmp, "gopher://", strlen("gopher://")) == 0) {
		tmp += strlen("gopher://");
	} else if (strncmp(tmp, "gophers://", strlen("gophers://")) == 0) {
#ifdef TLS
		ret->tls = 1;
		tmp += strlen("gophers://");
#else
		error("TLS support not compiled");
		free(ret);
		ret = NULL;
		goto end;
#endif /* TLS */
	} else if (strstr(tmp, "://")) {
		error("non-gopher protocol entered");
		free(ret);
		ret = NULL;
		goto end;
	}

	for (p = tmp, seg = SEGSERVER; *p; p++) {
		if (seg == SEGSELECTOR || *p == '\t') {
			ret->selector = estrdup(p);
			if (seg = SEGSERVER) {
				*p = '\0';
				serv = tmp;
			}
			break;
		} else if (seg == SEGSERVER && *p == '/') {
			*p = '\0';
			serv = tmp;
			tmp = p + 1;
			seg = SEGTYPE;
		} else if (seg == SEGTYPE) {
			ret->type = *p;
			tmp = p + 1;
			break;
		}
	}

	if (!serv && seg == SEGSERVER) {
		serv = tmp;
		tmp += strlen(tmp);
	}

	if (*serv == '[' && (p = strstr(serv + 1, "]:"))) { /* ipv6 + port */
		*p = '\0';
		ret->server = estrdup(serv + 1);
		ret->port = estrdup(p + 2);
	} else if ((p = strchr(serv, ':')) == strrchr(serv, ':') && p) { /* only one : == ipv4 + port */
		*p = '\0';
		ret->server = estrdup(serv);
		ret->port = estrdup(p + 1);
	} else { /* no port */
		ret->server = estrdup(serv);
		ret->port = estrdup("70");
	}

	if (!ret->selector)
		ret->selector = estrdup(tmp);

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

	ret = elem_create(0, *(tmp++), NULL, NULL, NULL, NULL);

	for (p = tmp, seg = SEGDESC; *p; p++) {
		if (*p == '\t') {
			*p = '\0';
			switch (seg) {
			case SEGDESC:     ret->desc     = estrdup(tmp); break;
			case SEGSELECTOR: ret->selector = estrdup(tmp); break;
			case SEGSERVER:   ret->server   = estrdup(tmp); break;
			case SEGPORT:     ret->port     = estrdup(tmp); break;
			}
			tmp = p + 1;
			seg++;
		}
	}

	/* ret->port will only be set on gopher+ menus with 
	 * the above loop, set it here for non-gopher+ */
	if (!ret->port)
		ret->port = estrdup(tmp);
	if (from && from->tls && ret->server && ret->port &&
			strcmp(ret->server, from->server) == 0 &&
			strcmp(ret->port, from->port) == 0)
		ret->tls = 1;
	else
		ret->tls = 0;

	free(dup);

	if (ret->desc != NULL &&
			ret->server != NULL &&
			ret->port != NULL)
		return ret;

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
list_free(Elem **l) {
	Elem *prev, *p;

	if (!l || !*l)
		return;
	for (prev = *l, p = prev->next; p; p = p->next) {
		elem_free(prev);
		prev = p;
	}
	*l = NULL;
}

void
list_append(Elem **l, Elem *e) {
	Elem *elem, *p;

	zygo_assert(l);
	elem = elem_dup(e);

	if (!*l) {
		(*l) = elem;
		(*l)->len = 1;
		(*l)->lastid = 0; /* incremented later */
	} else {
		for (p = *l; p && p->next; p = p->next);
		p->next = elem;
		(*l)->len++;
	}

	if (elem->type != 'i' && elem->type != '3')
		elem->id = ++(*l)->lastid;
}

Elem *
list_get(Elem **l, size_t elem) {
	Elem *p;
	if (!l || !(*l) || (*l)->len == 0 || elem >= (*l)->len)
		return NULL;
	for (p = *l; p && elem; elem--, p = p->next);
	return p;
}

Elem *
list_idget(Elem **l, size_t id) {
	Elem *p;
	if (!l || !(*l) || (*l)->len == 0 || id > (*l)->lastid)
		return NULL;
	for (p = *l; p && id; p = p->next)
		if (p->type != 'i' && p->type != '3')
			if (!--id)
				break;
	return p;
}

size_t
list_len(Elem **l) {
	if (!l || !(*l))
		return 0;
	return (*l)->len;
}

void
list_rev(Elem **l) {
	Elem *p, *prev, *next;
	size_t len, lastid;

	if (!l || !*l)
		return;

	len = (*l)->len;
	lastid = (*l)->lastid;

	for (p = *l, prev = NULL; p; p = next) {
		next = p->next;
		p->next = prev;
		prev = p;
		if (p->id)
			p->id = lastid - p->id + 1;
	}

	prev->len = len;
	prev->lastid = lastid;

	*l = prev;
}

/*
 * Misc functions
 */
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
go(Elem *e, int mhist, int notls) {
	char line[BUFLEN];
	char *sh, *arg, *uri;
	char *pstr;
	Elem *elem;
	Elem *dup = elem_dup(e); /* elem may be part of page */
	Elem missing = {0, '3', "Full contents not received."};
	int ret;
	int gotall = 0;
	pid_t pid;

	if (!e) return -1;

	if (dup->type != '0' && dup->type != '1' && dup->type != '7' && dup->type != '+') {
		/* call mario */
		uri = elemtouri(e);
		arg = emalloc(strlen(plumber) + strlen(uri) + 2);
		snprintf(arg, strlen(plumber) + strlen(uri) + 2, "%s %s", plumber, uri);

		sh = getenv("SHELL");
		sh = sh ? sh : "/bin/sh";

		if (!parallelplumb)
			endwin();

		if ((pid = fork()) == 0) {
			if (parallelplumb) {
				close(1);
				close(2);
			}
			execl(sh, sh, "-c", arg, NULL);
		}
		zygo_assert(pid != -1);

		if (!parallelplumb) {
			waitpid(pid, NULL, 0);
			fprintf(stderr, "Press enter...");
			fread(&line, sizeof(char), 1, stdin);
				initscr();
		}
		return -1;
	}

	if (dup->type == '7' && !strchr(dup->selector, '\t')) {
		if ((pstr = prompt("Query: ", 0)) == NULL) {
			elem_free(dup);
			return -1;
		}

		free(dup->selector);
		dup->selector = emalloc(strlen(e->selector) + strlen(pstr) + 2);
		snprintf(dup->selector, strlen(e->selector) + strlen(pstr) + 2,
				"%s\t%s", e->selector, pstr);
	}

	move(LINES - 1, 0);
	clrtoeol();
#ifdef TLS
	if (!dup->tls && autotls && !notls &&
			(!current || strcmp(current->server, dup->server) != 0)) {
		dup->tls = 1;
		printw("Attempting a TLS connection with %s:%s", dup->server, dup->port);
	} else {
#endif /* TLS */
		printw("Connecting to %s:%s", dup->server, dup->port);
#ifdef TLS
	}
#endif /* TLS */
	refresh();

	if ((ret = net_connect(dup, e->tls != dup->tls)) == -1) {
		if (dup->tls && dup->tls == e->tls) {
			timeout(stimeout * 1000);
			pstr = prompt("TLS failed. Retry in cleartext (y/n)? ", 1);
			if (pstr && tolower(*pstr) == 'y') {
				dup->tls = 0;
				ui.error = 0; /* hide the TLS error */
				ret = go(dup, mhist, 1);
			}
			timeout(-1);
		} else if (dup->tls) {
			dup->tls = 0;
			ret = go(dup, mhist, 1);
		}

		elem_free(dup);
		return ret;
	}

	net_write(dup->selector, strlen(dup->selector));
	net_write("\r\n", 2);

	list_free(&page);
	while (readline(line, sizeof(line))) {
		if (strcmp(line, ".") == 0) {
			gotall = 1;
		} else {
			if (dup->type == '0')
				elem = elem_create(0, 'i', estrdup(line), NULL, NULL, NULL);
			else
				elem = gophertoelem(dup, line);
			list_append(&page, elem);
			elem_free(elem);
		}
	}

	if (!gotall && dup->type != '0')
		list_append(&page, &missing);

	elem_free(current);
	current = dup;
	if (mhist)
		list_append(&history, current);

	ui.scroll = 0;
	if (ui.search) {
		regfree(&ui.regex);
		ui.search = 0;
	}

	return 0;
}

int
digits(int i) {
	int ret = 0;

	do {
		ret++;
		i /= 10;
	} while (i != 0);

	return ret;
}

/*
 * UI functions
 */
void
error(char *format, ...) {
	va_list ap;

	ui.error = 1;

	va_start(ap, format);
	vsnprintf(ui.errorbuf, sizeof(ui.errorbuf), format, ap);
	va_end(ap);

	draw_bar();
}

Scheme *
getscheme(Elem *e) {
	char type;
	int i;

	type = e->type;
	if (type == 'h' && strstr(e->selector, "URL:"))
		type = EXTR;

	/* Try to get scheme from markdown header */
	if (type == 'i' && mdhilight) {
		/* 4+ matches MDH4 */
		if (strncmp(e->desc, "####", 4) == 0)
			type = MDH4;
		else if (strncmp(e->desc, "###", 3) == 0)
			type = MDH3;
		else if (strncmp(e->desc, "##", 2) == 0)
			type = MDH2;
		else if (strncmp(e->desc, "#", 1) == 0)
			type = MDH1;
	}

	for (i = 0; ; i++)
		if (scheme[i].type == type || scheme[i].type == DEFL)
			return &scheme[i];
}

void
find(int backward) {
	enum {mfirst, mclose, mlast};
	struct {
		size_t pos;
		int found;
	} matches[] = {
		[mfirst] = {.found = 0},
		[mclose] = {.found = 0},
		[mlast]  = {.found = 0},
	};
	size_t i;
	size_t want;
	Elem *e;

	if (!ui.search) {
		error("no search");
		return;
	}

	for (i = 0, e = page; i < list_len(&page); i++, e = e->next) {
		if (regexec(&ui.regex, e->desc, 0, NULL, 0) == 0) {
			matches[mlast].found = 1;
			matches[mlast].pos = i;
			if (!matches[mfirst].found) {
				matches[mfirst].found = 1;
				matches[mfirst].pos = i;
			}
			if (!matches[mclose].found && ((backward && i < ui.scroll) || (!backward && i > ui.scroll))) {
				matches[mclose].found = 1;
				matches[mclose].pos = i;
			}
		}
	}

	if (matches[mfirst].found == 0 &&
			matches[mclose].found == 0 &&
			matches[mlast].found == 0) {
		error("no match");
		return;
	}

	if (matches[mclose].found)
		want = matches[mclose].pos;
	else if (backward && matches[mlast].found)
		want = matches[mlast].pos;
	else
		want = matches[mfirst].pos;

	ui.scroll = want;
}

int
draw_line(Elem *e, int nwidth) {
	int y, x, len;
	wchar_t *mbdesc, *p;

	if (nwidth)
		attron(COLOR_PAIR(PAIR_EID));

	if (e->type != 'i' && e->type != '3')
		printw("%1$ *2$ld ", e->id, nwidth + 1);
	else if (nwidth)
		printw("%1$ *2$s ", "", nwidth + 1);

	if (nwidth) {
		attroff(A_COLOR);
		attron(COLOR_PAIR(getscheme(e)->pair));
		printw("%s ", getscheme(e)->name);
		attroff(A_COLOR);
		printw("%s ", normsep);
	} else {
		attroff(A_COLOR);
	}

	if (ui.search && regexec(&ui.regex, e->desc, 0, NULL, 0) == 0)
		attron(A_REVERSE);

	if (mdhilight && strncmp(e->desc, "#", 1) == 0) {
		attron(A_BOLD);
		attron(COLOR_PAIR(getscheme(e)->pair));
		attroff(A_BOLD);
	}

	len = mbstowcs(NULL, e->desc, 0) + 1;
	mbdesc = emalloc(len * sizeof(wchar_t*));
	mbstowcs(mbdesc, e->desc, len);

	getyx(stdscr, y, x);
	for (p = mbdesc; *p; p++) {
		x++;
		if (x == COLS) {
			attron(A_REVERSE);
			printw("%s", toolong);
			goto end;
		}
		addnwstr(p, 1);
	}

	printw("\n");
end:
	free(mbdesc);
	attroff(A_REVERSE);
	return y + 1;
}

void
draw_page(void) {
	int y = 0, i;
	int nwidth;
	Elem *e;

	attroff(A_COLOR);
	if (page) {
		if (!current || current->type != '0')
			nwidth = digits(page->lastid);
		else
			nwidth = 0;
		move(0, 0);
		if (ui.scroll > list_len(&page))
			ui.scroll = 0;
		for (i = ui.scroll, e = list_get(&page, i); i <= list_len(&page) - 1 && y != LINES - 1; i++, e = e->next)
			y = draw_line(e, nwidth);
		for (; y < LINES - 1; y++) {
			move(y, 0);
			clrtoeol();
		}
	}
}

void
draw_bar(void) {
	int savey, savex, x;

	move(LINES - 1, 0);
	clrtoeol();
	if (current) {
		attron(COLOR_PAIR(PAIR_URI));
		printw(" %s ", elemtouri(current));
	}
	attron(COLOR_PAIR(PAIR_BAR));
	printw(" ");
	if (ui.error) {
		curs_set(0);
		attron(COLOR_PAIR(PAIR_ERR));
		printw("%s", ui.errorbuf);
	} else if (ui.wantinput) {
		curs_set(1);
		if (ui.cmd) {
			attron(COLOR_PAIR(PAIR_CMD));
			printw("%c", ui.cmd);
		}
		attron(COLOR_PAIR(PAIR_ARG));
		printw("%s", ui.arg);
	} else curs_set(0);

	attron(COLOR_PAIR(PAIR_BAR));
	getyx(stdscr, savey, savex);
	for (x = savex; x < COLS; x++)
		addch(' ');
	move(savey, savex);
}

void
manpage(void) {
	pid_t pid;
	int status;
	char buf;
	char *sh;

	endwin();

	sh = getenv("SHELL");
	sh = sh ? sh : "/bin/sh";

	pid = fork();
	if (pid == 0)
		execlp(sh, sh, "-c", "man zygo", NULL);
	assert(pid != -1);
	waitpid(pid, &status, 0);
	if (WEXITSTATUS(status) != 0) {
		fprintf(stderr, "%s", "could not find manpage, press enter to continue...");
		fread(&buf, sizeof(char), 1, stdin);
	}

	initscr();
	draw_page();
	draw_bar();
}

/* 0 - clear
 * KEY_BACKSPACE - remove character
 * other - append character */
void
input(int c) {
	static size_t il = 0;
	size_t len;

	if (!c) {
		ui.input[il = 0] = '\0';
		ui.arg[0] = '\0';
		return;
	} else if (c == KEY_BACKSPACE) {
		ui.input[--il] = '\0';
	} else if (il == sizeof(ui.input)) {
		return;
	} else {
		ui.input[il++] = c;
		ui.input[il] = '\0';
	}

	wcstombs(ui.arg, ui.input, sizeof(ui.arg));
}

char *
prompt(char *prompt, size_t count) {
	wint_t c;
	int ret;
	int y, x;

	attrset(A_NORMAL);
	input(0);
	curs_set(1);
	goto start;
	while ((ret = get_wch(&c)) != ERR) {
		if (c == KEY_RESIZE) {
start:
			draw_page();
			move(LINES - 1, 0);
			clrtoeol();
			printw("%s%s", prompt, ui.arg);
		} else if (c == 27 /* escape */) {
			return NULL;
		} else if (c == '\n') {
			goto end;
		} else if (c == KEY_BACKSPACE || c == 127) {
			if (ui.input[0]) {
				getyx(stdscr, y, x);
				move(LINES - 1, x - 1);
				addch(' ');
				move(LINES - 1, x - 1);
				refresh();
				input(KEY_BACKSPACE);
			}
		} else if (c >= 32 && c < KEY_CODE_YES) {
			addnwstr((wchar_t *)&c, 1);
			input(c);
		}

		if (count && wcslen(ui.input) == count)
			goto end;
	}

end:
	return ui.arg;
}

Elem *
strtolink(char *str) {
	if (atoi(str) > page->lastid || atoi(str) < 0) {
		error("no such link: %s", str);
		return NULL;
	}

	return list_idget(&page, atoi(str));
}

void
yank(Elem *e) {
	char *uri, *sh;
	int pfd[2];
	int status;
	pid_t pid;

	uri = elemtouri(e);

	zygo_assert(pipe(pfd) != -1);
	zygo_assert((pid = fork()) != -1);

	sh = getenv("SHELL");
	sh = sh ? sh : "/bin/sh";

	if (pid == 0) {
		close(0);
		close(1);
		close(2);
		close(pfd[1]);
		dup2(pfd[0], 0);
		execl(sh, sh, "-c", yanker, NULL);
	}

	close(pfd[0]);
	write(pfd[1], uri, strlen(uri));
	close(pfd[1]);

	waitpid(pid, &status, 0);
	if (WEXITSTATUS(status) != 0)
		error("could not execute '%s' for yanking", yanker);

	free(uri);
}

void
pagescroll(int lines) {
	if (lines > 0 && list_len(&page) > LINES - 1) {
		ui.scroll += lines;
		if (ui.scroll > list_len(&page) - LINES)
			ui.scroll = list_len(&page) - LINES + 1;
	} else if (lines < 0) {
		ui.scroll += lines;
		if (ui.scroll < 0)
			ui.scroll = 0;
	} /* else intentionally left blank */
	draw_page();
}

void
idgo(size_t id) {
	if (id > page->lastid || id < 1)
		error("no such link: %d", id);
	else
		go(list_idget(&page, id), 1, 0);
}

int
wantnum(char cmd) {
	return (!ui.cmd || ui.cmd == BIND_DISPLAY || ui.cmd == BIND_YANK);
}

int
acceptkey(char cmd, int key) {
	if (wantnum(cmd))
		return isdigit(key);
	return key >= 32 && key < KEY_CODE_YES;
}

/*
 * Main loop
 */
void
run(void) {
	wint_t c;
	int ret, i;
	Elem *e;
	char tmperror[BUFLEN];

	draw_page();
	draw_bar();

	/* get_wch does refresh() for us */
	while ((ret = get_wch(&c)) != ERR) {
		if (ui.error && c != KEY_RESIZE)
			ui.error = 0;

		if (c == KEY_RESIZE) {
			draw_page();
			draw_bar();
		} else if (ui.wantinput) {
			if (c == 27 /* escape */) {
				ui.wantinput = 0;
			} else if (c == '\n') {
submit:
				switch (ui.cmd) {
				case BIND_URI:
					e = uritoelem(ui.arg);
					go(e, 1, 0);
					elem_free(e);
					break;
				case BIND_DISPLAY:
					if ((e = strtolink(ui.arg))) {
						move(LINES - 1, 0);
						attroff(A_COLOR);
						clrtoeol();
						printw("%s", elemtouri(list_idget(&page, atoi(ui.arg))));
						curs_set(0);
						getch(); /* wait */
					}
					break;
				case BIND_SEARCH:
				case BIND_SEARCH_BACK:
					if (ui.search) {
						regfree(&ui.regex);
						ui.search = 0;
					}

					if (ui.input[0] != '\0') {
						if ((ret = regcomp(&ui.regex, ui.arg, regexflags)) != 0) {
							regerror(ret, &ui.regex, (char *)&tmperror, sizeof(tmperror));
							error("could not compile regex '%s': %s", ui.arg, tmperror);
						} else {
							ui.search = 1;
							find(ui.cmd == BIND_SEARCH_BACK ? 1 : 0);
						}
					}
					break;
				case BIND_APPEND:
					e = elem_dup(current);
					e->selector = erealloc(e->selector, strlen(e->selector) + strlen(ui.arg) + 1);
					/* should be safe.. I think */
					strcat(e->selector, ui.arg);
					go(e, 1, 0);
					elem_free(e);
					break;
				case BIND_YANK:
					if ((e = strtolink(ui.arg)))
						yank(e);
					break;
				case '\0': /* links */
					idgo(atoi(ui.arg));
				}
				ui.wantinput = 0;
				draw_page();
			} else if (c == KEY_BACKSPACE || c == 127) {
				if ((ui.cmd && !ui.input[0]) || (!ui.cmd && !ui.input[1]))
					ui.wantinput = 0;
				else
					input(KEY_BACKSPACE);
			} else if (ui.cmd == BIND_YANK && c == BIND_YANK && !ui.input[0]) {
				ui.wantinput = 0;
				yank(current);
			} else if (ui.cmd && acceptkey(ui.cmd, c)) {
				input(c);
				if (wantnum(ui.cmd) && atoi(ui.arg) * 10 > page->lastid)
					goto submit;
			}
			draw_bar();
		} else {
			if ((c == BIND_RELOAD || c == BIND_ROOT || c == BIND_APPEND || c == BIND_YANK) &&
					(!current || !current->server || !current->port)) {
				error("%c command can only be used on remote gopher menus", c);
				continue;
			}

			switch (c) {
			case KEY_DOWN:
			case BIND_DOWN:
				pagescroll(1);
				break;
			case 4: /* ^D */
			case 6: /* ^F */
				pagescroll((int)(LINES / 2));
				break;
			case KEY_UP:
			case 'k':
				pagescroll(-1);
				break;
			case 21: /* ^U */
			case 2:  /* ^B */
				pagescroll(-(int)(LINES / 2));
				break;
			case 3: /* ^C */
			case BIND_QUIT:
				endwin();
				exit(EXIT_SUCCESS);
			case BIND_BACK:
				if (history && history->next) {
					for (e = history; e; e = e->next)
						if (e->next && !e->next->next)
							break;
					go(e, 0, 0);
					free(e->next);
					e->next = NULL;
					draw_page();
					draw_bar();
				} else {
					error("no previous history");
				}
				break;
			case BIND_RELOAD:
				go(current, 0, 0);
				draw_page();
				draw_bar();
				break;
			case BIND_TOP:
				pagescroll(INT_MIN);
				break;
			case BIND_BOTTOM:
				pagescroll(INT_MAX);
				break;
			case BIND_SEARCH_NEXT:
			case BIND_SEARCH_PREV:
				find(c == BIND_SEARCH_PREV ? 1 : 0);
				draw_page();
				break;
			case BIND_ROOT:
				e = elem_dup(current);
				free(e->selector);
				e->selector = strdup("");
				go(e, 1, 0);
				elem_free(e);
				draw_page();
				draw_bar();
				break;
			case BIND_HELP:
				manpage();
				break;
			case BIND_HISTORY:
				if (history) {
					elem_free(current);
					current = NULL;
					list_free(&page);
					for (e = history; e; e = e->next) {
						free(e->desc);
						e->desc = elemtouri(e);
						list_append(&page, e);
					}
					list_rev(&page);
					draw_bar();
					draw_page();
				} else {
					error("no history");
				}
				break;
			/* link numbers */
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				ui.wantinput = 1;
				ui.cmd = '\0';
				input(0);
				input(c);
				if (atoi(ui.arg) * 10 > page->lastid) {
					idgo(atoi(ui.arg));
					ui.wantinput = 0;
					draw_page();
				}
				draw_bar();
				break;
			/* commands with arg */
			case BIND_URI:
			case BIND_DISPLAY:
			case BIND_SEARCH:
			case BIND_SEARCH_BACK:
			case BIND_APPEND:
			case BIND_YANK:
				ui.cmd = (char)c;
				ui.wantinput = 1;
				input(0);
				draw_bar();
				break;
			case '\n':
			case 27: /* escape */
			case KEY_BACKSPACE:
				break;
			default:
				error("not bound");
				break;
			}
		}
	}
}

void
sighandler(int signal) {
	switch (signal) {
	case SIGCHLD:
		while (waitpid(-1, NULL, WNOHANG) == 0);
		break;
	}
}

void
usage(char *argv0) {
#ifdef TLS
#define OPTS "-Pv" TLSOPTS
#else
#define OPTS "-Pv"
#endif /* TLS */
	fprintf(stderr, "usage: %s [%s] [-p plumber] [-y yanker] [uri]\n", basename(argv0), OPTS);
	exit(EXIT_FAILURE);
#undef OPTS
}

int
main(int argc, char *argv[]) {
	Elem *target = NULL;
	Elem err = {0, 0, NULL, NULL, NULL, NULL, 0};
	char *s;
	int i;
	Elem start[] = {
		{0, 'i', "Welcome to zygo."},
		{0, '1', " - git repo", "/git/o/zygo", "hhvn.uk", "70"},
		{0, 'i', ""},
		{0, 'i', "Type 'h' to read the man page."},
		{0, 'i', NULL},
	};

	for (i = 1; i < argc; i++) {
		if ((*argv[i] == '-' && *(argv[i]+1) == '\0') ||
				(*argv[i] != '-' && target)) {
			usage(argv[0]);
		} else if (*argv[i] == '-') {
			for (s = argv[i]+1; *s; s++) {
#ifndef TLS
				if (strchr(TLSOPTS, *s)) {
					fprintf(stderr, "-%c: TLS support not compiled\n", *s);
					exit(EXIT_FAILURE);
				}
#endif /* TLS */
				switch (*s) {
				case 'k':
					insecure = 1;
					break;
				case 'p':
					if (*(s+1)) {
						plumber = s + 1;
						s += strlen(s) - 1;
					} else if (i + 1 != argc) {
						plumber = argv[++i];
					} else {
						usage(argv[0]);
					}
					break;
				case 'y':
					if (*(s+1)) {
						yanker = s + 1;
						s += strlen(s) - 1;
					} else if (i + 1 != argc) {
						yanker = argv[++i];
					} else {
						usage(argv[0]);
					}
					break;
				case 'P':
					parallelplumb = 1;
					break;
				case 'u':
					autotls = 1;
					break;
				case 'v':
					fprintf(stderr, "zygo %s\n", COMMIT);
					exit(EXIT_SUCCESS);
				default:
					usage(argv[0]);
				}
			}
		} else {
			target = uritoelem(argv[argc-1]);
			go(target, 1, 0);
		}
	}

	if (!page) {
		if (ui.error) {
			err.type = '3';
			err.desc = ui.errorbuf;
			list_append(&page, &err);
			err.type = 'i';
			err.desc = "";
			list_append(&page, &err);
		}

		for (i = 0; start[i].desc; i++)
			list_append(&page, &start[i]);
	}

	setlocale(LC_ALL, "");
	initscr();
	noecho();
	start_color();
	use_default_colors();
	keypad(stdscr, TRUE);
	set_escdelay(10);

	signal(SIGALRM, sighandler);
	signal(SIGCHLD, sighandler);

	init_pair(PAIR_BAR, bar_pair[0], bar_pair[1]);
	init_pair(PAIR_URI, uri_pair[0], uri_pair[1]);
	init_pair(PAIR_CMD, cmd_pair[0], cmd_pair[1]);
	init_pair(PAIR_ARG, arg_pair[0], arg_pair[1]);
	init_pair(PAIR_ERR, err_pair[0], err_pair[1]);
	init_pair(PAIR_EID, eid_pair[0], eid_pair[1]);
	for (i = 0; i == 0 || scheme[i - 1].type; i++) {
		scheme[i].pair = i + PAIR_SCHEME;
		init_pair(scheme[i].pair, scheme[i].fg, -1);
	}

	run();

	endwin();
}
