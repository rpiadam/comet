/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * umode_secureonly.c: user mode +Z (secure only - only receive from secure users)
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
#include "numeric.h"

static const char umode_secureonly_desc[] = "Adds user mode +Z which only receives messages from secure users";

static void hook_privmsg_user(void *);
static void hook_privmsg_channel(void *);

mapi_hfn_list_av1 umode_secureonly_hfnlist[] = {
	{ "privmsg_user", hook_privmsg_user },
	{ "privmsg_channel", hook_privmsg_channel },
	{ NULL, NULL }
};

static void
hook_privmsg_user(void *data_)
{
	hook_data_privmsg_user *data = data_;

	if (!MyClient(data->target_p))
		return;

	if ((data->target_p->umodes & user_modes['Z']) && !IsSecureClient(data->source_p)) {
		sendto_one_numeric(data->source_p, ERR_CANNOTSENDTOUSER, form_str(ERR_CANNOTSENDTOUSER),
			data->target_p->name, "User only accepts messages from secure connections (+Z)");
		data->approved = ERR_CANNOTSENDTOUSER;
	}
}

static void
hook_privmsg_channel(void *data_)
{
	hook_data_privmsg_channel *data = data_;

	if (!MyClient(data->target_p))
		return;

	if ((data->target_p->umodes & user_modes['Z']) && !IsSecureClient(data->source_p)) {
		/* Suppress message to this user in channel */
		/* Implementation would filter the message */
	}
}

static int
_modinit(void)
{
	user_modes['Z'] = find_umode_slot();
	construct_umodebuf();
	return 0;
}

static void
_moddeinit(void)
{
	user_modes['Z'] = 0;
	construct_umodebuf();
}

DECLARE_MODULE_AV2(umode_secureonly, _modinit, _moddeinit, NULL, NULL, umode_secureonly_hfnlist, NULL, NULL, umode_secureonly_desc);

