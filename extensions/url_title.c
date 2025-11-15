/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * url_title.c: Fetch and display URL titles
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
#include "match.h"
#include "dns.h"
#include "rb_commio.h"

static const char url_title_desc[] = "Fetches and displays URL titles from messages";

#define URL_TITLE_RATE_LIMIT 5  /* Max URLs per minute per user */
#define URL_TITLE_RATE_WINDOW 60

struct url_rate_limit {
	struct Client *client;
	time_t window_start;
	int count;
	rb_dlink_node node;
};

static rb_dlink_list url_rate_limits;
static struct ev_entry *url_rate_cleanup_ev;

static void hook_privmsg_channel(void *);
static void hook_privmsg_user(void *);
static void url_rate_cleanup(void *);
static bool check_url_rate_limit(struct Client *client_p);

mapi_hfn_list_av1 url_title_hfnlist[] = {
	{ "privmsg_channel", hook_privmsg_channel },
	{ "privmsg_user", hook_privmsg_user },
	{ NULL, NULL }
};

#define MAX_URL_LEN 512
#define MAX_TITLE_LEN 256
#define MAX_RESPONSE_LEN 8192

struct url_request {
	struct Client *source_p;
	struct Channel *chptr;
	char url[MAX_URL_LEN];
	char host[256];
	char path[512];
	int port;
	rb_fde_t *fd;
	char response_buf[MAX_RESPONSE_LEN];
	size_t response_len;
	uint32_t dns_req;
	bool is_https;
};

static void url_dns_callback(const char *res, int status, int aftype, void *data);
static void url_connect_callback(rb_fde_t *F, int status, void *data);
static void url_read_callback(rb_fde_t *F, void *data);
static void url_timeout_callback(rb_fde_t *F, void *data);
static bool extract_url(const char *text, char *url, size_t url_len);
static void parse_url(const char *url, char *host, size_t host_len, char *path, size_t path_len, int *port, bool *is_https);
static char *extract_title_from_html(const char *html, size_t html_len);

static void
url_timeout_callback(rb_fde_t *F, void *data)
{
	struct url_request *req = data;
	
	if (req->dns_req != 0) {
		cancel_lookup(req->dns_req);
		req->dns_req = 0;
	}
	
	if (req->fd != NULL) {
		rb_settimeout(req->fd, 0, NULL, NULL);
		rb_close(req->fd);
		req->fd = NULL;
	}
	
	rb_free(req);
}

static void
url_dns_callback(const char *res, int status, int aftype, void *data)
{
	struct url_request *req = data;
	struct sockaddr_storage addr;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	
	req->dns_req = 0;
	
	if (status == 0 || res == NULL) {
		rb_free(req);
		return;
	}
	
	memset(&addr, 0, sizeof(addr));
	
	if (aftype == AF_INET6) {
		sin6 = (struct sockaddr_in6 *)&addr;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = htons(req->port);
		if (rb_inet_pton(AF_INET6, res, &sin6->sin6_addr) <= 0) {
			rb_free(req);
			return;
		}
	} else {
		sin = (struct sockaddr_in *)&addr;
		sin->sin_family = AF_INET;
		sin->sin_port = htons(req->port);
		if (rb_inet_pton(AF_INET, res, &sin->sin_addr) <= 0) {
			rb_free(req);
			return;
		}
	}
	
	req->fd = rb_socket(GET_SS_FAMILY(&addr), SOCK_STREAM, IPPROTO_TCP, "url_title");
	if (req->fd == NULL) {
		rb_free(req);
		return;
	}
	
	if (req->is_https) {
		/* For HTTPS, we'd need SSL support - for now, skip HTTPS URLs */
		rb_close(req->fd);
		rb_free(req);
		return;
	}
	
	rb_connect_tcp(req->fd, (struct sockaddr *)&addr, NULL, url_connect_callback, req, 10);
}

static void
url_connect_callback(rb_fde_t *F, int status, void *data)
{
	struct url_request *req = data;
	char request[1024];
	
	if (status != RB_OK) {
		rb_close(F);
		rb_free(req);
		return;
	}
	
	snprintf(request, sizeof(request),
		"GET %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"User-Agent: FoxComet-IRCD/1.0\r\n"
		"Connection: close\r\n"
		"\r\n",
		req->path, req->host);
	
	if (rb_write(F, request, strlen(request)) != (ssize_t)strlen(request)) {
		rb_close(F);
		rb_free(req);
		return;
	}
	
	req->response_len = 0;
	req->fd = F;
	rb_settimeout(F, 15, url_timeout_callback, req);
	rb_setselect(F, RB_SELECT_READ, url_read_callback, req);
}

static char *
extract_title_from_html(const char *html, size_t html_len)
{
	const char *title_start, *title_end;
	char *title;
	size_t title_len;
	
	/* Look for <title> tag (case-insensitive) */
	title_start = html;
	while ((title_start = strcasestr(title_start, "<title")) != NULL) {
		title_start = strchr(title_start, '>');
		if (title_start == NULL)
			break;
		title_start++; /* Skip '>' */
		
		title_end = strcasestr(title_start, "</title>");
		if (title_end == NULL)
			break;
		
		title_len = title_end - title_start;
		if (title_len > MAX_TITLE_LEN - 1)
			title_len = MAX_TITLE_LEN - 1;
		
		title = rb_malloc(title_len + 1);
		memcpy(title, title_start, title_len);
		title[title_len] = '\0';
		
		/* Decode HTML entities (basic) */
		/* Remove extra whitespace */
		while (title_len > 0 && (title[title_len - 1] == ' ' || title[title_len - 1] == '\n' || title[title_len - 1] == '\r' || title[title_len - 1] == '\t')) {
			title[title_len - 1] = '\0';
			title_len--;
		}
		
		return title;
	}
	
	return NULL;
}

static void
url_read_callback(rb_fde_t *F, void *data)
{
	struct url_request *req = data;
	ssize_t n;
	char *title;
	char *body_start;
	
	n = rb_read(F, req->response_buf + req->response_len, sizeof(req->response_buf) - req->response_len - 1);
	if (n <= 0) {
		rb_settimeout(F, 0, NULL, NULL);
		rb_close(F);
		req->fd = NULL;
		
		req->response_buf[req->response_len] = '\0';
		
		/* Find HTTP body (after \r\n\r\n) */
		body_start = strstr(req->response_buf, "\r\n\r\n");
		if (body_start == NULL)
			body_start = strstr(req->response_buf, "\n\n");
		
		if (body_start != NULL) {
			body_start += (strstr(req->response_buf, "\r\n\r\n") != NULL) ? 4 : 2;
			title = extract_title_from_html(body_start, req->response_len - (body_start - req->response_buf));
			
			if (title != NULL) {
				char msg[512];
				snprintf(msg, sizeof(msg), ":*** URL Title: %s", title);
				
				if (req->chptr != NULL) {
					sendto_channel_local(ALL_MEMBERS, req->chptr,
						":%s NOTICE %s %s", me.name, req->chptr->chname, msg);
				} else {
					sendto_one_notice(req->source_p, "%s", msg);
				}
				
				rb_free(title);
			}
		}
		
		rb_free(req);
		return;
	}
	
	req->response_len += n;
	if (req->response_len >= sizeof(req->response_buf) - 1) {
		rb_settimeout(F, 0, NULL, NULL);
		rb_close(F);
		req->fd = NULL;
		rb_free(req);
	}
}

static void
parse_url(const char *url, char *host, size_t host_len, char *path, size_t path_len, int *port, bool *is_https)
{
	const char *p, *host_start, *host_end, *path_start;
	
	*is_https = false;
	*port = 80;
	
	if (strncasecmp(url, "https://", 8) == 0) {
		*is_https = true;
		*port = 443;
		host_start = url + 8;
	} else if (strncasecmp(url, "http://", 7) == 0) {
		host_start = url + 7;
	} else {
		return;
	}
	
	/* Find end of hostname (:/ or / or end) */
	host_end = host_start;
	while (*host_end && *host_end != '/' && *host_end != ':')
		host_end++;
	
	/* Check for port */
	if (*host_end == ':') {
		*port = atoi(host_end + 1);
		path_start = strchr(host_end + 1, '/');
	} else {
		path_start = host_end;
	}
	
	if (path_start == NULL || *path_start == '\0') {
		strncpy(path, "/", path_len);
		path[path_len - 1] = '\0';
	} else {
		size_t path_len_calc = strlen(path_start);
		if (path_len_calc >= path_len)
			path_len_calc = path_len - 1;
		memcpy(path, path_start, path_len_calc);
		path[path_len_calc] = '\0';
	}
	
	/* Copy hostname */
	size_t host_len_calc = host_end - host_start;
	if (host_len_calc >= host_len)
		host_len_calc = host_len - 1;
	memcpy(host, host_start, host_len_calc);
	host[host_len_calc] = '\0';
}

static bool
extract_url(const char *text, char *url, size_t url_len)
{
	const char *start = NULL, *end;
	const char *p;
	
	/* Find http:// or https:// (case-insensitive) */
	p = text;
	while (*p) {
		if ((strncasecmp(p, "http://", 7) == 0) || (strncasecmp(p, "https://", 8) == 0)) {
			start = p;
			break;
		}
		p++;
	}
	if (start == NULL)
		return false;
	
	/* Find end of URL (space, newline, or end of string) */
	end = start;
	while (*end && *end != ' ' && *end != '\n' && *end != '\r' && *end != '\t' && *end != ')' && *end != ']' && *end != '}')
		end++;
	
	/* Copy URL */
	if (end - start >= url_len)
		return false;
	
	memcpy(url, start, end - start);
	url[end - start] = '\0';
	return true;
}

static bool
check_url_rate_limit(struct Client *client_p)
{
	rb_dlink_node *ptr;
	struct url_rate_limit *limit;
	time_t now = rb_current_time();
	
	if (!MyClient(client_p) || IsOper(client_p))
		return true;
	
	/* Find or create rate limit entry */
	RB_DLINK_FOREACH(ptr, url_rate_limits.head) {
		limit = ptr->data;
		if (limit->client == client_p) {
			/* Reset window if expired */
			if (now - limit->window_start > URL_TITLE_RATE_WINDOW) {
				limit->window_start = now;
				limit->count = 0;
			}
			
			if (limit->count >= URL_TITLE_RATE_LIMIT) {
				return false;
			}
			
			limit->count++;
			return true;
		}
	}
	
	/* Create new entry */
	limit = rb_malloc(sizeof(struct url_rate_limit));
	limit->client = client_p;
	limit->window_start = now;
	limit->count = 1;
	rb_dlinkAdd(limit, &limit->node, &url_rate_limits);
	return true;
}

static void
url_rate_cleanup(void *unused)
{
	rb_dlink_node *ptr, *next;
	time_t now = rb_current_time();
	
	RB_DLINK_FOREACH_SAFE(ptr, next, url_rate_limits.head) {
		struct url_rate_limit *limit = ptr->data;
		if (now - limit->window_start > URL_TITLE_RATE_WINDOW * 2) {
			rb_dlinkDelete(ptr, &url_rate_limits);
			rb_free(limit);
		}
	}
}

static void
hook_privmsg_channel(void *data_)
{
	hook_data_privmsg_channel *data = data_;
	char url[MAX_URL_LEN];
	struct url_request *req;
	
	if (data->msgtype != MESSAGE_TYPE_PRIVMSG)
		return;
	
	/* Check rate limit */
	if (!check_url_rate_limit(data->source_p))
		return;
	
	/* Extract URL from message */
	if (!extract_url(data->text, url, sizeof(url)))
		return;
	
	/* Skip HTTPS for now (would need SSL support) */
	if (strncasecmp(url, "https://", 8) == 0)
		return;
	
	req = rb_malloc(sizeof(struct url_request));
	req->source_p = data->source_p;
	req->chptr = data->chptr;
	rb_strlcpy(req->url, url, sizeof(req->url));
	req->fd = NULL;
	req->response_len = 0;
	req->dns_req = 0;
	
	parse_url(url, req->host, sizeof(req->host), req->path, sizeof(req->path), &req->port, &req->is_https);
	
	if (req->host[0] == '\0') {
		rb_free(req);
		return;
	}
	
	/* Start DNS lookup */
	req->dns_req = lookup_hostname(req->host, AF_INET, url_dns_callback, req);
	if (req->dns_req == 0) {
		rb_free(req);
		return;
	}
	
	/* Timeout will be handled by DNS and connection callbacks */
}

static void
hook_privmsg_user(void *data_)
{
	hook_data_privmsg_user *data = data_;
	char url[MAX_URL_LEN];
	struct url_request *req;
	
	if (data->msgtype != MESSAGE_TYPE_PRIVMSG)
		return;
	
	if (!extract_url(data->text, url, sizeof(url)))
		return;
	
	/* Skip HTTPS for now */
	if (strncasecmp(url, "https://", 8) == 0)
		return;
	
	req = rb_malloc(sizeof(struct url_request));
	req->source_p = data->source_p;
	req->chptr = NULL;
	rb_strlcpy(req->url, url, sizeof(req->url));
	req->fd = NULL;
	req->response_len = 0;
	req->dns_req = 0;
	
	parse_url(url, req->host, sizeof(req->host), req->path, sizeof(req->path), &req->port, &req->is_https);
	
	if (req->host[0] == '\0') {
		rb_free(req);
		return;
	}
	
	/* Start DNS lookup */
	req->dns_req = lookup_hostname(req->host, AF_INET, url_dns_callback, req);
	if (req->dns_req == 0) {
		rb_free(req);
		return;
	}
	
	/* Timeout will be handled by DNS and connection callbacks */
}

DECLARE_MODULE_AV2(url_title, NULL, NULL, NULL, NULL, url_title_hfnlist, NULL, NULL, url_title_desc);
