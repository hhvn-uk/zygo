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
#include <regex.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/wait.h>
#include "zygo.h"
#include "config.h"

List *history = NULL;
List *page = NULL;
Elem *current = NULL;
int insecure = 0;

struct {
	int scroll;
	int wantinput; /* 0 - no
			* 1 - yes (with cmd)
			* 2 - yes (id) */
	wint_t input[BUFLEN];
	char cmd;
	char *arg;
	int search;
	regex_t regex;
	int error;
	char errorbuf[BUFLEN];
	int candraw;
} ui = {.scroll = 0,
	.wantinput = 0,
	.search = 0,
	.error = 0,
	.candraw = 1};

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
elem_dup(Elem *e) {
	Elem *ret;

	if (e) {
#define DUP(str) str ? estrdup(str) : NULL
		ret = emalloc(sizeof(Elem));
		ret->tls = e->tls;
		ret->type = e->type;
		ret->desc = DUP(e->desc);
		ret->selector = DUP(e->selector);
		ret->server = DUP(e->server);
		ret->port = DUP(e->port);
#undef DUP
	} else ret = NULL;

	return ret;
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
	} else if (!ret->port) {
		ret->port = estrdup(tmp);
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
	ret->type = *(tmp++);
	ret->desc = ret->selector = ret->server = ret->port = NULL;

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
	if (from && from->tls &&
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
	Elem *elem;

	zygo_assert(l);

	if (!(*l)) {
		(*l) = emalloc(sizeof(List));
		(*l)->len = 0;
		(*l)->lastid = 0;
		(*l)->elems = NULL;
	}

	elem = elem_dup(e);
	if (elem->type != 'i' && elem->type != '3')
		elem->id = ++(*l)->lastid;
	else
		elem->id = 0;

	if (!(*l)->elems) {
		(*l)->len = 1;
		(*l)->elems = emalloc(sizeof(Elem *) * (*l)->len);
		*(*l)->elems = elem;
		return;
	}

	(*l)->len++;
	(*l)->elems = erealloc((*l)->elems, sizeof(Elem *) * (*l)->len);
	*((*l)->elems + (*l)->len - 1) = elem;
}

Elem *
list_get(List **l, size_t elem) {
	if (!l || !(*l) || (*l)->len == 0 || elem >= (*l)->len)
		return NULL;
	return *((*l)->elems + elem);
}

Elem *
list_idget(List **l, size_t id) {
	int i;

	if (!l || !(*l) || (*l)->len == 0 || id >= (*l)->len)
		return NULL;
	for (i = 0; i < (*l)->len; i++)
		if ((*((*l)->elems + i))->id == id)
			return *((*l)->elems + i);
	return NULL;
}

size_t
list_len(List **l) {
	if (!l || !(*l))
		return 0;
	return (*l)->len;
}

Elem *
list_pop(List **l) {
	Elem *ret;
	size_t i;

	if (!l || !(*l))
		return NULL;

	if ((*l)->len == 1) {
		ret = *(*l)->elems;
		free((*l)->elems);
		free((*l));
		*l = NULL;
	} else {
		ret = *((*l)->elems + (*l)->len - 1);
		(*l)->len--;
		(*l)->elems = erealloc((*l)->elems, sizeof(Elem *) * (*l)->len);
	}

	return ret;
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
go(Elem *e, int mhist) {
	char line[BUFLEN];
	char *sh, *arg, *uri;
	char *search;
	Elem *elem;
	Elem *dup = elem_dup(e); /* elem may be part of page */
	Elem missing = {0, '3', "Full contents not received."};
	int ret;
	int gotall = 0;
	pid_t pid;

	if (!e) return -1;

	if (dup->type != '1' && dup->type != '7' && dup->type != '+') {
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
		if ((search = prompt("Query")) == NULL) {
			elem_free(dup);
			return -1;
		}

		free(dup->selector);
		dup->selector = emalloc(strlen(e->selector) + strlen(search) + 2);
		snprintf(dup->selector, strlen(e->selector) + strlen(search) + 2,
				"%s\t%s", e->selector, search);
	}

	move(LINES - 1, 0);
	clrtoeol();
	printw("Connecting to %s:%s", dup->server, dup->port);
	refresh();

	if ((ret = net_connect(e)) == -1) {
		if (dup->tls) {
			attron(A_BOLD | COLOR_PAIR(PAIR_CMD));
			printw("  Try again in cleartext? ");
			curs_set(1);
			attroff(A_BOLD);
			refresh();
			timeout(stimeout * 1000);
			if (tolower(getch()) == 'y') {
				elem = elem_dup(e);
				elem->tls = 0;
				ret = go(elem, mhist);
				elem_free(elem);
			}
		}

		timeout(-1);
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
			elem = gophertoelem(dup, line);
			list_append(&page, elem);
			elem_free(elem);
		}
	}

	if (!gotall)
		list_append(&page, &missing);

	if (current && mhist)
		list_append(&history, current);
	elem_free(current);
	current = dup;

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

	for (i = 0; ; i++)
		if (scheme[i].type == type || scheme[i].type == '\0')
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

	if (!ui.search) {
		error("no search");
		return;
	}

	for (i = 0; i < list_len(&page); i++) {
		if (regexec(&ui.regex, list_get(&page, i)->desc, 0, NULL, 0) == 0) {
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
	int lc, cc;

	attron(COLOR_PAIR(PAIR_EID));
	if (e->type != 'i' && e->type != '3')
		printw("%1$ *2$d ", e->id, nwidth + 1);
	else
		printw("%1$ *2$s ", "", nwidth + 1);
	attroff(A_COLOR);
	attron(COLOR_PAIR(getscheme(e)->pair));
	printw("%s ", getscheme(e)->name);
	attroff(A_COLOR);
	printw("| ");
	if (ui.search && regexec(&ui.regex, e->desc, 0, NULL, 0) == 0)
		attron(A_REVERSE);
	printw("%s", e->desc);
	attroff(A_REVERSE);
	printw("\n");
	return 1;
}

void
draw_page(void) {
	int y = 0, i;
	int nwidth;

	if (!ui.candraw)
		return;

	attroff(A_COLOR);

	if (page) {
		nwidth = digits(page->lastid);
		move(0, 0);
		zygo_assert(ui.scroll <= list_len(&page));
		for (i = ui.scroll; i <= list_len(&page) - 1 && y < LINES - 1; i++)
			y += draw_line(list_get(&page, i), nwidth);
		for (; y < LINES - 1; y++) {
			move(y, 0);
			clrtoeol();
		}
	}
}

void
draw_bar(void) {
	int savey, savex, x;

	if (!ui.candraw)
		return;

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
		if (ui.wantinput == 1) {
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

void
syncinput(void) {
	int len;
	free(ui.arg);
	len = wcstombs(NULL, ui.input, 0) + 1;
	ui.arg = emalloc(len);
	wcstombs(ui.arg, ui.input, len);
}

char *
prompt(char *prompt) {
	wint_t c;
	int ret;
	size_t il;
	int y, x;

	ui.input[il = 0] = '\0';
	curs_set(1);
	goto start;
	while ((ret = get_wch(&c)) != ERR) {
		if (c == KEY_RESIZE) {
start:
			draw_page();
			move(LINES - 1, 0);
			clrtoeol();
			syncinput();
			printw("%s: %s", prompt, ui.arg);
		} else if (c == 27 /* escape */) {
			return NULL;
		} else if (c == '\n') {
			syncinput();
			return ui.arg;
		} else if (c == KEY_BACKSPACE || c == 127) {
			if (il != 0) {
				getyx(stdscr, y, x);
				move(LINES - 1, x - 1);
				addch(' ');
				move(LINES - 1, x - 1);
				refresh();
				ui.input[--il] = '\0';
			}
		} else if (c >= 32 && c < KEY_CODE_YES) {
			addnwstr(&c, 1);
			ui.input[il++] = c;
			ui.input[il] = '\0';
		}
	}

	return ui.arg;
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
	char tmperror[BUFLEN];

	draw_page();
	draw_bar();

#define checkcurrent() \
	if (!current || !current->server || !current->port) { \
		error("%c command can only be used on remote gopher menus", c); \
		break; \
	}

	/* get_wch does refresh() for us */
	while ((ret = get_wch(&c)) != ERR) {
		if (!ui.candraw || ui.error) {
			ui.candraw = 1;
			ui.error = 0;
			draw_page();
			draw_bar();
		}

		if (c == KEY_RESIZE) {
			draw_page();
			draw_bar();
		} else if (ui.wantinput == 1) {
			if (c == 27 /* escape */) {
				ui.wantinput = 0;
			} else if (c == '\n') {
				switch (ui.cmd) {
				case ':':
					e = uritoelem(ui.arg);
					go(e, 1);
					elem_free(e);
					break;
				case '+':
					if (atoi(ui.arg) >= page->lastid || atoi(ui.arg) < 0) {
						error("no suck link: %s", ui.arg);
					} else {
						move(LINES - 1, 0);
						attroff(A_COLOR);
						clrtoeol();
						printw("%s", elemtouri(list_idget(&page, atoi(ui.arg))));
						curs_set(0);
						ui.candraw = 0;
					}
					break;
				case '/':
				case '?':
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
							find(ui.cmd == '?' ? 1 : 0);
						}
					}
					break;
				case 'a':
					e = elem_dup(current);
					e->selector = erealloc(e->selector, strlen(e->selector) + strlen(ui.arg) + 1);
					/* should be safe.. I think */
					strcat(e->selector, ui.arg);
					go(e, 1);
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
		} else if (ui.wantinput == 2) {
			if (c == 27 /* escape */) {
				ui.wantinput = 0;
			} else if (c == '\n' || il + 1 >= digits(page->lastid)) {
				if (c != '\n') {
					ui.input[il++] = c;
					ui.input[il] = '\0';
					syncinput();
				}
				goto gonum;
			} else if (c == KEY_BACKSPACE || c == 127) {
				if (il == 0) {
					ui.wantinput = 0;
				} else {
					ui.input[--il] = '\0';
					syncinput();
				}
			} else if (isdigit((int)c)) {
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
			case '<':
				if (history) {
					e = list_pop(&history);
					go(e, 0);
					free(e);
					draw_page();
					draw_bar();
				} else {
					error("no history");
				}
				break;
			case '*':
				checkcurrent();
				go(current, 0);
				draw_page();
				draw_bar();
				break;
			case 'g':
				ui.scroll = 0;
				draw_page();
				break;
			case 'G':
				if (list_len(&page) > LINES)
					ui.scroll = list_len(&page) - LINES;
				draw_page();
				break;
			case 'n':
			case 'N':
				find(c == 'N' ? 1 : 0);
				draw_page();
				break;
			case 'r':
				checkcurrent();
				e = elem_dup(current);
				free(e->selector);
				e->selector = strdup("");
				go(e, 1);
				elem_free(e);
				draw_page();
				draw_bar();
				break;
			case 'h':
				manpage();
				break;
			/* link numbers */
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				ui.wantinput = 2;
				ui.input[0] = c;
				ui.input[1] = '\0';
				syncinput();
				il = 1;
				if (digits(page->lastid) == 1)
					goto gonum;
				draw_bar();
				break;
			/* commands with arg */
			case ':':
			case '+':
			case '/':
			case 'a':
				if (c == 'a' || c == '+')
					checkcurrent();
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

		continue;

gonum:
		if (atoi(ui.arg) > page->lastid || atoi(ui.arg) < 1)
			error("no such link: %d", atoi(ui.arg));
		else
			go(list_idget(&page, atoi(ui.arg)), 1);
		ui.wantinput = 0;
		draw_page();
		draw_bar();
	}

#undef checkcurrent
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
	fprintf(stderr, "usage: %s [-kPv] [-p plumber] [uri]\n", basename(argv0));
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[]) {
	Elem *target = NULL;
	char *s;
	int i;
	Elem start[] = {
		{0, '3', "No URI specified, or unable to locate URI."},
		{0, 'i', ""},
		{0, 'i', "Welcome to zygo."},
		{0, '1', " - git repo", "/git/o/zygo", "hhvn.uk", "70"},
		{0, 'i', ""},
		{0, 'i', "Type 'h' to read the man page."},
		{0, 'i', NULL},
	};

	for (i = 0; start[i].desc; i++)
		list_append(&page, &start[i]);

	for (i = 1; i < argc; i++) {
		if ((*argv[i] == '-' && *(argv[i]+1) == '\0') ||
				(*argv[i] != '-' && target)) {
			usage(argv[0]);
		} else if (*argv[i] == '-') {
			for (s = argv[i]+1; *s; s++) {
				switch (*s) {
				case 'k':
#ifdef TLS
					insecure = 1;
#else
					error("TLS support not compiled");
#endif /* TLS */
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
				case 'P':
					parallelplumb = 1;
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
			go(target, 1);
		}
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
