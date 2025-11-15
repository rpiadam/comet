/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_football.c: FOOTBALL command for football/soccer news
 *
 * Copyright (c) 2024
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 */

#include "stdinc.h"
#include "client.h"
#include "ircd.h"
#include "send.h"
#include "s_user.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "numeric.h"
#include "channel.h"
#include "dns.h"
#include "rb_commio.h"
#include "hook.h"

static const char football_desc[] = "Provides the FOOTBALL command for football/soccer news";

static void m_football(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void football_news_update(void *);

struct Message football_msgtab = {
	"FOOTBALL", 0, 0, 0, 0,
	{mg_ignore, {m_football, 1}, mg_ignore, mg_ignore, mg_ignore, {m_football, 1}}
};

mapi_clist_av1 football_clist[] = { &football_msgtab, NULL };

/* Football API configuration */
static char *football_api_key = NULL;
static char *football_api_url = "api.football-data.org";
static int football_api_port = 443;
static bool football_api_use_ssl = true;
static struct ev_entry *football_news_ev = NULL;

/* Channel subscriptions for automatic news updates */
struct football_channel {
	char *channel;
	char *league;
	char *team;
	rb_dlink_node node;
};

static rb_dlink_list football_channels;
static time_t last_news_check = 0;
#define FOOTBALL_NEWS_INTERVAL 300  /* Check every 5 minutes */

struct football_request {
	struct Client *source_p;
	struct Channel *chptr;
	char *query;
	rb_fde_t *fd;
	char response_buf[8192];
	size_t response_len;
	uint32_t dns_req;
	uint32_t dns_req_v4;
	bool tried_ipv6;
	bool is_news_update;
};

static void football_dns_callback(const char *res, int status, int aftype, void *data);
static void football_connect_callback(rb_fde_t *F, int status, void *data);
static void football_read_callback(rb_fde_t *F, void *data);
static void football_timeout_callback(rb_fde_t *F, void *data);

static void
football_timeout_callback(rb_fde_t *F, void *data)
{
	struct football_request *req = data;
	
	if (req->dns_req != 0) {
		cancel_lookup(req->dns_req);
		req->dns_req = 0;
	}
	
	if (req->dns_req_v4 != 0) {
		cancel_lookup(req->dns_req_v4);
		req->dns_req_v4 = 0;
	}
	
	if (req->fd != NULL) {
		rb_settimeout(req->fd, 0, NULL, NULL);
		rb_close(req->fd);
		req->fd = NULL;
	}
	
	if (!req->is_news_update && req->source_p != NULL) {
		sendto_one_notice(req->source_p, ":*** Football news request timed out");
	}
	
	if (req->query != NULL)
		rb_free(req->query);
	rb_free(req);
}

static void
football_dns_callback(const char *res, int status, int aftype, void *data)
{
	struct football_request *req = data;
	struct sockaddr_storage addr;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	
	if (aftype == AF_INET6) {
		req->dns_req = 0;
		req->tried_ipv6 = true;
	} else {
		req->dns_req_v4 = 0;
	}
	
	if (status == 0 || res == NULL) {
		/* If IPv6 failed, try IPv4 as fallback */
		if (aftype == AF_INET6) {
			req->dns_req_v4 = lookup_hostname(football_api_url, AF_INET, football_dns_callback, req);
			if (req->dns_req_v4 != 0)
				return;
		}
		
		/* Both failed or IPv4 failed */
		if (!req->is_news_update && req->source_p != NULL) {
			sendto_one_notice(req->source_p, ":*** Failed to resolve football API hostname");
		}
		if (req->query != NULL)
			rb_free(req->query);
		rb_free(req);
		return;
	}
	
	memset(&addr, 0, sizeof(addr));
	
	if (aftype == AF_INET6) {
		sin6 = (struct sockaddr_in6 *)&addr;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = htons(football_api_port);
		if (rb_inet_pton(AF_INET6, res, &sin6->sin6_addr) <= 0) {
			if (!req->is_news_update && req->source_p != NULL) {
				sendto_one_notice(req->source_p, ":*** Invalid IPv6 address");
			}
			if (req->query != NULL)
				rb_free(req->query);
			rb_free(req);
			return;
		}
	} else {
		sin = (struct sockaddr_in *)&addr;
		sin->sin_family = AF_INET;
		sin->sin_port = htons(football_api_port);
		if (rb_inet_pton(AF_INET, res, &sin->sin_addr) <= 0) {
			if (!req->is_news_update && req->source_p != NULL) {
				sendto_one_notice(req->source_p, ":*** Invalid IPv4 address");
			}
			if (req->query != NULL)
				rb_free(req->query);
			rb_free(req);
			return;
		}
	}
	
	req->fd = rb_socket(GET_SS_FAMILY(&addr), SOCK_STREAM, IPPROTO_TCP, "football_api");
	if (req->fd == NULL) {
		if (!req->is_news_update && req->source_p != NULL) {
			sendto_one_notice(req->source_p, ":*** Failed to create socket");
		}
		if (req->query != NULL)
			rb_free(req->query);
		rb_free(req);
		return;
	}
	
	if (football_api_use_ssl && rb_supports_ssl()) {
		rb_connect_tcp_ssl(req->fd, (struct sockaddr *)&addr, NULL, football_connect_callback, req, 10);
	} else {
		rb_connect_tcp(req->fd, (struct sockaddr *)&addr, NULL, football_connect_callback, req, 10);
	}
}

static void
football_connect_callback(rb_fde_t *F, int status, void *data)
{
	struct football_request *req = data;
	char request[1024];
	size_t len;
	
	if (status != RB_OK) {
		if (!req->is_news_update && req->source_p != NULL) {
			sendto_one_notice(req->source_p, ":*** Failed to connect to football API");
		}
		rb_close(F);
		if (req->query != NULL)
			rb_free(req->query);
		rb_free(req);
		return;
	}
	
	/* Build API request */
	if (football_api_key != NULL && strlen(football_api_key) > 0) {
		if (req->query != NULL && strlen(req->query) > 0) {
			snprintf(request, sizeof(request),
				"GET /v4/%s HTTP/1.1\r\n"
				"Host: %s\r\n"
				"X-Auth-Token: %s\r\n"
				"Connection: close\r\n"
				"\r\n",
				req->query, football_api_url, football_api_key);
		} else {
			/* Default: get latest news/headlines */
			snprintf(request, sizeof(request),
				"GET /v4/competitions HTTP/1.1\r\n"
				"Host: %s\r\n"
				"X-Auth-Token: %s\r\n"
				"Connection: close\r\n"
				"\r\n",
				football_api_url, football_api_key);
		}
	} else {
		/* Fallback: use free tier endpoint */
		snprintf(request, sizeof(request),
			"GET /v4/competitions HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Connection: close\r\n"
			"\r\n",
			football_api_url);
	}
	
	len = strlen(request);
	if (rb_write(F, request, len) != (ssize_t)len) {
		if (!req->is_news_update && req->source_p != NULL) {
			sendto_one_notice(req->source_p, ":*** Failed to send football request");
		}
		rb_close(F);
		if (req->query != NULL)
			rb_free(req->query);
		rb_free(req);
		return;
	}
	
	req->response_len = 0;
	req->fd = F;
	rb_settimeout(F, 15, football_timeout_callback, req);
	rb_setselect(F, RB_SELECT_READ, football_read_callback, req);
}

static char *
extract_news_from_json(const char *json, size_t json_len, int max_items)
{
	char *news_buf = rb_malloc(1024);
	char *p = news_buf;
	size_t remaining = 1024;
	int items = 0;
	const char *item_start, *item_end;
	const char *title_start, *title_end;
	const char *desc_start, *desc_end;
	
	/* Simple JSON parsing - look for articles or matches */
	item_start = json;
	
	while (items < max_items && remaining > 50) {
		/* Look for title or name field */
		title_start = strstr(item_start, "\"title\":\"");
		if (title_start == NULL)
			title_start = strstr(item_start, "\"name\":\"");
		if (title_start == NULL)
			title_start = strstr(item_start, "\"headline\":\"");
		
		if (title_start == NULL)
			break;
		
		title_start += 9; /* Skip past "title":" or "name":" */
		title_end = strchr(title_start, '"');
		if (title_end == NULL)
			break;
		
		/* Copy title */
		size_t title_len = title_end - title_start;
		if (title_len > remaining - 10)
			title_len = remaining - 10;
		
		if (title_len > 0) {
			memcpy(p, title_start, title_len);
			p[title_len] = '\0';
			
			/* Decode basic JSON escapes */
			char *q = p;
			while (*q) {
				if (*q == '\\' && *(q+1) == 'n') {
					*q = ' ';
					memmove(q+1, q+2, strlen(q+2)+1);
				}
				q++;
			}
			
			p += title_len;
			remaining -= title_len;
			
			if (remaining > 3) {
				*p++ = ';';
				*p++ = ' ';
				remaining -= 2;
			}
		}
		
		items++;
		item_start = title_end;
	}
	
	if (p > news_buf) {
		/* Remove trailing "; " */
		if (p >= news_buf + 2 && p[-2] == ';' && p[-1] == ' ') {
			p -= 2;
			*p = '\0';
		}
		return news_buf;
	}
	
	rb_free(news_buf);
	return NULL;
}

static void
football_read_callback(rb_fde_t *F, void *data)
{
	struct football_request *req = data;
	ssize_t n;
	char *news;
	char response[512];
	
	n = rb_read(F, req->response_buf + req->response_len, sizeof(req->response_buf) - req->response_len - 1);
	if (n <= 0) {
		rb_settimeout(F, 0, NULL, NULL);
		rb_close(F);
		req->fd = NULL;
		
		req->response_buf[req->response_len] = '\0';
		
		/* Find HTTP body (after \r\n\r\n) */
		char *body_start = strstr(req->response_buf, "\r\n\r\n");
		if (body_start == NULL)
			body_start = strstr(req->response_buf, "\n\n");
		
		if (body_start != NULL) {
			body_start += (strstr(req->response_buf, "\r\n\r\n") != NULL) ? 4 : 2;
			news = extract_news_from_json(body_start, req->response_len - (body_start - req->response_buf), 3);
			
			if (news != NULL) {
				snprintf(response, sizeof(response), ":*** Football News: %s", news);
				
				if (req->is_news_update) {
					/* Send to subscribed channels */
					rb_dlink_node *ptr;
					RB_DLINK_FOREACH(ptr, football_channels.head) {
						struct football_channel *fc = ptr->data;
						struct Channel *chptr = find_channel(fc->channel);
						if (chptr != NULL) {
							sendto_channel_local(ALL_MEMBERS, chptr, "%s", response);
						}
					}
				} else if (req->chptr != NULL) {
					sendto_channel_local(ALL_MEMBERS, req->chptr, "%s", response);
				} else if (req->source_p != NULL) {
					sendto_one_notice(req->source_p, "%s", response);
				}
				
				rb_free(news);
			} else {
				/* Try to extract error message */
				char *error_msg = strstr(req->response_buf, "\"message\":\"");
				if (error_msg != NULL) {
					char err[128];
					char *end;
					error_msg += 11;
					end = strchr(error_msg, '"');
					if (end != NULL) {
						size_t len = end - error_msg;
						if (len >= sizeof(err))
							len = sizeof(err) - 1;
						memcpy(err, error_msg, len);
						err[len] = '\0';
						snprintf(response, sizeof(response),
							":*** Football API error: %s", err);
					} else {
						snprintf(response, sizeof(response),
							":*** Unable to parse football news response");
					}
				} else {
					snprintf(response, sizeof(response),
						":*** Unable to parse football news response");
				}
				
				if (!req->is_news_update && req->source_p != NULL) {
					sendto_one_notice(req->source_p, "%s", response);
				}
			}
		} else {
			if (!req->is_news_update && req->source_p != NULL) {
				sendto_one_notice(req->source_p, ":*** Unable to parse football API response");
			}
		}
		
		if (req->query != NULL)
			rb_free(req->query);
		rb_free(req);
		return;
	}
	
	req->response_len += n;
	if (req->response_len >= sizeof(req->response_buf) - 1) {
		rb_settimeout(F, 0, NULL, NULL);
		rb_close(F);
		req->fd = NULL;
		if (req->query != NULL)
			rb_free(req->query);
		rb_free(req);
	}
}

static void
football_news_update(void *unused)
{
	rb_dlink_node *ptr;
	time_t now = rb_current_time();
	
	/* Only check if we have subscribed channels */
	if (rb_dlink_list_length(&football_channels) == 0)
		return;
	
	/* Rate limit: don't check more than once per interval */
	if (now - last_news_check < FOOTBALL_NEWS_INTERVAL)
		return;
	
	last_news_check = now;
	
	/* Create a news update request */
	struct football_request *req = rb_malloc(sizeof(struct football_request));
	req->source_p = NULL;
	req->chptr = NULL;
	req->query = rb_strdup("competitions");
	req->fd = NULL;
	req->response_len = 0;
	req->dns_req = 0;
	req->dns_req_v4 = 0;
	req->tried_ipv6 = false;
	req->is_news_update = true;
	
	/* Start DNS lookup - prefer IPv6, fallback to IPv4 */
	req->tried_ipv6 = false;
	req->dns_req = lookup_hostname(football_api_url, AF_INET6, football_dns_callback, req);
	req->dns_req_v4 = 0;
	
	if (req->dns_req == 0) {
		/* IPv6 lookup failed, try IPv4 immediately */
		req->dns_req_v4 = lookup_hostname(football_api_url, AF_INET, football_dns_callback, req);
		if (req->dns_req_v4 == 0) {
			rb_free(req->query);
			rb_free(req);
			return;
		}
	}
}

static void
m_football(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char *query = NULL;
	struct football_request *req;
	
	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: FOOTBALL <news|league|team|subscribe|unsubscribe> [channel]");
		sendto_one_notice(source_p, ":*** Examples: FOOTBALL news, FOOTBALL news #football");
		sendto_one_notice(source_p, ":*** Subscribe: FOOTBALL subscribe #channel [league] [team]");
		return;
	}

	query = (char *)parv[1];
	
	/* Handle subscription commands */
	if (strcasecmp(query, "subscribe") == 0) {
		if (parc < 3 || EmptyString(parv[2])) {
			sendto_one_notice(source_p, ":*** Syntax: FOOTBALL subscribe <channel> [league] [team]");
			return;
		}
		
		struct Channel *chptr = find_channel(parv[2]);
		if (chptr == NULL) {
			sendto_one_notice(source_p, ":*** Channel %s not found", parv[2]);
			return;
		}
		
		char *league = (parc > 3 && !EmptyString(parv[3])) ? (char *)parv[3] : NULL;
		char *team = (parc > 4 && !EmptyString(parv[4])) ? (char *)parv[4] : NULL;
		
		add_football_channel(chptr->chname, league, team);
		sendto_one_notice(source_p, ":*** Channel %s subscribed to football news updates", chptr->chname);
		return;
	}
	
	if (strcasecmp(query, "unsubscribe") == 0) {
		if (parc < 3 || EmptyString(parv[2])) {
			sendto_one_notice(source_p, ":*** Syntax: FOOTBALL unsubscribe <channel>");
			return;
		}
		
		rb_dlink_node *ptr, *next;
		RB_DLINK_FOREACH_SAFE(ptr, next, football_channels.head) {
			struct football_channel *fc = ptr->data;
			if (strcasecmp(fc->channel, parv[2]) == 0) {
				rb_dlinkDelete(ptr, &football_channels);
				rb_free(fc->channel);
				if (fc->league != NULL)
					rb_free(fc->league);
				if (fc->team != NULL)
					rb_free(fc->team);
				rb_free(fc);
				sendto_one_notice(source_p, ":*** Channel %s unsubscribed from football news", parv[2]);
				return;
			}
		}
		sendto_one_notice(source_p, ":*** Channel %s is not subscribed", parv[2]);
		return;
	}
	
	req = rb_malloc(sizeof(struct football_request));
	req->source_p = source_p;
	req->query = rb_strdup(query);
	req->fd = NULL;
	req->response_len = 0;
	req->dns_req = 0;
	req->dns_req_v4 = 0;
	req->tried_ipv6 = false;
	req->is_news_update = false;
	
	if (parc > 2 && !EmptyString(parv[2])) {
		req->chptr = find_channel(parv[2]);
	} else {
		req->chptr = NULL;
	}
	
	/* Start DNS lookup - prefer IPv6, fallback to IPv4 */
	req->tried_ipv6 = false;
	req->dns_req = lookup_hostname(football_api_url, AF_INET6, football_dns_callback, req);
	req->dns_req_v4 = 0;
	
	if (req->dns_req == 0) {
		/* IPv6 lookup failed, try IPv4 immediately */
		req->dns_req_v4 = lookup_hostname(football_api_url, AF_INET, football_dns_callback, req);
		if (req->dns_req_v4 == 0) {
			sendto_one_notice(source_p, ":*** Failed to start DNS lookup");
			rb_free(req->query);
			rb_free(req);
			return;
		}
	}
}

static void
add_football_channel(const char *channel, const char *league, const char *team)
{
	struct football_channel *fc;
	rb_dlink_node *ptr;
	
	/* Check if channel already exists */
	RB_DLINK_FOREACH(ptr, football_channels.head) {
		fc = ptr->data;
		if (strcasecmp(fc->channel, channel) == 0) {
			/* Update league/team if provided */
			if (league != NULL) {
				if (fc->league != NULL)
					rb_free(fc->league);
				fc->league = rb_strdup(league);
			}
			if (team != NULL) {
				if (fc->team != NULL)
					rb_free(fc->team);
				fc->team = rb_strdup(team);
			}
			return;
		}
	}
	
	/* Add new channel subscription */
	fc = rb_malloc(sizeof(struct football_channel));
	fc->channel = rb_strdup(channel);
	fc->league = league != NULL ? rb_strdup(league) : NULL;
	fc->team = team != NULL ? rb_strdup(team) : NULL;
	rb_dlinkAdd(fc, &fc->node, &football_channels);
}

static int
_modinit(void)
{
	/* Try to get API key from environment variable */
	const char *env_key = getenv("FOOTBALL_API_KEY");
	if (env_key != NULL && strlen(env_key) > 0) {
		football_api_key = rb_strdup(env_key);
		ilog(L_MAIN, "Football API key loaded from environment variable");
	}
	
	/* If still not set, try FOOTBALL_DATA_API_KEY as alternative */
	if (football_api_key == NULL) {
		env_key = getenv("FOOTBALL_DATA_API_KEY");
		if (env_key != NULL && strlen(env_key) > 0) {
			football_api_key = rb_strdup(env_key);
			ilog(L_MAIN, "Football API key loaded from FOOTBALL_DATA_API_KEY environment variable");
		}
	}
	
	if (football_api_key == NULL) {
		ilog(L_MAIN, "Football API key not configured. Set FOOTBALL_API_KEY or FOOTBALL_DATA_API_KEY environment variable for full access.");
		ilog(L_MAIN, "Module will use free tier (limited requests)");
	}
	
	/* Start periodic news updates */
	football_news_ev = rb_event_addish("football_news_update", football_news_update, NULL, FOOTBALL_NEWS_INTERVAL);
	
	return 0;
}

static void
_moddeinit(void)
{
	rb_dlink_node *ptr, *next;
	
	if (football_news_ev != NULL) {
		rb_event_delete(football_news_ev);
		football_news_ev = NULL;
	}
	
	if (football_api_key != NULL) {
		rb_free(football_api_key);
		football_api_key = NULL;
	}
	
	RB_DLINK_FOREACH_SAFE(ptr, next, football_channels.head) {
		struct football_channel *fc = ptr->data;
		rb_dlinkDelete(ptr, &football_channels);
		rb_free(fc->channel);
		if (fc->league != NULL)
			rb_free(fc->league);
		if (fc->team != NULL)
			rb_free(fc->team);
		rb_free(fc);
	}
}

DECLARE_MODULE_AV2(football, _modinit, _moddeinit, football_clist, NULL, NULL, NULL, NULL, football_desc);

