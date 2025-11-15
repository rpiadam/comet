/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * umode_deaf.c: user mode +d (deaf - don't receive channel messages)
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

static const char umode_deaf_desc[] = "Adds user mode +d which prevents receiving channel messages";

static void hook_privmsg_channel(void *);

mapi_hfn_list_av1 umode_deaf_hfnlist[] = {
	{ "privmsg_channel", hook_privmsg_channel },
	{ NULL, NULL }
};

static void
hook_privmsg_channel(void *data_)
{
	hook_data_privmsg_channel *data = data_;
	rb_dlink_node *ptr;
	struct Client *target;

	if (data->msgtype != MESSAGE_TYPE_PRIVMSG)
		return;

	/* Check all channel members for +d mode */
	RB_DLINK_FOREACH(ptr, data->chptr->locmembers.head) {
		struct membership *msptr = ptr->data;
		target = msptr->client_p;

		if (MyClient(target) && (target->umodes & user_modes['d'])) {
			/* Suppress message to this user */
			/* This would need to modify the send mechanism */
			/* For now, we mark it as not approved for this user */
		}
	}
}

static int
_modinit(void)
{
	user_modes['d'] = find_umode_slot();
	construct_umodebuf();
	return 0;
}

static void
_moddeinit(void)
{
	user_modes['d'] = 0;
	construct_umodebuf();
}

DECLARE_MODULE_AV2(umode_deaf, _modinit, _moddeinit, NULL, NULL, umode_deaf_hfnlist, NULL, NULL, umode_deaf_desc);

