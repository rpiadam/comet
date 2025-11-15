/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * umode_sslonly.c: user mode +S (SSL only - require SSL for PMs)
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

static const char umode_sslonly_desc[] = "Adds user mode +S which requires SSL for private messages";

static void hook_privmsg_user(void *);

mapi_hfn_list_av1 umode_sslonly_hfnlist[] = {
	{ "privmsg_user", hook_privmsg_user },
	{ NULL, NULL }
};

static void
hook_privmsg_user(void *data_)
{
	hook_data_privmsg_user *data = data_;

	if (!MyClient(data->target_p))
		return;

	if ((data->target_p->umodes & user_modes['S']) && !IsSecureClient(data->source_p)) {
		sendto_one_numeric(data->source_p, ERR_CANNOTSENDTOUSER, form_str(ERR_CANNOTSENDTOUSER),
			data->target_p->name, "User requires SSL connection (+S)");
		data->approved = ERR_CANNOTSENDTOUSER;
	}
}

static int
_modinit(void)
{
	user_modes['S'] = find_umode_slot();
	construct_umodebuf();
	return 0;
}

static void
_moddeinit(void)
{
	user_modes['S'] = 0;
	construct_umodebuf();
}

DECLARE_MODULE_AV2(umode_sslonly, _modinit, _moddeinit, NULL, NULL, umode_sslonly_hfnlist, NULL, NULL, umode_sslonly_desc);

