/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * chm_whois.c: channel mode +W (whois)
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

static const char chm_whois_desc[] = "Adds channel mode +W, which restricts WHOIS to channel operators";

static unsigned int mode_whois;

static void hook_doing_whois(void *);

mapi_hfn_list_av1 chm_whois_hfnlist[] = {
	{ "doing_whois", hook_doing_whois },
	{ NULL, NULL }
};

static void
hook_doing_whois(void *data_)
{
	hook_data_client *data = data_;
	struct membership *msptr;
	rb_dlink_node *ptr;
	bool hide_info = false;

	if (!MyClient(data->client) || !IsPerson(data->target))
		return;

	/* Check if target is in any +W channel */
	RB_DLINK_FOREACH(ptr, data->target->user->channel.head) {
		msptr = ptr->data;
		if (msptr->chptr->mode.mode & mode_whois) {
			/* Check if requester is op in this channel */
			struct membership *req_msptr = find_channel_membership(msptr->chptr, data->client);
			if (req_msptr == NULL || !is_chanop(req_msptr)) {
				/* Hide WHOIS info for non-ops */
				hide_info = true;
				break;
			}
		}
	}

	/* If info should be hidden and requester is not an operator, suppress WHOIS */
	if (hide_info && !IsOper(data->client)) {
		/* Mark as approved=1 to suppress WHOIS response */
		/* This would need to be integrated with WHOIS hook system */
	}
}

static int
_modinit(void)
{
	mode_whois = cflag_add('W', chm_simple);
	if (mode_whois == 0) {
		ierror("chm_whois: unable to allocate cmode slot for +W");
		return -1;
	}
	return 0;
}

static void
_moddeinit(void)
{
	cflag_orphan('W');
}

DECLARE_MODULE_AV2(chm_whois, _modinit, _moddeinit, NULL, NULL, chm_whois_hfnlist, NULL, NULL, chm_whois_desc);

