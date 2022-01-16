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
#include <stdio.h>
#include "zygo.h"
#include "config.h"

List *history = NULL;
List *page = NULL;
Elem *current = NULL;

int candraw = 1;
int config[] = {
	[CONF_TLS_VERIFY] = 1,
};

struct {
	int scroll;
	int wantinput;
	wint_t input[BUFLEN];
	char cmd;
	char *arg;
} ui = {0, 0};

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
elem_create(int tls, char type, char *desc, char *selector, char *server, char *port) {
	Elem *ret;

#define DUP(str) str ? estrdup(str) : NULL
	ret = emalloc(sizeof(Elem));
	ret->tls = tls;
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
		return elem_create(e->tls, e->type, e->desc, e->selector, e->server, e->port);
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

	zygo_assert(e->server);
	zygo_assert(e->port);

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
	ret->tls = 0;
	ret->type = '1';
	ret->desc = ret->selector = ret->server = ret->port = NULL;

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

	if (!ret->server) {
		ret->server = estrdup(tmp);
		tmp += strlen(tmp);
	}

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
	free(dup);
	return ret;

invalid:
	free(dup);
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

	zygo_assert(l);
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
	zygo_assert(l);

	if (!(*l)) {
		(*l) = emalloc(sizeof(List));
		(*l)->len = 0;
		(*l)->elems = NULL;
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

	if (e->type != '1' && e->type != '7' && e->type != '+') {
		/* TODO: call plumber */
		return -1;
	}

	if (net_connect(e) == -1)
		return -1;

	net_write(e->selector, strlen(e->selector));
	net_write("\r\n", 2);

	list_free(&page);
	while (readline(line, sizeof(line))) {
		elem = gophertoelem(e, line);
		list_append(&page, elem);
		elem_free(elem);
	}

	elem_free(current);
	current = elem_dup(e);
	list_append(&history, current);
	ui.scroll = 0;
	return 0;
}

/*
 * UI functions
 */
void
error(char *format, ...) {
	va_list ap;

	move(LINES - 1, 0);
	clrtoeol();
	attron(COLOR_PAIR(PAIR_ERR));
	addstr(" error: ");

	va_start(ap, format);
	vw_printw(stdscr, format, ap);
	va_end(ap);

	addstr(" ");
	refresh();
	candraw = 0;
	alarm(1);
}

Scheme *
getscheme(char type) {
	int i;

	for (i = 0; ; i++)
		if (scheme[i].type == type || scheme[i].type == '\0')
			return &scheme[i];
}

int
draw_line(Elem *e, int maxlines) {
	int lc, cc;

	printw("%s | ", getscheme(e->type)->name);
	attron(COLOR_PAIR(getscheme(e->type)->pair));
	printw("%s\n", e->desc);
	attroff(A_COLOR);
	return 1;
}

void
draw_page(void) {
	int y = 0, i;

	if (!candraw)
		return;

	attroff(A_COLOR);

	move(0, 0);
	zygo_assert(ui.scroll < list_len(&page));
	for (i = ui.scroll; i < list_len(&page) - 1 && y < LINES - 1; i++)
		y += draw_line(list_get(&page, i), 1);
	for (; y < LINES - 1; y++) {
		move(y, 0);
		clrtoeol();
	}
}

void
draw_bar(void) {
	int savey, savex, x;

	if (!candraw)
		return;

	move(LINES - 1, 0);
	clrtoeol();
	attron(COLOR_PAIR(PAIR_URI));
	printw(" %s ", elemtouri(current));
	attron(COLOR_PAIR(PAIR_BAR));
	printw(" ");
	if (ui.wantinput) {
		curs_set(1);
		attron(COLOR_PAIR(PAIR_CMD));
		printw("%c", ui.cmd);
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
syncinput(void) {
	int len;
	free(ui.arg);
	len = wcstombs(NULL, ui.input, 0) + 1;
	ui.arg = emalloc(len);
	wcstombs(ui.arg, ui.input, len);
}

/*
 * Main loop
 */
void
run(void) {
	wint_t c;
	int ret;
	size_t il;
	Elem *e;

	draw_page();
	draw_bar();

	/* get_wch does refresh() for us */
	while ((ret = get_wch(&c)) != ERR) {
		if (!candraw) {
			candraw = 1;
			draw_page();
			draw_bar();
		}

		if (c == KEY_RESIZE) {
			draw_page();
			draw_bar();
		} else if (ui.wantinput) {
			if (c == 27 /* escape */) {
				ui.wantinput = 0;
			} else if (c == '\n') {
				switch (ui.cmd) {
				case ':':
					e = uritoelem(ui.arg);
					go(e);
					elem_free(e);
					break;
				}
				ui.wantinput = 0;
				draw_page();
			} else if (c == KEY_BACKSPACE || c == 127) {
				if (il == 0) {
					ui.wantinput = 0;
				} else {
					ui.input[--il] = '\0';
					syncinput();
				}
			} else if (c >= 32 && c < KEY_CODE_YES) {
				ui.input[il++] = c;
				ui.input[il] = '\0';
				syncinput();
			}
			draw_bar();
		} else {
			switch (c) {
			case KEY_DOWN:
			case 'j':
				if (list_len(&page) - ui.scroll > LINES)
					ui.scroll++;
				draw_page();
				break;
			case 4: /* ^D */
			case 6: /* ^F */
				if (list_len(&page) - ui.scroll > ((int)LINES * 1.5))
					ui.scroll += ((int)LINES / 2);
				else if (list_len(&page) > LINES)
					ui.scroll = list_len(&page) - LINES;
				draw_page();
				break;
			case KEY_UP:
			case 'k':
				if (ui.scroll > 0)
					ui.scroll--;
				draw_page();
				break;
			case 21: /* ^U */
			case 2:  /* ^B */
				ui.scroll -= ((int)LINES / 2);
				if (ui.scroll < 0)
					ui.scroll = 0;
				draw_page();
				break;
			case 3: /* ^C */
			case 'q':
				endwin();
				exit(EXIT_SUCCESS);
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				/* TODO: numbered link handling */
				break;
			case ':':
				ui.cmd = (char)c;
				ui.wantinput = 1;
				ui.input[0] = '\0';
				ui.arg = estrdup("");
				il = 0;
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
	if (signal == SIGALRM) {
		candraw = 1;
		draw_bar();
		refresh();
	}
}

int
main(int argc, char *argv[]) {
	Elem *target;
	char *targeturi;
	int i;

	switch (argc) {
	case 2:
		targeturi = argv[1];
		break;
	case 1:
		targeturi = starturi;
		break;
	default:
		fprintf(stderr, "usage: %s [uri]\n", basename(argv[0]));
		exit(EXIT_FAILURE);
	}

	target = uritoelem(targeturi);
	go(target);

	setlocale(LC_ALL, "");
	initscr();
	noecho();
	start_color();
	use_default_colors();
	keypad(stdscr, TRUE);
	set_escdelay(10);

	signal(SIGALRM, sighandler);

	init_pair(PAIR_BAR, bar_pair[0], bar_pair[1]);
	init_pair(PAIR_URI, uri_pair[0], uri_pair[1]);
	init_pair(PAIR_CMD, cmd_pair[0], cmd_pair[1]);
	init_pair(PAIR_ARG, arg_pair[0], arg_pair[1]);
	init_pair(PAIR_ERR, err_pair[0], err_pair[1]);
	for (i = 0; scheme[i].type; i++) {
		scheme[i].pair = i + PAIR_SCHEME;
		init_pair(scheme[i].pair, scheme[i].fg, -1);
	}

	run();

	endwin();
}
