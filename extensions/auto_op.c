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

static void hook_join_channel(void *);

mapi_hfn_list_av1 auto_op_hfnlist[] = {
	{ "join_channel", hook_join_channel },
	{ NULL, NULL }
};

static void
hook_join_channel(void *data_)
{
	hook_data_channel_join *data = data_;
	struct membership *msptr;

	if (!MyClient(data->client_p))
		return;

	msptr = find_channel_membership(data->chptr, data->client_p);
	if (msptr == NULL)
		return;

	/* Auto-op logic - check if channel/user matches criteria */
	/* This is a framework - would need configuration for which channels/users */
	if (is_chanop(msptr))
		return;

	/* Example: auto-op if user is identified */
	if (!EmptyString(data->client_p->user->suser)) {
		/* Would set +o mode here */
	}
}

DECLARE_MODULE_AV2(auto_op, NULL, NULL, NULL, NULL, auto_op_hfnlist, NULL, NULL, auto_op_desc);

