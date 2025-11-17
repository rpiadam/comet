/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * chm_noinvite.c: channel mode +V (no invites)
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
#include "numeric.h"
#include "logger.h"

static const char chm_noinvite_desc[] = "Adds channel mode +V, which disallows INVITE";

static unsigned int mode_noinvite;

static void hook_invite_channel(void *);

mapi_hfn_list_av1 chm_noinvite_hfnlist[] = {
	{ "invite", hook_invite_channel },
	{ NULL, NULL }
};

static void
hook_invite_channel(void *data_)
{
	hook_data_channel_approval *data = data_;

	if (!(data->chptr->mode.mode & mode_noinvite))
		return;

	sendto_one_numeric(data->client, ERR_CANNOTSENDTOCHAN, form_str(ERR_CANNOTSENDTOCHAN),
		data->chptr->chname);
	sendto_one_notice(data->client, ":*** INVITE is disabled on this channel (+V)");
	data->approved = ERR_CANNOTSENDTOCHAN;
}

static int
_modinit(void)
{
	mode_noinvite = cflag_add('V', chm_simple);
	if (mode_noinvite == 0) {
		ierror("chm_noinvite: unable to allocate cmode slot for +V");
		return -1;
	}
	return 0;
}

static void
_moddeinit(void)
{
	cflag_orphan('V');
}

DECLARE_MODULE_AV2(chm_noinvite, _modinit, _moddeinit, NULL, NULL, chm_noinvite_hfnlist, NULL, NULL, chm_noinvite_desc);

