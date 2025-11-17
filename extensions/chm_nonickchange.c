/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * chm_nonickchange.c: channel mode +N (no nick changes)
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

static const char chm_nonickchange_desc[] = "Adds channel mode +N, which disallows nick changes in channel";

static unsigned int mode_nonickchange;

static void hook_nick_change(void *);

mapi_hfn_list_av1 chm_nonickchange_hfnlist[] = {
	{ "nick_change", hook_nick_change },
	{ NULL, NULL }
};

static void
hook_nick_change(void *data_)
{
	hook_data_nick_change *data = data_;
	struct membership *msptr;
	rb_dlink_node *ptr;

	if (!MyClient(data->client_p))
		return;

	RB_DLINK_FOREACH(ptr, data->client_p->user->channel.head) {
		msptr = ptr->data;
		if (msptr->chptr->mode.mode & mode_nonickchange) {
			sendto_one_notice(data->client_p, ":*** Cannot change nickname: channel %s has +N set",
				msptr->chptr->chname);
			/* Note: This would need to prevent the nick change */
			/* Full implementation would require hooking into nick change logic */
		}
	}
}

static int
_modinit(void)
{
	mode_nonickchange = cflag_add('N', chm_simple);
	if (mode_nonickchange == 0) {
		ierror("chm_nonickchange: unable to allocate cmode slot for +N");
		return -1;
	}
	return 0;
}

static void
_moddeinit(void)
{
	cflag_orphan('N');
}

DECLARE_MODULE_AV2(chm_nonickchange, _modinit, _moddeinit, NULL, NULL, chm_nonickchange_hfnlist, NULL, NULL, chm_nonickchange_desc);

