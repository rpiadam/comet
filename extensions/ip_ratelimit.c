/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * ip_ratelimit.c: Per-IP rate limiting extension
 *
 * Copyright (c) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "stdinc.h"
#include "client.h"
#include "hash.h"
#include "hostmask.h"
#include "ircd.h"
#include "match.h"
#include "modules.h"
#include "msg.h"
#include "parse.h"
#include "reject.h"
#include "s_conf.h"
#include "send.h"
#include "s_serv.h"
#include "s_user.h"
#include "numeric.h"

static const char ip_ratelimit_desc[] = "Per-IP rate limiting to prevent abuse";

/* IP rate limit tracking structure */
struct ip_rate_limit {
	struct rb_sockaddr_storage ip;
	time_t window_start;
	unsigned int commands;
	unsigned int connections;
	unsigned int messages;
	unsigned int violations;
	bool throttled;
	time_t throttle_until;
	rb_dlink_node node;
};

/* Configuration */
static int max_commands_per_minute = 60;
static int max_connections_per_hour = 10;
static int max_messages_per_minute = 30;
static int cidr_limit = 24;
static int auto_kline_violations = 5;
static int throttle_duration = 3600;
static bool enabled = true;

static rb_patricia_tree_t *ip_ratelimit_tree;
static rb_dlink_list ip_ratelimit_list;
static struct ev_entry *ip_ratelimit_expire_ev;

/* Hook functions */
static void ip_ratelimit_new_local_user(void *data);
static void ip_ratelimit_client_exit(void *data);
static void ip_ratelimit_privmsg_user(void *data);
static void ip_ratelimit_privmsg_channel(void *data);

mapi_hfn_list_av1 ip_ratelimit_hfnlist[] = {
	{ "new_local_user", ip_ratelimit_new_local_user },
	{ "client_exit", ip_ratelimit_client_exit },
	{ "privmsg_user", ip_ratelimit_privmsg_user },
	{ "privmsg_channel", ip_ratelimit_privmsg_channel },
	{ NULL, NULL }
};

static struct ip_rate_limit *
find_or_create_ip_limit(struct sockaddr *addr)
{
	rb_patricia_node_t *pnode;
	struct ip_rate_limit *limit;
	int bitlen = (GET_SS_FAMILY(addr) == AF_INET) ? cidr_limit : 128;

	pnode = rb_match_ip(ip_ratelimit_tree, addr);
	if (pnode != NULL) {
		limit = pnode->data;
		return limit;
	}

	limit = rb_malloc(sizeof(struct ip_rate_limit));
	memcpy(&limit->ip, addr, sizeof(struct rb_sockaddr_storage));
	limit->window_start = rb_current_time();
	limit->commands = 0;
	limit->connections = 0;
	limit->messages = 0;
	limit->violations = 0;
	limit->throttled = false;
	limit->throttle_until = 0;

	pnode = make_and_lookup_ip(ip_ratelimit_tree, addr, bitlen);
	pnode->data = limit;
	rb_dlinkAdd(limit, &limit->node, &ip_ratelimit_list);

	return limit;
}

static struct ip_rate_limit *
find_ip_limit(struct sockaddr *addr)
{
	rb_patricia_node_t *pnode;

	pnode = rb_match_ip(ip_ratelimit_tree, addr);
	if (pnode != NULL)
		return pnode->data;

	return NULL;
}

static void
check_ip_rate_limit(struct Client *client_p, const char *type)
{
	struct ip_rate_limit *limit;
	time_t now = rb_current_time();
	int bitlen = (GET_SS_FAMILY(&client_p->localClient->ip) == AF_INET) ? cidr_limit : 128;

	if (!enabled || !MyClient(client_p) || IsOper(client_p))
		return;

	limit = find_or_create_ip_limit((struct sockaddr *)&client_p->localClient->ip);

	/* Reset window if expired */
	if (now - limit->window_start > 60) {
		limit->window_start = now;
		limit->commands = 0;
		limit->messages = 0;
	}

	/* Check if throttled */
	if (limit->throttled && limit->throttle_until > now) {
		sendto_one_notice(client_p, ":*** You are being rate limited. Please wait %d seconds.",
			(int)(limit->throttle_until - now));
		return;
	}

	if (limit->throttle_until <= now)
		limit->throttled = false;

	/* Check limits */
	if (strcmp(type, "command") == 0) {
		limit->commands++;
		if (limit->commands > max_commands_per_minute) {
			limit->violations++;
			limit->throttled = true;
			limit->throttle_until = now + throttle_duration;
			sendto_one_notice(client_p, ":*** Rate limit exceeded for commands. You are throttled for %d seconds.",
				throttle_duration);
		}
	} else if (strcmp(type, "message") == 0) {
		limit->messages++;
		if (limit->messages > max_messages_per_minute) {
			limit->violations++;
			limit->throttled = true;
			limit->throttle_until = now + throttle_duration;
			sendto_one_notice(client_p, ":*** Rate limit exceeded for messages. You are throttled for %d seconds.",
				throttle_duration);
		}
	}

	/* Auto-kline after too many violations */
	if (limit->violations >= auto_kline_violations && IsOperGeneral(client_p) == 0) {
		char reason[BUFSIZE];
		snprintf(reason, sizeof(reason), "Auto-kline: %d rate limit violations from IP", limit->violations);
		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE,
			"Auto-kline: %s (%s@%s) [%s] - %d violations",
			client_p->name, client_p->username, client_p->host,
			client_p->sockhost, limit->violations);
		/* Note: Actual kline would need to be implemented via oper command */
	}
}

static void
ip_ratelimit_new_local_user(void *data)
{
	struct Client *client_p = data;
	struct ip_rate_limit *limit;
	time_t now = rb_current_time();

	if (!enabled || !MyClient(client_p))
		return;

	limit = find_or_create_ip_limit((struct sockaddr *)&client_p->localClient->ip);

	/* Check connection limit (per hour) */
	if (now - limit->window_start > 3600) {
		limit->window_start = now;
		limit->connections = 0;
	}

	limit->connections++;
	if (limit->connections > max_connections_per_hour) {
		sendto_one_notice(client_p, ":*** Too many connections from your IP address. Please wait before connecting again.");
		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE,
			"Connection limit exceeded: %s (%s@%s) [%s] - %d connections/hour",
			client_p->name, client_p->username, client_p->host,
			client_p->sockhost, limit->connections);
		/* Could exit client here if desired */
	}
}

static void
ip_ratelimit_client_exit(void *data)
{
	/* Cleanup handled by expiration */
}

static void
ip_ratelimit_privmsg_user(void *data)
{
	struct Client *client_p = ((void **)data)[0];
	check_ip_rate_limit(client_p, "message");
}

static void
ip_ratelimit_privmsg_channel(void *data)
{
	struct Client *client_p = ((void **)data)[0];
	check_ip_rate_limit(client_p, "message");
}

static void
ip_ratelimit_expire(void *unused)
{
	rb_dlink_node *ptr, *next;
	time_t now = rb_current_time();

	RB_DLINK_FOREACH_SAFE(ptr, next, ip_ratelimit_list.head) {
		struct ip_rate_limit *limit = ptr->data;
		rb_patricia_node_t *pnode;

		/* Expire old entries (1 hour of inactivity) */
		if (now - limit->window_start > 3600 && !limit->throttled) {
			pnode = rb_match_ip(ip_ratelimit_tree, (struct sockaddr *)&limit->ip);
			if (pnode != NULL) {
				pnode->data = NULL;
				rb_patricia_remove(ip_ratelimit_tree, pnode);
			}
			rb_dlinkDelete(ptr, &ip_ratelimit_list);
			rb_free(limit);
		}
	}
}

static int
modinit(void)
{
	ip_ratelimit_tree = rb_new_patricia(128);
	ip_ratelimit_expire_ev = rb_event_addish("ip_ratelimit_expire", ip_ratelimit_expire, NULL, 60);
	return 0;
}

static void
moddeinit(void)
{
	rb_dlink_node *ptr, *next;
	rb_patricia_node_t *pnode;

	if (ip_ratelimit_expire_ev != NULL)
		rb_event_delete(ip_ratelimit_expire_ev);

	RB_DLINK_FOREACH_SAFE(ptr, next, ip_ratelimit_list.head) {
		struct ip_rate_limit *limit = ptr->data;
		pnode = rb_match_ip(ip_ratelimit_tree, (struct sockaddr *)&limit->ip);
		if (pnode != NULL) {
			pnode->data = NULL;
			rb_patricia_remove(ip_ratelimit_tree, pnode);
		}
		rb_dlinkDelete(ptr, &ip_ratelimit_list);
		rb_free(limit);
	}

	if (ip_ratelimit_tree != NULL) {
		rb_destroy_patricia(ip_ratelimit_tree, NULL);
		ip_ratelimit_tree = NULL;
	}
}

DECLARE_MODULE_AV2(ip_ratelimit, modinit, moddeinit, NULL, NULL, ip_ratelimit_hfnlist, NULL, NULL, ip_ratelimit_desc);

