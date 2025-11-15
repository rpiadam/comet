/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * webhook.c: Webhook notifications for IRC events
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
#include "hash.h"
#include "dns.h"
#include <rb_lib.h>
#include <rb_commio.h>

static const char webhook_desc[] = "Webhook notifications for IRC events";

/* Webhook configuration */
struct webhook_config {
	char *url;
	rb_dlink_node node;
};

static rb_dlink_list webhook_urls;
static bool webhooks_enabled = false;

/* Webhook request state */
struct webhook_request {
	char *url;
	char *payload;
	uint32_t dns_req;
	uint32_t dns_req_v4;
	bool tried_ipv6;
	rb_fde_t *fd;
	char host[256];
	int port;
	bool is_https;
	char path[512];
};

/* Forward declarations */
static void webhook_dns_callback(const char *res, int status, int aftype, void *data);
static void webhook_connect_callback(rb_fde_t *F, int status, void *data);
static void webhook_write_callback(rb_fde_t *F, void *data);
static void webhook_timeout_callback(rb_fde_t *F, void *data);

/* Parse webhook URL */
static int
parse_webhook_url(const char *url, char *host, size_t hostlen, int *port, bool *is_https, char *path, size_t pathlen)
{
	const char *p;
	char *colon, *slash;

	if (url == NULL || host == NULL || port == NULL || is_https == NULL || path == NULL)
		return -1;

	*is_https = false;
	*port = 80;

	if (strncasecmp(url, "https://", 8) == 0) {
		*is_https = true;
		*port = 443;
		p = url + 8;
	} else if (strncasecmp(url, "http://", 7) == 0) {
		p = url + 7;
	} else {
		return -1;
	}

	/* Find host:port/path */
	colon = strchr(p, ':');
	slash = strchr(p, '/');

	if (colon != NULL && (slash == NULL || colon < slash)) {
		/* Has port */
		size_t host_len = colon - p;
		if (host_len >= hostlen)
			return -1;
		memcpy(host, p, host_len);
		host[host_len] = '\0';
		*port = atoi(colon + 1);
		if (*port <= 0 || *port > 65535)
			return -1;
		p = strchr(colon, '/');
		if (p == NULL)
			p = colon + strlen(colon);
	} else if (slash != NULL) {
		/* No port, has path */
		size_t host_len = slash - p;
		if (host_len >= hostlen)
			return -1;
		memcpy(host, p, host_len);
		host[host_len] = '\0';
		p = slash;
	} else {
		/* No port, no path */
		if (strlen(p) >= hostlen)
			return -1;
		rb_strlcpy(host, p, hostlen);
		rb_strlcpy(path, "/", pathlen);
		return 0;
	}

	/* Extract path */
	if (p != NULL) {
		if (strlen(p) >= pathlen)
			return -1;
		rb_strlcpy(path, p, pathlen);
	} else {
		rb_strlcpy(path, "/", pathlen);
	}

	return 0;
}

/* Send webhook notification */
static void
send_webhook_notification(const char *event_type, const char *json_payload)
{
	rb_dlink_node *ptr;
	struct webhook_request *req;

	if (!webhooks_enabled || rb_dlink_list_length(&webhook_urls) == 0)
		return;

	RB_DLINK_FOREACH(ptr, webhook_urls.head) {
		struct webhook_config *config = ptr->data;
		char host[256];
		int port;
		bool is_https;
		char path[512];

		if (parse_webhook_url(config->url, host, sizeof(host), &port, &is_https, path, sizeof(path)) < 0)
			continue;

		req = rb_malloc(sizeof(struct webhook_request));
		req->url = rb_strdup(config->url);
		req->payload = rb_strdup(json_payload);
		req->dns_req = 0;
		req->dns_req_v4 = 0;
		req->tried_ipv6 = false;
		req->fd = NULL;
		rb_strlcpy(req->host, host, sizeof(req->host));
		req->port = port;
		req->is_https = is_https;
		rb_strlcpy(req->path, path, sizeof(req->path));

		/* Start DNS lookup */
		req->dns_req = lookup_hostname(req->host, AF_INET6, webhook_dns_callback, req);
		if (req->dns_req == 0) {
			req->dns_req_v4 = lookup_hostname(req->host, AF_INET, webhook_dns_callback, req);
			if (req->dns_req_v4 == 0) {
				rb_free(req->url);
				rb_free(req->payload);
				rb_free(req);
				continue;
			}
		}
	}
}

static void
webhook_timeout_callback(rb_fde_t *F, void *data)
{
	struct webhook_request *req = data;
	rb_close(F);
	if (req->dns_req != 0)
		cancel_lookup(req->dns_req);
	if (req->dns_req_v4 != 0)
		cancel_lookup(req->dns_req_v4);
	rb_free(req->url);
	rb_free(req->payload);
	rb_free(req);
}

static void
webhook_write_callback(rb_fde_t *F, void *data)
{
	struct webhook_request *req = data;
	/* Request sent, close connection */
	rb_settimeout(F, 5, webhook_timeout_callback, req);
	rb_setselect(F, RB_SELECT_READ, NULL, NULL);
}

static void
webhook_connect_callback(rb_fde_t *F, int status, void *data)
{
	struct webhook_request *req = data;
	char http_request[4096];
	size_t len;

	if (status != RB_OK) {
		rb_close(F);
		if (req->dns_req != 0)
			cancel_lookup(req->dns_req);
		if (req->dns_req_v4 != 0)
			cancel_lookup(req->dns_req_v4);
		rb_free(req->url);
		rb_free(req->payload);
		rb_free(req);
		return;
	}

	req->fd = F;
	rb_settimeout(F, 10, webhook_timeout_callback, req);

	/* Build HTTP POST request */
	len = snprintf(http_request, sizeof(http_request),
		"POST %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %zu\r\n"
		"Connection: close\r\n"
		"\r\n"
		"%s",
		req->path, req->host, strlen(req->payload), req->payload);

	rb_setselect(F, RB_SELECT_WRITE, webhook_write_callback, req);
	rb_write(F, http_request, len);
}

static void
webhook_dns_callback(const char *res, int status, int aftype, void *data)
{
	struct webhook_request *req = data;
	struct rb_sockaddr_storage addr;
	rb_fde_t *F;
	int family;

	if (aftype == AF_INET6) {
		req->dns_req = 0;
		req->tried_ipv6 = true;
	} else {
		req->dns_req_v4 = 0;
	}

	if (status == 0 || res == NULL) {
		if (aftype == AF_INET6) {
			req->dns_req_v4 = lookup_hostname(req->host, AF_INET, webhook_dns_callback, req);
			if (req->dns_req_v4 != 0)
				return;
		}
		rb_free(req->url);
		rb_free(req->payload);
		rb_free(req);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	family = (aftype == AF_INET6) ? AF_INET6 : AF_INET;
	SET_SS_FAMILY(&addr, family);
	SET_SS_LEN(&addr, (family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in));

	if (family == AF_INET6) {
		if (rb_inet_pton(AF_INET6, res, &((struct sockaddr_in6 *)&addr)->sin6_addr) <= 0) {
			if (req->dns_req_v4 == 0) {
				req->dns_req_v4 = lookup_hostname(req->host, AF_INET, webhook_dns_callback, req);
				if (req->dns_req_v4 != 0)
					return;
			}
			rb_free(req->url);
			rb_free(req->payload);
			rb_free(req);
			return;
		}
		((struct sockaddr_in6 *)&addr)->sin6_port = htons(req->port);
	} else {
		if (rb_inet_pton(AF_INET, res, &((struct sockaddr_in *)&addr)->sin_addr) <= 0) {
			rb_free(req->url);
			rb_free(req->payload);
			rb_free(req);
			return;
		}
		((struct sockaddr_in *)&addr)->sin_port = htons(req->port);
	}

	F = rb_socket(family, SOCK_STREAM, IPPROTO_TCP, "Webhook HTTP");
	if (F == NULL) {
		rb_free(req->url);
		rb_free(req->payload);
		rb_free(req);
		return;
	}

	if (req->is_https && rb_supports_ssl()) {
		rb_connect_tcp_ssl(F, (struct sockaddr *)&addr, NULL, webhook_connect_callback, req, 10);
	} else if (!req->is_https) {
		rb_connect_tcp(F, (struct sockaddr *)&addr, NULL, webhook_connect_callback, req, 10);
	} else {
		rb_close(F);
		rb_free(req->url);
		rb_free(req->payload);
		rb_free(req);
	}
}

/* Hook functions */
static void
hook_privmsg_channel_webhook(void *data_)
{
	hook_data_privmsg_channel *data = data_;
	char json[1024];

	if (data->msgtype != MESSAGE_TYPE_PRIVMSG)
		return;

	snprintf(json, sizeof(json),
		"{\"event\":\"message\",\"channel\":\"%s\",\"nick\":\"%s\",\"text\":\"%s\"}",
		data->chptr->chname, data->source_p->name, data->text);
	send_webhook_notification("message", json);
}

static void
hook_channel_join_webhook(void *data_)
{
	hook_data_channel_activity *data = data_;
	char json[512];

	if (!MyClient(data->client))
		return;

	snprintf(json, sizeof(json),
		"{\"event\":\"join\",\"channel\":\"%s\",\"nick\":\"%s\"}",
		data->chptr->chname, data->client->name);
	send_webhook_notification("join", json);
}

static void
hook_channel_part_webhook(void *data_)
{
	hook_data_channel_activity *data = data_;
	char json[512];

	if (!MyClient(data->client))
		return;

	snprintf(json, sizeof(json),
		"{\"event\":\"part\",\"channel\":\"%s\",\"nick\":\"%s\"}",
		data->chptr->chname, data->client->name);
	send_webhook_notification("part", json);
}

mapi_hfn_list_av1 webhook_hfnlist[] = {
	{ "privmsg_channel", hook_privmsg_channel_webhook },
	{ "channel_join", hook_channel_join_webhook },
	{ "channel_part", hook_channel_part_webhook },
	{ NULL, NULL }
};

/* Add webhook URL */
static void
add_webhook_url(const char *url)
{
	struct webhook_config *config;

	if (url == NULL || strlen(url) == 0)
		return;

	config = rb_malloc(sizeof(struct webhook_config));
	config->url = rb_strdup(url);
	rb_dlinkAdd(config, &config->node, &webhook_urls);
	webhooks_enabled = true;
}

static int
modinit(void)
{
	const char *webhook_url;

	/* Load webhook URL from environment variable */
	webhook_url = getenv("WEBHOOK_URL");
	if (webhook_url != NULL && strlen(webhook_url) > 0) {
		add_webhook_url(webhook_url);
		ilog(L_MAIN, "webhook: Webhook URL configured from environment");
	}

	return 0;
}

static void
moddeinit(void)
{
	rb_dlink_node *ptr, *next;

	RB_DLINK_FOREACH_SAFE(ptr, next, webhook_urls.head) {
		struct webhook_config *config = ptr->data;
		rb_dlinkDelete(ptr, &webhook_urls);
		rb_free(config->url);
		rb_free(config);
	}
}

DECLARE_MODULE_AV2(webhook, modinit, moddeinit, NULL, NULL, webhook_hfnlist, NULL, NULL, webhook_desc);

