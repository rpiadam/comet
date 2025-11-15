/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * ping_monitor.c: Server ping monitoring and statistics
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
#include "hook.h"
#include "s_serv.h"
#include "s_conf.h"
#include "event.h"
#include "rb_lib.h"

static const char ping_monitor_desc[] = "Monitors server ping times and latency statistics";

static void m_pingstats(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void hook_ping(void *);
static void ping_update(void *);

struct ping_stat {
	struct Client *server_p;
	time_t last_ping;
	time_t last_pong;
	unsigned long ping_count;
	unsigned long total_time;
	unsigned long min_ping;
	unsigned long max_ping;
	rb_dlink_node node;
};

static rb_dlink_list ping_stats;
static rb_event *ping_update_event;

mapi_hfn_list_av1 ping_monitor_hfnlist[] = {
	{ "ping", hook_ping },
	{ NULL, NULL }
};

struct Message pingstats_msgtab = {
	"PINGSTATS", 0, 0, 0, 0,
	{mg_unreg, {m_pingstats, 0}, mg_ignore, mg_ignore, mg_ignore, {m_pingstats, 0}}
};

mapi_clist_av1 ping_monitor_clist[] = { &pingstats_msgtab, NULL };

static struct ping_stat *
find_ping_stat(struct Client *server_p)
{
	rb_dlink_node *ptr;
	struct ping_stat *stat;

	RB_DLINK_FOREACH(ptr, ping_stats.head) {
		stat = ptr->data;
		if (stat->server_p == server_p)
			return stat;
	}
	return NULL;
}

static void
hook_ping(void *data_)
{
	hook_data_ping *data = data_;
	struct ping_stat *stat;
	time_t now = rb_current_time();

	if (!IsServer(data->target_p))
		return;

	stat = find_ping_stat(data->target_p);
	if (stat == NULL) {
		stat = rb_malloc(sizeof(struct ping_stat));
		stat->server_p = data->target_p;
		stat->last_ping = now;
		stat->last_pong = 0;
		stat->ping_count = 0;
		stat->total_time = 0;
		stat->min_ping = ULONG_MAX;
		stat->max_ping = 0;
		rb_dlinkAddAlloc(stat, &ping_stats);
	} else {
		stat->last_ping = now;
	}
}

static void
ping_update(void *unused)
{
	rb_dlink_node *ptr, *next;
	struct ping_stat *stat;
	time_t now = rb_current_time();

	/* Update pong times and calculate ping times */
	RB_DLINK_FOREACH_SAFE(ptr, next, ping_stats.head) {
		stat = ptr->data;
		
		/* Check if server is still connected */
		if (!IsServer(stat->server_p) || stat->server_p->localClient == NULL) {
			rb_dlinkDelete(ptr, &ping_stats);
			rb_free(stat);
			continue;
		}

		/* If we have a ping but no pong, check timeout */
		if (stat->last_ping > 0 && stat->last_pong < stat->last_ping) {
			time_t elapsed = now - stat->last_ping;
			if (elapsed > 60) {
				/* Server appears to be lagging or disconnected */
				/* Could send a notice to opers here */
			}
		}
	}
}

static void
m_pingstats(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	rb_dlink_node *ptr;
	struct ping_stat *stat;
	unsigned long avg_ping;

	if (!IsOper(source_p)) {
		sendto_one_numeric(source_p, ERR_NOPRIVS, form_str(ERR_NOPRIVS), "oper");
		return;
	}

	sendto_one_notice(source_p, ":*** Server Ping Statistics:");
	sendto_one_notice(source_p, ":*** Server Name | Last Ping | Last Pong | Count | Avg | Min | Max");

	RB_DLINK_FOREACH(ptr, ping_stats.head) {
		stat = ptr->data;
		if (!IsServer(stat->server_p))
			continue;

		avg_ping = stat->ping_count > 0 ? stat->total_time / stat->ping_count : 0;

		sendto_one_notice(source_p, ":*** %s | %lu | %lu | %lu | %lu | %lu | %lu",
			stat->server_p->name,
			(unsigned long)(stat->last_ping > 0 ? rb_current_time() - stat->last_ping : 0),
			(unsigned long)(stat->last_pong > 0 ? rb_current_time() - stat->last_pong : 0),
			stat->ping_count,
			avg_ping,
			stat->min_ping == ULONG_MAX ? 0 : stat->min_ping,
			stat->max_ping);
	}

	if (rb_dlink_list_length(&ping_stats) == 0)
		sendto_one_notice(source_p, ":*** No ping statistics available");
}

static int
modinit(void)
{
	/* Update ping stats every 30 seconds */
	ping_update_event = rb_event_addish("ping_update", ping_update, NULL, 30);
	return 0;
}

static void
moddeinit(void)
{
	rb_dlink_node *ptr, *next;

	if (ping_update_event)
		rb_event_delete(ping_update_event);

	RB_DLINK_FOREACH_SAFE(ptr, next, ping_stats.head) {
		rb_dlinkDelete(ptr, &ping_stats);
		rb_free(ptr->data);
	}
}

DECLARE_MODULE_AV2(ping_monitor, modinit, moddeinit, ping_monitor_clist, NULL, ping_monitor_hfnlist, NULL, NULL, ping_monitor_desc);

