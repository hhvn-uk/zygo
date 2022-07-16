/*
 * zygo/plain.c
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

#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include "zygo.h"

static int fd;
static struct addrinfo *ai = NULL;

int
net_connect(Elem *e, int silent) {
	int ret;

	if (ai)
		freeaddrinfo(ai);

	if ((ret = getaddrinfo(e->server, e->port, NULL, &ai)) != 0 || ai == NULL) {
		if (!silent)
			error("could not lookup %s:%s", e->server, e->port);
		return -1;
	}

	if ((fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1 ||
			connect(fd, ai->ai_addr, ai->ai_addrlen) == -1) {
		if (!silent)
			error("could not connect to %s:%s", e->server, e->port);
		return -1;
	}

	return 0;
}

int
net_read(void *buf, size_t count) {
	return read(fd, buf, count);
}

int
net_write(void *buf, size_t count) {
	return write(fd, buf, count);
}

int
net_close(void) {
	return close(fd);
}
