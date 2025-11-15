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

struct server_metrics {
	unsigned long connections;
	unsigned long messages;
	unsigned long channels;
	unsigned long users;
	time_t last_update;
};

struct server_metrics metrics;
rb_dictionary_t *channel_metrics_dict;
static struct ev_entry *metrics_update_ev;

static void hook_new_local_user(void *);
static void hook_client_exit(void *);
static void hook_privmsg_channel(void *);
static void hook_channel_join_metrics(void *);
static void hook_channel_part_metrics(void *);
static void metrics_update(void *);
static struct channel_metrics *get_channel_metrics(struct Channel *chptr);

mapi_hfn_list_av1 metrics_hfnlist[] = {
	{ "new_local_user", hook_new_local_user },
	{ "client_exit", hook_client_exit },
	{ "privmsg_channel", hook_privmsg_channel },
	{ "channel_join", hook_channel_join_metrics },
	{ "channel_part", hook_channel_part_metrics },
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
	struct channel_metrics *chm;

	if (data->msgtype == MESSAGE_TYPE_PRIVMSG)
	{
		metrics.messages++;
		chm = get_channel_metrics(data->chptr);
		if (chm)
		{
			chm->messages++;
			chm->last_activity = rb_current_time();
		}
	}
}

static void
hook_channel_join_metrics(void *data_)
{
	hook_data_channel_activity *data = data_;
	struct channel_metrics *chm;

	if (!MyClient(data->client))
		return;

	chm = get_channel_metrics(data->chptr);
	if (chm)
	{
		chm->joins++;
		chm->last_activity = rb_current_time();
	}
}

static void
hook_channel_part_metrics(void *data_)
{
	hook_data_channel_activity *data = data_;
	struct channel_metrics *chm;

	if (!MyClient(data->client))
		return;

	chm = get_channel_metrics(data->chptr);
	if (chm)
	{
		chm->parts++;
		chm->last_activity = rb_current_time();
	}
}

static struct channel_metrics *
get_channel_metrics(struct Channel *chptr)
{
	struct channel_metrics *chm;

	if (chptr == NULL)
		return NULL;

	chm = rb_dictionary_retrieve(channel_metrics_dict, chptr->chname);
	if (chm == NULL)
	{
	chm = rb_malloc(sizeof(struct channel_metrics));
	memset(chm, 0, sizeof(struct channel_metrics));
	chm->chptr = chptr;
	chm->created = rb_current_time();
	chm->last_activity = rb_current_time();
	rb_dictionary_add(channel_metrics_dict, chptr->chname, chm);
	}

	return chm;
}

static void
metrics_update(void *unused)
{
	metrics.users = ServerStats.is_cl;
	metrics.channels = rb_dlink_list_length(&global_channel_list);
	metrics.last_update = rb_current_time();
}

static int
modinit(void)
{
	memset(&metrics, 0, sizeof(metrics));
	channel_metrics_dict = rb_dictionary_create("channel_metrics", rb_dictionary_str_casecmp);
	metrics_update_ev = rb_event_addish("metrics_update", metrics_update, NULL, 60);
	return 0;
}

static void
moddeinit(void)
{
	if (metrics_update_ev != NULL)
		rb_event_delete(metrics_update_ev);
	if (channel_metrics_dict != NULL)
	{
		rb_dictionary_destroy(channel_metrics_dict, NULL, NULL);
		channel_metrics_dict = NULL;
	}
}

DECLARE_MODULE_AV2(metrics, modinit, moddeinit, NULL, NULL, metrics_hfnlist, NULL, NULL, metrics_desc);

