#include <unistd.h>
#include <netdb.h>
#include <tls.h>
#include "zygo.h"

struct tls *ctx = NULL;
struct tls_config *conf = NULL;
int fd;
int tls;

int
net_connect(Elem *e) {
	struct addrinfo *ai = NULL;
	int ret;

	tls = e->tls;

	if (tls) {
		if (conf)
			tls_config_free(conf);
		if (ctx)
			tls_free(ctx);

		if ((conf = tls_config_new()) == NULL) {
			error("tls_config_new(): %s", tls_config_error(conf));
			goto fail;
		}

		if (insecure) {
			tls_config_insecure_noverifycert(conf);
			tls_config_insecure_noverifyname(conf);
		}

		if ((ctx = tls_client()) == NULL) {
			error("tls_client(): %s", tls_error(ctx));
			goto fail;
		}

		if (tls_configure(ctx, conf) == -1) {
			error("tls_configure(): %s", tls_error(ctx));
			goto fail;
		}
	}

	if ((ret = getaddrinfo(e->server, e->port, NULL, &ai)) != 0 || ai == NULL) {
		error("could not lookup %s:%s", e->server, e->port);
		goto fail;
	}

	if ((fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1 ||
			connect(fd, ai->ai_addr, ai->ai_addrlen) == -1) {
		error("could not connect to %s:%s", e->server, e->port);
		goto fail;
	}

	if (tls) {
		if (tls_connect_socket(ctx, fd, e->server) == -1) {
			error("could not tls-ify connection to %s:%s", e->server, e->port);
			goto fail;
		}

		if (tls_handshake(ctx) == -1) {
			error("could not perform tls handshake with %s:%s", e->server, e->port);
			goto fail;
		}
	}

	freeaddrinfo(ai);
	return 0;

fail:
	if (ai)
		freeaddrinfo(ai);
	if (ctx) {
		tls_free(ctx);
		ctx = NULL;
	}
	if (conf) {
		tls_config_free(conf);
		conf = NULL;
	}
	return -1;
}

int
net_read(void *buf, size_t count) {
	int ret;

	if (tls) {
		do {
			ret = tls_read(ctx, buf, count);
		} while (ret == TLS_WANT_POLLIN || ret == TLS_WANT_POLLOUT);
		if (ret == -1)
			error("tls_read(): %s", tls_error(ctx));
	} else {
		ret = read(fd, buf, count);
	}

	return ret;
}

int
net_write(void *buf, size_t count) {
	int ret;

	if (tls) {
		while (count > 0) {
			switch (ret = tls_write(ctx, buf, count)) {
			case TLS_WANT_POLLIN:
			case TLS_WANT_POLLOUT:
				break;
			case -1:
				error("tls_write(): %s", tls_error(ctx));
				break;
			default:
				buf += ret;
				count -= ret;
			}
		}
	} else {
		ret = write(fd, buf, count);
	}

	return ret;
}

int
net_close(void) {
	int ret;

	if (tls) {
		do {
			ret = tls_close(ctx);
		} while (ret == TLS_WANT_POLLIN || TLS_WANT_POLLOUT);
		tls_free(ctx);
		ctx = NULL;
		tls_config_free(conf);
		conf = NULL;
	}

	return close(fd);
}
