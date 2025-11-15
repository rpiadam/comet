/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_weather.c: WEATHER command for weather information
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

static const char weather_desc[] = "Provides the WEATHER command for weather information";

static void m_weather(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message weather_msgtab = {
	"WEATHER", 0, 0, 0, 0,
	{mg_ignore, {m_weather, 1}, mg_ignore, mg_ignore, mg_ignore, {m_weather, 1}}
};

mapi_clist_av1 weather_clist[] = { &weather_msgtab, NULL };

/* Weather API configuration */
static char *weather_api_key = NULL;
static char *weather_api_url = "api.openweathermap.org";
static int weather_api_port = 80;

struct weather_request {
	struct Client *source_p;
	struct Channel *chptr;
	char *location;
	rb_fde_t *fd;
	char response_buf[4096];
	size_t response_len;
	uint32_t dns_req;
};

static void weather_dns_callback(const char *res, int status, int aftype, void *data);
static void weather_connect_callback(rb_fde_t *F, int status, void *data);
static void weather_read_callback(rb_fde_t *F, void *data);
static void weather_timeout_callback(rb_fde_t *F, void *data);

static void
weather_timeout_callback(rb_fde_t *F, void *data)
{
	struct weather_request *req = data;
	
	if (req->dns_req != 0) {
		cancel_lookup(req->dns_req);
		req->dns_req = 0;
	}
	
	if (req->fd != NULL) {
		rb_settimeout(req->fd, 0, NULL, NULL);
		rb_close(req->fd);
		req->fd = NULL;
	}
	
	sendto_one_notice(req->source_p, ":*** Weather request timed out");
	
	if (req->chptr != NULL) {
		sendto_channel_local(ALL_MEMBERS, req->chptr,
			":%s NOTICE %s :Weather request timed out", me.name, req->chptr->chname);
	}
	
	rb_free(req->location);
	rb_free(req);
}

static void
weather_dns_callback(const char *res, int status, int aftype, void *data)
{
	struct weather_request *req = data;
	struct sockaddr_storage addr;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	
	req->dns_req = 0;
	
	if (status == 0 || res == NULL) {
		sendto_one_notice(req->source_p, ":*** Failed to resolve weather API hostname");
		rb_free(req->location);
		rb_free(req);
		return;
	}
	
	memset(&addr, 0, sizeof(addr));
	
	if (aftype == AF_INET6) {
		sin6 = (struct sockaddr_in6 *)&addr;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = htons(weather_api_port);
		if (rb_inet_pton(AF_INET6, res, &sin6->sin6_addr) <= 0) {
			sendto_one_notice(req->source_p, ":*** Invalid IPv6 address");
			rb_free(req->location);
			rb_free(req);
			return;
		}
	} else {
		sin = (struct sockaddr_in *)&addr;
		sin->sin_family = AF_INET;
		sin->sin_port = htons(weather_api_port);
		if (rb_inet_pton(AF_INET, res, &sin->sin_addr) <= 0) {
			sendto_one_notice(req->source_p, ":*** Invalid IPv4 address");
			rb_free(req->location);
			rb_free(req);
			return;
		}
	}
	
	req->fd = rb_socket(GET_SS_FAMILY(&addr), SOCK_STREAM, IPPROTO_TCP, "weather_api");
	if (req->fd == NULL) {
		sendto_one_notice(req->source_p, ":*** Failed to create socket");
		rb_free(req->location);
		rb_free(req);
		return;
	}
	
	rb_connect_tcp(req->fd, (struct sockaddr *)&addr, NULL, weather_connect_callback, req, 10);
}

static void
weather_connect_callback(rb_fde_t *F, int status, void *data)
{
	struct weather_request *req = data;
	char request[1024];
	char *encoded_location;
	size_t len;
	
	if (status != RB_OK) {
		sendto_one_notice(req->source_p, ":*** Failed to connect to weather API");
		rb_close(F);
		rb_free(req->location);
		rb_free(req);
		return;
	}
	
	/* Simple URL encoding - just replace spaces with %20 */
	encoded_location = rb_strdup(req->location);
	for (char *p = encoded_location; *p; p++) {
		if (*p == ' ')
			*p = '+';
	}
	
	if (weather_api_key != NULL && strlen(weather_api_key) > 0) {
		snprintf(request, sizeof(request),
			"GET /data/2.5/weather?q=%s&appid=%s&units=imperial HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Connection: close\r\n"
			"\r\n",
			encoded_location, weather_api_key, weather_api_url);
	} else {
		/* Fallback to simple response if no API key */
		sendto_one_notice(req->source_p, ":*** Weather API key not configured. Please set weather_api_key in configuration.");
		rb_close(F);
		rb_free(encoded_location);
		rb_free(req->location);
		rb_free(req);
		return;
	}
	
	rb_free(encoded_location);
	
	len = strlen(request);
	if (rb_write(F, request, len) != (ssize_t)len) {
		sendto_one_notice(req->source_p, ":*** Failed to send weather request");
		rb_close(F);
		rb_free(req->location);
		rb_free(req);
		return;
	}
	
	req->response_len = 0;
	req->fd = F;
	rb_settimeout(F, 15, weather_timeout_callback, req);
	rb_setselect(F, RB_SELECT_READ, weather_read_callback, req);
}

static void
weather_read_callback(rb_fde_t *F, void *data)
{
	struct weather_request *req = data;
	ssize_t n;
	char *json_start, *temp_str, *desc_str, *humidity_str;
	char response[512];
	double temp_f, temp_c;
	
	n = rb_read(F, req->response_buf + req->response_len, sizeof(req->response_buf) - req->response_len - 1);
	if (n <= 0) {
		rb_settimeout(F, 0, NULL, NULL);
		rb_close(F);
		req->fd = NULL;
		
		req->response_buf[req->response_len] = '\0';
		
		/* Simple JSON parsing - look for temperature and description */
		json_start = strstr(req->response_buf, "\"temp\":");
		if (json_start != NULL) {
			temp_str = json_start + 7;
			temp_f = strtod(temp_str, NULL);
			temp_c = (temp_f - 32) * 5.0 / 9.0;
			
			desc_str = strstr(req->response_buf, "\"description\":\"");
			humidity_str = strstr(req->response_buf, "\"humidity\":");
			
			if (desc_str != NULL) {
				char desc[64];
				char *end;
				desc_str += 15;
				end = strchr(desc_str, '"');
				if (end != NULL) {
					size_t len = end - desc_str;
					if (len >= sizeof(desc))
						len = sizeof(desc) - 1;
					memcpy(desc, desc_str, len);
					desc[len] = '\0';
					
					double humidity = 0;
					if (humidity_str != NULL) {
						humidity = strtod(humidity_str + 11, NULL);
					}
					
					snprintf(response, sizeof(response),
						":*** Weather for %s: %.1f°F (%.1f°C), %s, Humidity %.0f%%",
						req->location, temp_f, temp_c, desc, humidity);
				} else {
					snprintf(response, sizeof(response),
						":*** Weather for %s: %.1f°F (%.1f°C)",
						req->location, temp_f, temp_c);
				}
			} else {
				snprintf(response, sizeof(response),
					":*** Weather for %s: %.1f°F (%.1f°C)",
					req->location, temp_f, temp_c);
			}
		} else {
			/* Fallback if parsing fails */
			snprintf(response, sizeof(response),
				":*** Weather for %s: Unable to parse API response",
				req->location);
		}
		
		if (req->chptr != NULL) {
			sendto_channel_local(ALL_MEMBERS, req->chptr, "%s", response);
		} else {
			sendto_one_notice(req->source_p, "%s", response);
		}
		
		rb_free(req->location);
		rb_free(req);
		return;
	}
	
	req->response_len += n;
	if (req->response_len >= sizeof(req->response_buf) - 1) {
		rb_settimeout(F, 0, NULL, NULL);
		rb_close(F);
		req->fd = NULL;
		sendto_one_notice(req->source_p, ":*** Weather response too large");
		rb_free(req->location);
		rb_free(req);
	}
}

static void
m_weather(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char *location;
	struct weather_request *req;
	
	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: WEATHER <location> [channel]");
		return;
	}

	location = (char *)parv[1];
	
	req = rb_malloc(sizeof(struct weather_request));
	req->source_p = source_p;
	req->location = rb_strdup(location);
	req->fd = NULL;
	req->response_len = 0;
	req->dns_req = 0;
	
	if (parc > 2 && !EmptyString(parv[2])) {
		req->chptr = find_channel(parv[2]);
	} else {
		req->chptr = NULL;
	}
	
	/* Start DNS lookup */
	req->dns_req = lookup_hostname(weather_api_url, AF_INET, weather_dns_callback, req);
	if (req->dns_req == 0) {
		sendto_one_notice(source_p, ":*** Failed to start DNS lookup");
		rb_free(req->location);
		rb_free(req);
		return;
	}
	
	/* Timeout will be set after DNS lookup completes */
}

DECLARE_MODULE_AV2(weather, NULL, NULL, weather_clist, NULL, NULL, NULL, NULL, weather_desc);
