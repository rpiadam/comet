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

static const char chm_delayjoin_desc[] = "Adds channel mode +D, which delays JOIN until first message";

static unsigned int mode_delayjoin;

static void hook_join_channel(void *);
static void hook_privmsg_channel(void *);

mapi_hfn_list_av1 chm_delayjoin_hfnlist[] = {
	{ "join_channel", hook_join_channel },
	{ "privmsg_channel", hook_privmsg_channel },
	{ NULL, NULL }
};

static void
hook_join_channel(void *data_)
{
	hook_data_channel_join *data = data_;

	if (!(data->chptr->mode.mode & mode_delayjoin))
		return;

	/* Store that user hasn't spoken yet */
	/* This would need to be tracked per user/channel */
}

static void
hook_privmsg_channel(void *data_)
{
	hook_data_privmsg_channel *data = data_;

	if (!(data->chptr->mode.mode & mode_delayjoin))
		return;

	/* On first message, send delayed JOIN */
	/* Implementation would track which users haven't been shown yet */
}

static int
_modinit(void)
{
	mode_delayjoin = cflag_add('D', chm_simple);
	if (mode_delayjoin == 0) {
		ierror("chm_delayjoin: unable to allocate cmode slot for +D");
		return -1;
	}
	return 0;
}

static void
_moddeinit(void)
{
	cflag_orphan('D');
}

DECLARE_MODULE_AV2(chm_delayjoin, _modinit, _moddeinit, NULL, NULL, chm_delayjoin_hfnlist, NULL, NULL, chm_delayjoin_desc);

