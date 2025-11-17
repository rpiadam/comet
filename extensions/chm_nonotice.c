/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * chm_nonotice.c: channel mode +T (no notices)
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

static const char chm_nonotice_desc[] = "Adds channel mode +T, which disallows channel notices";

static unsigned int mode_nonotice;

static void hook_notice_channel(void *);

mapi_hfn_list_av1 chm_nonotice_hfnlist[] = {
	{ "notice_channel", hook_notice_channel },
	{ NULL, NULL }
};

static void
hook_notice_channel(void *data_)
{
	hook_data_privmsg_channel *data = data_;
	struct membership *msptr;

	if (!(data->chptr->mode.mode & mode_nonotice))
		return;

	if (data->msgtype != MESSAGE_TYPE_NOTICE)
		return;

	msptr = find_channel_membership(data->chptr, data->source_p);
	if (is_chanop(msptr))
		return;

	sendto_one_numeric(data->source_p, ERR_CANNOTSENDTOCHAN, form_str(ERR_CANNOTSENDTOCHAN),
		data->chptr->chname);
	sendto_one_notice(data->source_p, ":*** NOTICE is disabled on this channel (+T)");
	data->approved = ERR_CANNOTSENDTOCHAN;
}

static int
_modinit(void)
{
	mode_nonotice = cflag_add('T', chm_simple);
	if (mode_nonotice == 0) {
		ierror("chm_nonotice: unable to allocate cmode slot for +T");
		return -1;
	}
	return 0;
}

static void
_moddeinit(void)
{
	cflag_orphan('T');
}

DECLARE_MODULE_AV2(chm_nonotice, _modinit, _moddeinit, NULL, NULL, chm_nonotice_hfnlist, NULL, NULL, chm_nonotice_desc);
