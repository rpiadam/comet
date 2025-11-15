/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * chm_stickyops.c: channel mode +Y (sticky ops)
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
#include "hash.h"
#include "match.h"

static const char chm_stickyops_desc[] = "Adds channel mode +Y, which makes ops persist after part/rejoin";

static unsigned int mode_stickyops;

struct sticky_op {
	char *nick;
	char *hostmask;
	time_t when;
	rb_dlink_node node;
};

struct channel_stickyops {
	struct Channel *chptr;
	rb_dlink_list sticky_ops;
	rb_dlink_node node;
};

static rb_dictionary_t *stickyops_dict;

static void hook_channel_join(void *);
static void hook_after_client_exit(void *);

mapi_hfn_list_av1 chm_stickyops_hfnlist[] = {
	{ "channel_join", hook_channel_join },
	{ "after_client_exit", hook_after_client_exit },
	{ NULL, NULL }
};

static struct channel_stickyops *
get_channel_stickyops(struct Channel *chptr)
{
	struct channel_stickyops *sticky;

	sticky = rb_dictionary_retrieve(stickyops_dict, chptr->chname);
	if (sticky == NULL) {
		sticky = rb_malloc(sizeof(struct channel_stickyops));
		sticky->chptr = chptr;
		rb_dictionary_add(stickyops_dict, chptr->chname, sticky);
	}

	return sticky;
}

static void
hook_channel_join(void *data_)
{
	hook_data_channel_activity *data = data_;
	struct channel_stickyops *sticky;
	struct sticky_op *op;
	rb_dlink_node *ptr;
	struct membership *msptr;
	char hostmask[BUFSIZE];

	if (!(data->chptr->mode.mode & mode_stickyops))
		return;

	if (!MyClient(data->client))
		return;

	sticky = get_channel_stickyops(data->chptr);
	snprintf(hostmask, sizeof(hostmask), "%s!%s@%s",
		data->client->name, data->client->username, data->client->host);

	/* Check if user was previously opped */
	RB_DLINK_FOREACH(ptr, sticky->sticky_ops.head) {
		op = ptr->data;
		if (match(op->hostmask, hostmask) == 0) {
			/* Restore op status */
			msptr = find_channel_membership(data->chptr, data->client);
			if (msptr != NULL && !is_chanop(msptr)) {
				/* Set +o mode */
				/* Would need to call mode setting function */
			}
			break;
		}
	}
}

static void
hook_after_client_exit(void *data_)
{
	hook_data_client_exit *data = data_;
	rb_dlink_node *ptr;
	struct membership *msptr;
	struct channel_stickyops *sticky;
	struct sticky_op *op;
	char hostmask[BUFSIZE];

	if (!MyClient(data->target))
		return;

	/* Check all channels user was in */
	RB_DLINK_FOREACH(ptr, data->target->user->channel.head) {
		msptr = ptr->data;
		if (!(msptr->chptr->mode.mode & mode_stickyops))
			continue;

		if (!is_chanop(msptr))
			continue;

		/* Store op status */
		sticky = get_channel_stickyops(msptr->chptr);
		snprintf(hostmask, sizeof(hostmask), "%s!%s@%s",
			data->target->name, data->target->username, data->target->host);

		op = rb_malloc(sizeof(struct sticky_op));
		op->nick = rb_strdup(data->target->name);
		op->hostmask = rb_strdup(hostmask);
		op->when = rb_current_time();

		rb_dlinkAdd(op, &op->node, &sticky->sticky_ops);
	}
}

static int
_modinit(void)
{
	mode_stickyops = cflag_add('Y', chm_simple);
	if (mode_stickyops == 0) {
		ierror("chm_stickyops: unable to allocate cmode slot for +Y");
		return -1;
	}

	stickyops_dict = rb_dictionary_create("stickyops", rb_dictionary_str_casecmp);
	return 0;
}

static void
_moddeinit(void)
{
	rb_dictionary_iter iter;
	struct channel_stickyops *sticky;
	rb_dlink_node *ptr, *next;

	RB_DICTIONARY_FOREACH(sticky, &iter, stickyops_dict) {
		RB_DLINK_FOREACH_SAFE(ptr, next, sticky->sticky_ops.head) {
			struct sticky_op *op = ptr->data;
			rb_dlinkDelete(ptr, &sticky->sticky_ops);
			rb_free(op->nick);
			rb_free(op->hostmask);
			rb_free(op);
		}
		rb_free(sticky);
	}

	if (stickyops_dict != NULL) {
		rb_dictionary_destroy(stickyops_dict, NULL, NULL);
		stickyops_dict = NULL;
	}

	cflag_orphan('Y');
}

DECLARE_MODULE_AV2(chm_stickyops, _modinit, _moddeinit, NULL, NULL, chm_stickyops_hfnlist, NULL, NULL, chm_stickyops_desc);

