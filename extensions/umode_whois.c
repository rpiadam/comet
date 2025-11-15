/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * umode_whois.c: user mode +W (whois)
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

static const char umode_whois_desc[] = "Adds user mode +W which restricts WHOIS to operators";

static void hook_doing_whois(void *);

mapi_hfn_list_av1 umode_whois_hfnlist[] = {
	{ "doing_whois", hook_doing_whois },
	{ NULL, NULL }
};

static void
hook_doing_whois(void *data_)
{
	hook_data_client *data = data_;

	if (!MyClient(data->client) || !IsPerson(data->target))
		return;

	/* If target has +W, only show WHOIS to operators */
	if (data->target->umodes & user_modes['W'] && !IsOper(data->client)) {
		/* Hide WHOIS info for non-operators */
		/* This would need to modify WHOIS response */
	}
}

static int
_modinit(void)
{
	user_modes['W'] = find_umode_slot();
	construct_umodebuf();
	return 0;
}

static void
_moddeinit(void)
{
	user_modes['W'] = 0;
	construct_umodebuf();
}

DECLARE_MODULE_AV2(umode_whois, _modinit, _moddeinit, NULL, NULL, umode_whois_hfnlist, NULL, NULL, umode_whois_desc);

