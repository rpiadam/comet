/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * auto_op.c: Auto-op users on join
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
#include "chmode.h"
#include "match.h"

static const char auto_op_desc[] = "Auto-ops users on join in configured channels";

static rb_dlink_list auto_op_channels;
static bool auto_op_enabled = true;

struct auto_op_channel {
	char *channel;
	char *mask;
	rb_dlink_node node;
};

static void hook_channel_join(void *);
static void add_auto_op_channel(const char *channel, const char *mask);

mapi_hfn_list_av1 auto_op_hfnlist[] = {
	{ "channel_join", hook_channel_join },
	{ NULL, NULL }
};

static bool
should_auto_op(struct Client *client_p, struct Channel *chptr)
{
	rb_dlink_node *ptr;
	char hostmask[BUFSIZE];

	snprintf(hostmask, sizeof(hostmask), "%s!%s@%s",
		client_p->name, client_p->username, client_p->host);

	RB_DLINK_FOREACH(ptr, auto_op_channels.head) {
		struct auto_op_channel *aoc = ptr->data;
		if (strcasecmp(aoc->channel, chptr->chname) == 0) {
			if (aoc->mask == NULL || match(aoc->mask, hostmask) == 0) {
				return true;
			}
		}
	}

	return false;
}

static void
add_auto_op_channel(const char *channel, const char *mask)
{
	struct auto_op_channel *aoc;

	/* Check if channel already exists */
	rb_dlink_node *ptr;
	RB_DLINK_FOREACH(ptr, auto_op_channels.head) {
		struct auto_op_channel *existing = ptr->data;
		if (strcasecmp(existing->channel, channel) == 0) {
			/* Update mask if provided */
			if (mask != NULL) {
				if (existing->mask != NULL)
					rb_free(existing->mask);
				existing->mask = mask != NULL ? rb_strdup(mask) : NULL;
			}
			return;
		}
	}

	/* Add new channel */
	aoc = rb_malloc(sizeof(struct auto_op_channel));
	aoc->channel = rb_strdup(channel);
	aoc->mask = mask != NULL ? rb_strdup(mask) : NULL;
	rb_dlinkAdd(aoc, &aoc->node, &auto_op_channels);
}

static void
hook_channel_join(void *data_)
{
	hook_data_channel_activity *data = data_;
	struct membership *msptr;

	if (!auto_op_enabled || !MyClient(data->client))
		return;

	msptr = find_channel_membership(data->chptr, data->client);
	if (msptr == NULL)
		return;

	if (is_chanop(msptr))
		return;

	if (should_auto_op(data->client, data->chptr)) {
		/* Set +o mode */
		const char *mode_parv[] = { "+o", data->client->name };
		set_channel_mode(&me, &me, data->chptr, msptr, 2, mode_parv);
		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE,
			"Auto-op: %s in %s", data->client->name, data->chptr->chname);
	}
}

static int
_modinit(void)
{
	/* Configuration can be added here or via configuration file */
	/* Example: add_auto_op_channel("#test", NULL); */
	/* Example: add_auto_op_channel("#staff", "*!*@staff.example.com"); */
	return 0;
}

static void
_moddeinit(void)
{
	rb_dlink_node *ptr, *next;
	RB_DLINK_FOREACH_SAFE(ptr, next, auto_op_channels.head) {
		struct auto_op_channel *aoc = ptr->data;
		rb_dlinkDelete(ptr, &auto_op_channels);
		rb_free(aoc->channel);
		if (aoc->mask != NULL)
			rb_free(aoc->mask);
		rb_free(aoc);
	}
}

DECLARE_MODULE_AV2(auto_op, _modinit, _moddeinit, NULL, NULL, auto_op_hfnlist, NULL, NULL, auto_op_desc);

