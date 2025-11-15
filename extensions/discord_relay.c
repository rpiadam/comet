/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * discord_relay.c: Relay IRC messages to Discord via webhooks
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
#include "logger.h"

static const char discord_relay_desc[] = "Relays IRC messages to Discord via webhooks";

/* Configuration */
static char *discord_webhook_url = NULL;
static rb_dlink_list relay_channels;  /* List of channels to relay */
static bool relay_all_channels = false;
static bool relay_private_messages = false;
static int max_message_length = 2000;  /* Discord's limit is 2000 chars */

struct channel_relay_config {
	char *channel;
	rb_dlink_node node;
};

struct discord_request {
	struct Client *source_p;
	struct Channel *chptr;
	char *message;
	char *username;
	char *channel_name;
	rb_fde_t *fd;
	char response_buf[4096];
	size_t response_len;
	uint32_t dns_req;
	uint32_t dns_req_v4;
	bool tried_ipv6;
};

static void hook_privmsg_channel(void *);
static void hook_privmsg_user(void *);
static void discord_dns_callback(const char *res, int status, int aftype, void *data);
static void discord_connect_callback(rb_fde_t *F, int status, void *data);
static void discord_read_callback(rb_fde_t *F, void *data);
static void discord_timeout_callback(rb_fde_t *F, void *data);
static bool should_relay_channel(const char *channel);
static void json_escape_string(const char *input, char *output, size_t output_size);
static void parse_webhook_url(const char *url, char *host, size_t host_size, 
			       char *path, size_t path_size, int *port, bool *is_https);

mapi_hfn_list_av1 discord_relay_hfnlist[] = {
	{ "privmsg_channel", hook_privmsg_channel },
	{ "privmsg_user", hook_privmsg_user },
	{ NULL, NULL }
};

/* Check if a channel should be relayed */
static bool
should_relay_channel(const char *channel)
{
	rb_dlink_node *ptr;

	if (relay_all_channels)
		return true;

	RB_DLINK_FOREACH(ptr, relay_channels.head)
	{
		struct channel_relay_config *config = ptr->data;
		if (irccmp(config->channel, channel) == 0)
			return true;
	}

	return false;
}

/* Add a channel to relay list */
static void
add_relay_channel(const char *channel)
{
	struct channel_relay_config *config;
	rb_dlink_node *ptr;

	/* Check if already exists */
	RB_DLINK_FOREACH(ptr, relay_channels.head)
	{
		struct channel_relay_config *existing = ptr->data;
		if (irccmp(existing->channel, channel) == 0)
			return;
	}

	config = rb_malloc(sizeof(struct channel_relay_config));
	config->channel = rb_strdup(channel);
	rb_dlinkAdd(config, &config->node, &relay_channels);
}

/* Parse Discord webhook URL */
static void
parse_webhook_url(const char *url, char *host, size_t host_size, 
		  char *path, size_t path_size, int *port, bool *is_https)
{
	const char *p;

	*is_https = false;
	*port = 443;

	/* Skip protocol */
	if (strncmp(url, "https://", 8) == 0)
	{
		*is_https = true;
		*port = 443;
		p = url + 8;
	}
	else if (strncmp(url, "http://", 7) == 0)
	{
		*is_https = false;
		*port = 80;
		p = url + 7;
	}
	else
	{
		/* Assume https if no protocol */
		*is_https = true;
		*port = 443;
		p = url;
	}

	/* Extract host and path */
	const char *slash = strchr(p, '/');
	if (slash != NULL)
	{
		size_t host_len = slash - p;
		if (host_len >= host_size)
			host_len = host_size - 1;
		rb_strlcpy(host, p, host_len + 1);
		rb_strlcpy(path, slash, path_size);
	}
	else
	{
		rb_strlcpy(host, p, host_size);
		rb_strlcpy(path, "/", path_size);
	}

	/* Check for custom port in host */
	char *colon = strchr(host, ':');
	if (colon != NULL)
	{
		*colon = '\0';
		*port = atoi(colon + 1);
	}
}

/* JSON escape a string */
static void
json_escape_string(const char *input, char *output, size_t output_size)
{
	size_t i, j;

	for (i = 0, j = 0; input[i] != '\0' && j < output_size - 1; i++)
	{
		switch (input[i])
		{
		case '"':
			if (j + 2 < output_size)
			{
				output[j++] = '\\';
				output[j++] = '"';
			}
			break;
		case '\\':
			if (j + 2 < output_size)
			{
				output[j++] = '\\';
				output[j++] = '\\';
			}
			break;
		case '\n':
			if (j + 2 < output_size)
			{
				output[j++] = '\\';
				output[j++] = 'n';
			}
			break;
		case '\r':
			if (j + 2 < output_size)
			{
				output[j++] = '\\';
				output[j++] = 'r';
			}
			break;
		case '\t':
			if (j + 2 < output_size)
			{
				output[j++] = '\\';
				output[j++] = 't';
			}
			break;
		default:
			if ((unsigned char)input[i] < 0x20)
			{
				/* Control character - skip or encode as \uXXXX */
				continue;
			}
			output[j++] = input[i];
			break;
		}
	}
	output[j] = '\0';
}

/* DNS callback */
static void
discord_dns_callback(const char *res, int status, int aftype, void *data)
{
	struct discord_request *req = data;
	struct sockaddr_storage addr;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	char host[256];
	char path[512];
	int port;
	bool is_https;

	if (aftype == AF_INET6)
	{
		req->dns_req = 0;
		req->tried_ipv6 = true;
	}
	else
	{
		req->dns_req_v4 = 0;
	}

	if (status == 0 || res == NULL)
	{
		/* If IPv6 failed, try IPv4 as fallback */
		if (aftype == AF_INET6)
		{
			parse_webhook_url(discord_webhook_url, host, sizeof(host), path, sizeof(path), &port, &is_https);
			req->dns_req_v4 = lookup_hostname(host, AF_INET, discord_dns_callback, req);
			if (req->dns_req_v4 != 0)
				return;
		}

		/* Both failed */
		ilog(L_MAIN, "Discord relay: Failed to resolve Discord webhook hostname");
		rb_free(req->message);
		rb_free(req->username);
		if (req->channel_name)
			rb_free(req->channel_name);
		rb_free(req);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	parse_webhook_url(discord_webhook_url, host, sizeof(host), path, sizeof(path), &port, &is_https);

	if (aftype == AF_INET6)
	{
		sin6 = (struct sockaddr_in6 *)&addr;
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = htons(port);
		if (rb_inet_pton(AF_INET6, res, &sin6->sin6_addr) <= 0)
		{
			ilog(L_MAIN, "Discord relay: Invalid IPv6 address");
			rb_free(req->message);
			rb_free(req->username);
			if (req->channel_name)
				rb_free(req->channel_name);
			rb_free(req);
			return;
		}
	}
	else
	{
		sin = (struct sockaddr_in *)&addr;
		sin->sin_family = AF_INET;
		sin->sin_port = htons(port);
		if (rb_inet_pton(AF_INET, res, &sin->sin_addr) <= 0)
		{
			ilog(L_MAIN, "Discord relay: Invalid IPv4 address");
			rb_free(req->message);
			rb_free(req->username);
			if (req->channel_name)
				rb_free(req->channel_name);
			rb_free(req);
			return;
		}
	}

	req->fd = rb_socket(GET_SS_FAMILY(&addr), SOCK_STREAM, IPPROTO_TCP, "discord_webhook");
	if (req->fd == NULL)
	{
		ilog(L_MAIN, "Discord relay: Failed to create socket");
		rb_free(req->message);
		rb_free(req->username);
		if (req->channel_name)
			rb_free(req->channel_name);
		rb_free(req);
		return;
	}

	rb_connect_tcp(req->fd, (struct sockaddr *)&addr, NULL, discord_connect_callback, req, 10);
}

/* Connection callback */
static void
discord_connect_callback(rb_fde_t *F, int status, void *data)
{
	struct discord_request *req = data;
	char request[4096];
	char json_body[2048];
	char escaped_username[256];
	char escaped_message[2048];
	char escaped_channel[256];
	char host[256];
	char path[512];
	int port;
	bool is_https;
	size_t len;

	if (status != RB_OK)
	{
		ilog(L_MAIN, "Discord relay: Failed to connect to Discord webhook");
		rb_close(F);
		rb_free(req->message);
		rb_free(req->username);
		if (req->channel_name)
			rb_free(req->channel_name);
		rb_free(req);
		return;
	}

	if (discord_webhook_url == NULL)
	{
		rb_close(F);
		rb_free(req->message);
		rb_free(req->username);
		if (req->channel_name)
			rb_free(req->channel_name);
		rb_free(req);
		return;
	}

	parse_webhook_url(discord_webhook_url, host, sizeof(host), path, sizeof(path), &port, &is_https);

	/* Escape strings for JSON */
	json_escape_string(req->username, escaped_username, sizeof(escaped_username));
	json_escape_string(req->message, escaped_message, sizeof(escaped_message));

	/* Truncate message if too long */
	if (strlen(escaped_message) > max_message_length - 50)
	{
		escaped_message[max_message_length - 50] = '\0';
		rb_strlcat(escaped_message, "...", sizeof(escaped_message));
	}

	/* Build JSON payload */
	if (req->channel_name != NULL)
	{
		json_escape_string(req->channel_name, escaped_channel, sizeof(escaped_channel));
		snprintf(json_body, sizeof(json_body),
			"{\"content\":\"[%s] <%s> %s\",\"username\":\"%s\"}",
			escaped_channel, escaped_username, escaped_message, escaped_username);
	}
	else
	{
		snprintf(json_body, sizeof(json_body),
			"{\"content\":\"<%s> %s\",\"username\":\"%s\"}",
			escaped_username, escaped_message, escaped_username);
	}

	/* Build HTTP request */
	snprintf(request, sizeof(request),
		"POST %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Content-Type: application/json\r\n"
		"Content-Length: %zu\r\n"
		"Connection: close\r\n"
		"\r\n"
		"%s",
		path, host, strlen(json_body), json_body);

	len = strlen(request);
	if (rb_write(F, request, len) != (ssize_t)len)
	{
		ilog(L_MAIN, "Discord relay: Failed to send webhook request");
		rb_close(F);
		rb_free(req->message);
		rb_free(req->username);
		if (req->channel_name)
			rb_free(req->channel_name);
		rb_free(req);
		return;
	}

	req->response_len = 0;
	req->fd = F;
	rb_settimeout(F, 15, discord_timeout_callback, req);
	rb_setselect(F, RB_SELECT_READ, discord_read_callback, req);
}

/* Read callback */
static void
discord_read_callback(rb_fde_t *F, void *data)
{
	struct discord_request *req = data;
	ssize_t len;
	char buf[512];

	len = rb_read(F, buf, sizeof(buf) - 1);
	if (len <= 0)
	{
		rb_settimeout(F, 0, NULL, NULL);
		rb_close(F);
		rb_free(req->message);
		rb_free(req->username);
		if (req->channel_name)
			rb_free(req->channel_name);
		rb_free(req);
		return;
	}

	buf[len] = '\0';
	if (req->response_len + len < sizeof(req->response_buf))
	{
		rb_strlcat(req->response_buf, buf, sizeof(req->response_buf));
		req->response_len += len;
	}

	/* Check if we got a complete response (simple check) */
	if (strstr(req->response_buf, "\r\n\r\n") != NULL)
	{
		/* Response received, clean up */
		rb_settimeout(F, 0, NULL, NULL);
		rb_close(F);
		rb_free(req->message);
		rb_free(req->username);
		if (req->channel_name)
			rb_free(req->channel_name);
		rb_free(req);
	}
}

/* Timeout callback */
static void
discord_timeout_callback(rb_fde_t *F, void *data)
{
	struct discord_request *req = data;

	if (req->dns_req != 0)
	{
		cancel_lookup(req->dns_req);
		req->dns_req = 0;
	}

	if (req->dns_req_v4 != 0)
	{
		cancel_lookup(req->dns_req_v4);
		req->dns_req_v4 = 0;
	}

	if (req->fd != NULL)
	{
		rb_settimeout(req->fd, 0, NULL, NULL);
		rb_close(req->fd);
		req->fd = NULL;
	}

	ilog(L_MAIN, "Discord relay: Request timed out");
	rb_free(req->message);
	rb_free(req->username);
	if (req->channel_name)
		rb_free(req->channel_name);
	rb_free(req);
}

/* Hook for channel messages */
static void
hook_privmsg_channel(void *data_)
{
	hook_data_privmsg_channel *data = data_;
	struct discord_request *req;
	char host[256];
	char path[512];
	int port;
	bool is_https;

	if (data->msgtype != MESSAGE_TYPE_PRIVMSG)
		return;

	if (discord_webhook_url == NULL)
		return;

	if (!should_relay_channel(data->chptr->chname))
		return;

	/* Skip CTCP and ACTION messages for now */
	if (data->text[0] == '\001')
		return;

	parse_webhook_url(discord_webhook_url, host, sizeof(host), path, sizeof(path), &port, &is_https);

	req = rb_malloc(sizeof(struct discord_request));
	req->source_p = data->source_p;
	req->chptr = data->chptr;
	req->message = rb_strdup(data->text);
	req->username = rb_strdup(data->source_p->name);
	req->channel_name = rb_strdup(data->chptr->chname);
	req->fd = NULL;
	req->response_len = 0;
	req->dns_req = 0;
	req->dns_req_v4 = 0;
	req->tried_ipv6 = false;

	/* Start DNS lookup */
	req->tried_ipv6 = false;
	req->dns_req = lookup_hostname(host, AF_INET6, discord_dns_callback, req);
	req->dns_req_v4 = 0;

	if (req->dns_req == 0)
	{
		/* IPv6 lookup failed, try IPv4 immediately */
		req->dns_req_v4 = lookup_hostname(host, AF_INET, discord_dns_callback, req);
		if (req->dns_req_v4 == 0)
		{
			rb_free(req->message);
			rb_free(req->username);
			rb_free(req->channel_name);
			rb_free(req);
			return;
		}
	}
}

/* Hook for private messages */
static void
hook_privmsg_user(void *data_)
{
	hook_data_privmsg_user *data = data_;
	struct discord_request *req;
	char host[256];
	char path[512];
	int port;
	bool is_https;

	if (data->msgtype != MESSAGE_TYPE_PRIVMSG)
		return;

	if (!relay_private_messages)
		return;

	if (discord_webhook_url == NULL)
		return;

	/* Skip CTCP and ACTION messages */
	if (data->text[0] == '\001')
		return;

	parse_webhook_url(discord_webhook_url, host, sizeof(host), path, sizeof(path), &port, &is_https);

	req = rb_malloc(sizeof(struct discord_request));
	req->source_p = data->source_p;
	req->chptr = NULL;
	req->message = rb_strdup(data->text);
	req->username = rb_strdup(data->source_p->name);
	req->channel_name = NULL;
	req->fd = NULL;
	req->response_len = 0;
	req->dns_req = 0;
	req->dns_req_v4 = 0;
	req->tried_ipv6 = false;

	/* Start DNS lookup */
	req->tried_ipv6 = false;
	req->dns_req = lookup_hostname(host, AF_INET6, discord_dns_callback, req);
	req->dns_req_v4 = 0;

	if (req->dns_req == 0)
	{
		req->dns_req_v4 = lookup_hostname(host, AF_INET, discord_dns_callback, req);
		if (req->dns_req_v4 == 0)
		{
			rb_free(req->message);
			rb_free(req->username);
			rb_free(req);
			return;
		}
	}
}

static int
_modinit(void)
{
	/* Configuration can be set via environment variables or config file parsing */
	/* For now, webhook URL should be set via environment variable DISCORD_WEBHOOK_URL */
	const char *env_url = getenv("DISCORD_WEBHOOK_URL");
	if (env_url != NULL)
	{
		discord_webhook_url = rb_strdup(env_url);
		ilog(L_MAIN, "Discord relay: Webhook URL configured from environment");
	}
	else
	{
		ilog(L_MAIN, "Discord relay: No webhook URL configured. Set DISCORD_WEBHOOK_URL environment variable.");
		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE,
			"Discord relay: No webhook URL configured. Set DISCORD_WEBHOOK_URL environment variable.");
	}

	/* Default: relay all channels if webhook is configured */
	if (discord_webhook_url != NULL)
	{
		relay_all_channels = true;
	}

	return 0;
}

static void
_moddeinit(void)
{
	rb_dlink_node *ptr, *next;

	if (discord_webhook_url != NULL)
	{
		rb_free(discord_webhook_url);
		discord_webhook_url = NULL;
	}

	RB_DLINK_FOREACH_SAFE(ptr, next, relay_channels.head)
	{
		struct channel_relay_config *config = ptr->data;
		rb_dlinkDelete(ptr, &relay_channels);
		rb_free(config->channel);
		rb_free(config);
	}
}

DECLARE_MODULE_AV2(discord_relay, _modinit, _moddeinit, NULL, NULL, discord_relay_hfnlist, NULL, NULL, discord_relay_desc);

