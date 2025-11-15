/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * auto_voice.c: Auto-voice users on join
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

static const char auto_voice_desc[] = "Auto-voices users on join in configured channels";

static rb_dlink_list auto_voice_channels;
static bool auto_voice_enabled = true;

struct auto_voice_channel {
	char *channel;
	char *mask;
	rb_dlink_node node;
};

static void hook_channel_join(void *);
static void add_auto_voice_channel(const char *channel, const char *mask);

mapi_hfn_list_av1 auto_voice_hfnlist[] = {
	{ "channel_join", hook_channel_join },
	{ NULL, NULL }
};

static bool
should_auto_voice(struct Client *client_p, struct Channel *chptr)
{
	rb_dlink_node *ptr;
	char hostmask[BUFSIZE];

	snprintf(hostmask, sizeof(hostmask), "%s!%s@%s",
		client_p->name, client_p->username, client_p->host);

	RB_DLINK_FOREACH(ptr, auto_voice_channels.head) {
		struct auto_voice_channel *avc = ptr->data;
		if (strcasecmp(avc->channel, chptr->chname) == 0) {
			if (avc->mask == NULL || match(avc->mask, hostmask) == 0) {
				return true;
			}
		}
	}

	return false;
}

static void
add_auto_voice_channel(const char *channel, const char *mask)
{
	struct auto_voice_channel *avc;

	/* Check if channel already exists */
	rb_dlink_node *ptr;
	RB_DLINK_FOREACH(ptr, auto_voice_channels.head) {
		struct auto_voice_channel *existing = ptr->data;
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
	avc = rb_malloc(sizeof(struct auto_voice_channel));
	avc->channel = rb_strdup(channel);
	avc->mask = mask != NULL ? rb_strdup(mask) : NULL;
	rb_dlinkAdd(avc, &avc->node, &auto_voice_channels);
}

static void
hook_channel_join(void *data_)
{
	hook_data_channel_activity *data = data_;
	struct membership *msptr;

	if (!auto_voice_enabled || !MyClient(data->client))
		return;

	msptr = find_channel_membership(data->chptr, data->client);
	if (msptr == NULL)
		return;

	if (is_voiced(msptr))
		return;

	if (should_auto_voice(data->client, data->chptr)) {
		/* Set +v mode */
		const char *mode_parv[] = { "+v", data->client->name };
		set_channel_mode(&me, &me, data->chptr, msptr, 2, mode_parv);
	}
}

static int
_modinit(void)
{
	/* Configuration can be added here or via configuration file */
	/* Example: add_auto_voice_channel("#test", NULL); */
	/* Example: add_auto_voice_channel("#public", "*!*@*"); */
	return 0;
}

static void
_moddeinit(void)
{
	rb_dlink_node *ptr, *next;
	RB_DLINK_FOREACH_SAFE(ptr, next, auto_voice_channels.head) {
		struct auto_voice_channel *avc = ptr->data;
		rb_dlinkDelete(ptr, &auto_voice_channels);
		rb_free(avc->channel);
		if (avc->mask != NULL)
			rb_free(avc->mask);
		rb_free(avc);
	}
}

DECLARE_MODULE_AV2(auto_voice, _modinit, _moddeinit, NULL, NULL, auto_voice_hfnlist, NULL, NULL, auto_voice_desc);

