/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * chm_delayjoin.c: channel mode +D (delay join)
 *
 * Copyright (c) 2024
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 */

#include "stdinc.h"
#include "modules.h"
#include "hook.h"
#include "client.h"
#include "ircd.h"
#include "send.h"
#include "s_user.h"
#include "channel.h"
#include "chmode.h"
#include "logger.h"
#include "hash.h"

static const char chm_delayjoin_desc[] = "Adds channel mode +D, which delays JOIN until first message";

/* Compatibility function - rb_dictionary_str_casecmp is an alias for rb_strcasecmp */
static int
rb_dictionary_str_casecmp(const void *a, const void *b)
{
	return rb_strcasecmp(a, b);
}

static unsigned int mode_delayjoin;

static void hook_join_channel(void *);
static void hook_privmsg_channel(void *);
static void hook_client_exit(void *);

mapi_hfn_list_av1 chm_delayjoin_hfnlist[] = {
	{ "channel_join", hook_join_channel },
	{ "privmsg_channel", hook_privmsg_channel },
	{ "after_client_exit", hook_client_exit },
	{ NULL, NULL }
};

struct delayed_join {
	struct Client *client_p;
	struct Channel *chptr;
	time_t join_time;
	rb_dlink_node node;
};

static rb_dictionary *delayed_joins;

static void
hook_join_channel(void *data_)
{
	hook_data_channel_activity *data = data_;
	struct delayed_join *dj;
	char key[512];

	if (!(data->chptr->mode.mode & mode_delayjoin))
		return;

	if (!MyClient(data->client))
		return;

	/* Store that user hasn't spoken yet */
	snprintf(key, sizeof(key), "%s:%s", data->client->name, data->chptr->chname);
	dj = rb_malloc(sizeof(struct delayed_join));
	dj->client_p = data->client;
	dj->chptr = data->chptr;
	dj->join_time = rb_current_time();
	rb_dictionary_add(delayed_joins, key, dj);
}

static void
hook_privmsg_channel(void *data_)
{
	hook_data_privmsg_channel *data = data_;
	struct delayed_join *dj;
	char key[512];
	struct membership *msptr;

	if (!(data->chptr->mode.mode & mode_delayjoin))
		return;

	if (!MyClient(data->source_p))
		return;

	snprintf(key, sizeof(key), "%s:%s", data->source_p->name, data->chptr->chname);
	dj = rb_dictionary_retrieve(delayed_joins, key);
	if (dj == NULL)
		return;

	/* On first message, send delayed JOIN to other users */
	msptr = find_channel_membership(data->chptr, data->source_p);
	if (msptr != NULL) {
		/* Send JOIN to other channel members */
		sendto_channel_local_butone(data->source_p, ALL_MEMBERS, data->chptr,
			":%s!%s@%s JOIN %s", data->source_p->name,
			data->source_p->username, data->source_p->host,
			data->chptr->chname);
	}

	/* Remove from delayed list */
	rb_dictionary_delete(delayed_joins, key);
	rb_free(dj);
}

static void
hook_client_exit(void *data_)
{
	hook_data_client_exit *data = data_;
	rb_dictionary_iter iter;
	struct delayed_join *dj;
	char key[512];

	if (!MyClient(data->target))
		return;

	/* Clean up any delayed joins for this client */
	RB_DICTIONARY_FOREACH(dj, &iter, delayed_joins) {
		if (dj->client_p == data->target) {
			snprintf(key, sizeof(key), "%s:%s", dj->client_p->name, dj->chptr->chname);
			rb_dictionary_delete(delayed_joins, key);
			rb_free(dj);
			break;
		}
	}
}

static int
_modinit(void)
{
	mode_delayjoin = cflag_add('D', chm_simple);
	if (mode_delayjoin == 0) {
		ierror("chm_delayjoin: unable to allocate cmode slot for +D");
		return -1;
	}

	delayed_joins = rb_dictionary_create("delayed_joins", rb_dictionary_str_casecmp);
	return 0;
}

static void
_moddeinit(void)
{
	rb_dictionary_iter iter;
	struct delayed_join *dj;

	RB_DICTIONARY_FOREACH(dj, &iter, delayed_joins) {
		rb_free(dj);
	}

	if (delayed_joins != NULL) {
		rb_dictionary_destroy(delayed_joins, NULL, NULL);
		delayed_joins = NULL;
	}

	cflag_orphan('D');
}

DECLARE_MODULE_AV2(chm_delayjoin, _modinit, _moddeinit, NULL, NULL, chm_delayjoin_hfnlist, NULL, NULL, chm_delayjoin_desc);

