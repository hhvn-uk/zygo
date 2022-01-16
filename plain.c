#include <unistd.h>
#include <netdb.h>
#include "zygo.h"

static int fd;

#define net_free() \
	if (ai) freeaddrinfo(ai)

int
net_connect(Elem *e) {
	struct addrinfo *ai = NULL;
	int ret;

	if ((ret = getaddrinfo(e->server, e->port, NULL, &ai)) != 0 || ai == NULL) {
		error("could not lookup %s:%s", e->server, e->port);
		goto fail;
	}

	if ((fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1 ||
			connect(fd, ai->ai_addr, ai->ai_addrlen) == -1) {
		error("could not connect to %s:%s", e->server, e->port);
		goto fail;
	}

	net_free();
	return 0;

fail:
	net_free();
	return -1;
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
