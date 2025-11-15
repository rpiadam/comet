/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * chm_noknock.c: channel mode +K (no-knock)
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

static const char chm_noknock_desc[] = "Adds channel mode +K, which disables KNOCK";

static unsigned int mode_noknock;

static void hook_knock_channel(void *);

mapi_hfn_list_av1 chm_noknock_hfnlist[] = {
	{ "knock_channel", hook_knock_channel },
	{ NULL, NULL }
};

static void
hook_knock_channel(void *data_)
{
	hook_data_channel_knock *data = data_;

	if (!(data->chptr->mode.mode & mode_noknock))
		return;

	sendto_one_numeric(data->source_p, ERR_CANNOTSENDTOCHAN, form_str(ERR_CANNOTSENDTOCHAN),
		data->chptr->chname, "KNOCK is disabled on this channel (+K)");
	data->approved = ERR_CANNOTSENDTOCHAN;
}

static int
_modinit(void)
{
	mode_noknock = cflag_add('K', chm_simple);
	if (mode_noknock == 0) {
		ierror("chm_noknock: unable to allocate cmode slot for +K");
		return -1;
	}
	return 0;
}

static void
_moddeinit(void)
{
	cflag_orphan('K');
}

DECLARE_MODULE_AV2(chm_noknock, _modinit, _moddeinit, NULL, NULL, chm_noknock_hfnlist, NULL, NULL, chm_noknock_desc);

