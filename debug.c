/*
 * zygo/debug.c
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

#include <stdio.h>
#include "zygo.h"

#define debug_start() printf("--- %s: start ---\n", __func__)
#define debug_end()   printf("--- %s: end   ---\n", __func__)

void *
elem_put(Elem *e) {
	debug_start();
	fprintf(stderr, "TLS: %d\n", e->tls);
	fprintf(stderr, "Type: %c\n", e->type);
	fprintf(stderr, "Desc: %s\n", e->desc);
	fprintf(stderr, "Selector: %s\n", e->selector);
	fprintf(stderr, "Server: %s\n", e->server);
	fprintf(stderr, "Port: %s\n", e->port);
	debug_end();
}

void *
list_put(List **l) {
	size_t i;

	for (i = 0; i < list_len(l); i++)
		elem_put(list_get(l, i));
}
