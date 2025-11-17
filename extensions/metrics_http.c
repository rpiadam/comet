/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * metrics_http.c: HTTP endpoint for Prometheus metrics export
 *
 * Copyright (c) 2024
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 */

#include "stdinc.h"
#include "client.h"
#include "channel.h"
#include "ircd.h"
#include "send.h"
#include "s_user.h"
#include "modules.h"
#include "hook.h"
#include "s_serv.h"
#include "s_stats.h"
#include "hash.h"
#include <rb_lib.h>
#include <rb_commio.h>

static const char metrics_http_desc[] = "HTTP endpoint for Prometheus metrics export";

/* HTTP server state */
static rb_fde_t *metrics_listener = NULL;
static int metrics_port = 9090;
static bool metrics_enabled = false;

/* HTTP connection state */
struct http_connection {
	rb_fde_t *fd;
	char *buffer;
	size_t buffer_size;
	size_t buffer_pos;
	bool headers_sent;
};

/* Forward declarations */
static void metrics_http_accept_callback(rb_fde_t *F, int status, struct sockaddr *addr, rb_socklen_t len, void *data);
static int metrics_http_accept_precallback(rb_fde_t *F, struct sockaddr *addr, rb_socklen_t len, void *data);
static void metrics_http_read_callback(rb_fde_t *F, void *data);
static void metrics_http_timeout_callback(rb_fde_t *F, void *data);

/* External references to metrics from metrics.c */
extern struct server_metrics metrics;
extern rb_dictionary *channel_metrics_dict;

/* Forward declaration */
struct channel_metrics {
	unsigned long messages;
	unsigned long joins;
	unsigned long parts;
	unsigned long unique_users;
	time_t created;
	time_t last_activity;
	rb_dlink_list active_users;
	struct Channel *chptr;
};

/* Generate Prometheus metrics output */
static void
generate_prometheus_metrics(struct http_connection *conn)
{
	char response[8192];
	size_t pos = 0;

	/* HTTP headers */
	pos += snprintf(response + pos, sizeof(response) - pos,
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/plain; version=0.0.4\r\n"
		"Connection: close\r\n"
		"\r\n");

	/* Server metrics */
	pos += snprintf(response + pos, sizeof(response) - pos,
		"# HELP ircd_users_total Total number of users\n"
		"# TYPE ircd_users_total gauge\n"
		"ircd_users_total %lu\n"
		"\n",
		ServerStats.is_cl);

	pos += snprintf(response + pos, sizeof(response) - pos,
		"# HELP ircd_channels_total Total number of channels\n"
		"# TYPE ircd_channels_total gauge\n"
		"ircd_channels_total %lu\n"
		"\n",
		rb_dlink_list_length(&global_channel_list));

	pos += snprintf(response + pos, sizeof(response) - pos,
		"# HELP ircd_connections_total Total number of connections\n"
		"# TYPE ircd_connections_total counter\n"
		"ircd_connections_total %lu\n"
		"\n",
		metrics.connections);

	pos += snprintf(response + pos, sizeof(response) - pos,
		"# HELP ircd_messages_total Total number of messages\n"
		"# TYPE ircd_messages_total counter\n"
		"ircd_messages_total %lu\n"
		"\n",
		metrics.messages);

	pos += snprintf(response + pos, sizeof(response) - pos,
		"# HELP ircd_uptime_seconds Server uptime in seconds\n"
		"# TYPE ircd_uptime_seconds gauge\n"
		"ircd_uptime_seconds %.0f\n"
		"\n",
		(double)(rb_current_time() - me.serv->boot_time));

	/* Channel metrics */
	if (channel_metrics_dict != NULL) {
		rb_dictionary_iter iter;
		struct channel_metrics *chm;
		RB_DICTIONARY_FOREACH(chm, &iter, channel_metrics_dict) {
			if (chm->chptr != NULL) {
				pos += snprintf(response + pos, sizeof(response) - pos,
					"# HELP ircd_channel_messages_total Total messages in channel\n"
					"# TYPE ircd_channel_messages_total counter\n"
					"ircd_channel_messages_total{channel=\"%s\"} %lu\n"
					"\n",
					chm->chptr->chname, chm->messages);
			}
		}
	}

	/* Send response */
	rb_write(conn->fd, response, pos);
	rb_close(conn->fd);
	rb_free(conn->buffer);
	rb_free(conn);
}

static void
metrics_http_read_callback(rb_fde_t *F, void *data)
{
	struct http_connection *conn = data;
	ssize_t n;

	if (conn->buffer_pos >= conn->buffer_size - 1) {
		rb_close(F);
		rb_free(conn->buffer);
		rb_free(conn);
		return;
	}

	n = rb_read(F, conn->buffer + conn->buffer_pos, conn->buffer_size - conn->buffer_pos - 1);
	if (n <= 0) {
		rb_close(F);
		rb_free(conn->buffer);
		rb_free(conn);
		return;
	}

	conn->buffer_pos += n;
	conn->buffer[conn->buffer_pos] = '\0';

	/* Check if we have a complete HTTP request */
	if (strstr(conn->buffer, "\r\n\r\n") != NULL || strstr(conn->buffer, "\n\n") != NULL) {
		/* Check if it's a GET request to /metrics */
		if (strncmp(conn->buffer, "GET /metrics", 12) == 0 ||
		    strncmp(conn->buffer, "GET /metrics/", 13) == 0) {
			generate_prometheus_metrics(conn);
		} else {
			/* 404 Not Found */
			const char *response = "HTTP/1.1 404 Not Found\r\n"
					       "Content-Type: text/plain\r\n"
					       "Connection: close\r\n"
					       "\r\n"
					       "404 Not Found\r\n";
			rb_write(F, response, strlen(response));
			rb_close(F);
			rb_free(conn->buffer);
			rb_free(conn);
		}
	}
}

static void
metrics_http_timeout_callback(rb_fde_t *F, void *data)
{
	struct http_connection *conn = data;
	rb_close(F);
	rb_free(conn->buffer);
	rb_free(conn);
}

static int
metrics_http_accept_precallback(rb_fde_t *F, struct sockaddr *addr, rb_socklen_t len, void *data)
{
	(void)F;
	(void)addr;
	(void)len;
	(void)data;
	return 1; /* Accept all connections */
}

static void
metrics_http_accept_callback(rb_fde_t *F, int status, struct sockaddr *addr, rb_socklen_t len, void *data)
{
	struct http_connection *conn;

	(void)addr;
	(void)len;
	(void)data;

	if (status != RB_OK) {
		rb_close(F);
		return;
	}

	conn = rb_malloc(sizeof(struct http_connection));
	conn->fd = F;
	conn->buffer_size = 4096;
	conn->buffer = rb_malloc(conn->buffer_size);
	conn->buffer_pos = 0;
	conn->headers_sent = false;

	rb_set_nb(F);
	rb_settimeout(F, 10, metrics_http_timeout_callback, conn);
	rb_setselect(F, RB_SELECT_READ, metrics_http_read_callback, conn);
}

static int
modinit(void)
{
	struct rb_sockaddr_storage addr;
	const char *port_env;

	/* Check if metrics HTTP is enabled via environment variable */
	port_env = getenv("METRICS_HTTP_PORT");
	if (port_env != NULL && strlen(port_env) > 0) {
		metrics_port = atoi(port_env);
		if (metrics_port <= 0 || metrics_port > 65535)
			metrics_port = 9090;
		metrics_enabled = true;
	} else {
		/* Default: disabled unless explicitly enabled */
		metrics_enabled = false;
		return 0;
	}

	/* Create socket */
	memset(&addr, 0, sizeof(addr));
	SET_SS_FAMILY(&addr, AF_INET);
	SET_SS_LEN(&addr, sizeof(struct sockaddr_in));
	((struct sockaddr_in *)&addr)->sin_addr.s_addr = INADDR_ANY;
	((struct sockaddr_in *)&addr)->sin_port = htons(metrics_port);

	metrics_listener = rb_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP, "Metrics HTTP");
	if (metrics_listener == NULL) {
		ilog(L_MAIN, "metrics_http: Failed to create socket");
		return -1;
	}

	if (rb_bind(metrics_listener, (struct sockaddr *)&addr) < 0) {
		ilog(L_MAIN, "metrics_http: Failed to bind to port %d", metrics_port);
		rb_close(metrics_listener);
		metrics_listener = NULL;
		return -1;
	}

	if (rb_listen(metrics_listener, 10, 0) < 0) {
		ilog(L_MAIN, "metrics_http: Failed to listen on port %d", metrics_port);
		rb_close(metrics_listener);
		metrics_listener = NULL;
		return -1;
	}

	rb_accept_tcp(metrics_listener, metrics_http_accept_precallback, metrics_http_accept_callback, NULL);
	ilog(L_MAIN, "metrics_http: Listening on port %d for Prometheus metrics", metrics_port);

	return 0;
}

static void
moddeinit(void)
{
	if (metrics_listener != NULL) {
		rb_close(metrics_listener);
		metrics_listener = NULL;
	}
}

DECLARE_MODULE_AV2(metrics_http, modinit, moddeinit, NULL, NULL, NULL, NULL, NULL, metrics_http_desc);

