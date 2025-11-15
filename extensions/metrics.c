/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * metrics.c: Metrics and observability system
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

static const char metrics_desc[] = "Provides metrics and observability for the IRC server";

struct server_metrics {
	unsigned long connections;
	unsigned long messages;
	unsigned long channels;
	unsigned long users;
	time_t last_update;
};

static struct server_metrics metrics;
static struct ev_entry *metrics_update_ev;

static void hook_new_local_user(void *);
static void hook_client_exit(void *);
static void hook_privmsg_channel(void *);
static void metrics_update(void *);

mapi_hfn_list_av1 metrics_hfnlist[] = {
	{ "new_local_user", hook_new_local_user },
	{ "client_exit", hook_client_exit },
	{ "privmsg_channel", hook_privmsg_channel },
	{ NULL, NULL }
};

static void
hook_new_local_user(void *data_)
{
	struct Client *client_p = data_;
	if (MyClient(client_p))
		metrics.connections++;
}

static void
hook_client_exit(void *data_)
{
	/* Track disconnections */
}

static void
hook_privmsg_channel(void *data_)
{
	hook_data_privmsg_channel *data = data_;
	if (data->msgtype == MESSAGE_TYPE_PRIVMSG)
		metrics.messages++;
}

static void
metrics_update(void *unused)
{
	metrics.users = ServerStats.is_cl;
	metrics.channels = rb_dictionary_size(channel_dict);
	metrics.last_update = rb_current_time();
}

static int
modinit(void)
{
	memset(&metrics, 0, sizeof(metrics));
	metrics_update_ev = rb_event_addish("metrics_update", metrics_update, NULL, 60);
	return 0;
}

static void
moddeinit(void)
{
	if (metrics_update_ev != NULL)
		rb_event_delete(metrics_update_ev);
}

DECLARE_MODULE_AV2(metrics, modinit, moddeinit, NULL, NULL, metrics_hfnlist, NULL, NULL, metrics_desc);

