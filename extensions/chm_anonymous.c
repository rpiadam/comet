/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * chm_anonymous.c: channel mode +a (anonymous ops - op hiding)
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
#include "s_conf.h"
#include "s_user.h"
#include "s_serv.h"
#include "numeric.h"
#include "chmode.h"
#include "channel.h"

static const char chm_anonymous_desc[] =
	"Adds channel mode +a which hides operator status from non-operators";

static unsigned int mode_anonymous;
unsigned int chm_anonymous_mode_flag = 0; /* Export for use in channel.c */

static void hook_names_channel(void *);
static void hook_who_channel(void *);
static void hook_whois_channel(void *);

mapi_hfn_list_av1 chm_anonymous_hfnlist[] = {
	{ "names_channel", hook_names_channel },
	{ "who_channel", hook_who_channel },
	{ "whois_channel", hook_whois_channel },
	{ NULL, NULL }
};

static int
_modinit(void)
{
	mode_anonymous = cflag_add('a', chm_staff);
	if (mode_anonymous == 0)
		return -1;

	chm_anonymous_mode_flag = mode_anonymous;
	return 0;
}

static void
_moddeinit(void)
{
	cflag_orphan('a');
}

DECLARE_MODULE_AV2(chm_anonymous, _modinit, _moddeinit, NULL, NULL, chm_anonymous_hfnlist, NULL, NULL, chm_anonymous_desc);

/* Hide op status in NAMES for non-ops */
static void
hook_names_channel(void *data_)
{
	hook_data_channel *data = data_;
	struct Channel *chptr = data->chptr;
	struct Client *client_p = data->client;
	struct membership *msptr;
	rb_dlink_node *ptr;

	if (!(chptr->mode.mode & mode_anonymous))
		return;

	if (!MyClient(client_p))
		return;

	/* If requester is an oper or op in channel, show real status */
	msptr = find_channel_membership(chptr, client_p);
	if (IsOper(client_p) || (msptr && is_chanop(msptr)))
		return;

	/* For non-ops, hide op status - this is handled in the NAMES response
	 * by checking membership flags, so we don't need to modify anything here.
	 * The actual hiding happens in the NAMES command handler.
	 */
}

/* Hide op status in WHO for non-ops */
static void
hook_who_channel(void *data_)
{
	hook_data_channel *data = data_;
	struct Channel *chptr = data->chptr;
	struct Client *client_p = data->client;
	struct membership *msptr;

	if (!(chptr->mode.mode & mode_anonymous))
		return;

	if (!MyClient(client_p))
		return;

	/* If requester is an oper or op in channel, show real status */
	msptr = find_channel_membership(chptr, client_p);
	if (IsOper(client_p) || (msptr && is_chanop(msptr)))
		return;

	/* Similar to NAMES, the actual hiding happens in WHO command handler */
}

/* Hide op status in WHOIS for non-ops */
static void
hook_whois_channel(void *data_)
{
	hook_data_channel *data = data_;
	struct Channel *chptr = data->chptr;
	struct Client *client_p = data->client;
	struct membership *msptr;

	if (!(chptr->mode.mode & mode_anonymous))
		return;

	if (!MyClient(client_p))
		return;

	/* If requester is an oper or op in channel, show real status */
	msptr = find_channel_membership(chptr, client_p);
	if (IsOper(client_p) || (msptr && is_chanop(msptr)))
		return;

	/* Similar to NAMES, the actual hiding happens in WHOIS command handler */
}

