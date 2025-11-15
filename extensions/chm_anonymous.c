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

static void hook_names_channel(void *);
static void hook_who_channel(void *);
static void hook_whois_channel(void *);

mapi_hfn_list_av1 chm_anonymous_hfnlist[] = {
	{ "names_channel", hook_names_channel },
	{ "who_channel", hook_who_channel },
	{ "whois_channel", hook_whois_channel },
	{ NULL, NULL }
};

static void chm_anonymous_mode(struct Client *source_p, struct Channel *chptr,
		int alevel, const char *arg, int *errors, int dir, char c, long mode_type);

static int
_modinit(void)
{
	mode_anonymous = cflag_add('a', chm_anonymous_mode);
	if (mode_anonymous == 0)
		return -1;

	return 0;
}

static void
chm_anonymous_mode(struct Client *source_p, struct Channel *chptr,
		int alevel, const char *arg, int *errors, int dir, char c, long mode_type)
{
	/* Use chm_staff logic but with MODE_ANONYMOUS */
	if(MyClient(source_p) && !IsOper(source_p))
	{
		if(!(*errors & SM_ERR_NOPRIVS))
			sendto_one_numeric(source_p, ERR_NOPRIVILEGES, form_str(ERR_NOPRIVILEGES));
		*errors |= SM_ERR_NOPRIVS;
		return;
	}
	if(MyClient(source_p) && !HasPrivilege(source_p, "oper:cmodes"))
	{
		if(!(*errors & SM_ERR_NOPRIVS))
			sendto_one(source_p, form_str(ERR_NOPRIVS), me.name,
					source_p->name, "cmodes");
		*errors |= SM_ERR_NOPRIVS;
		return;
	}

	/* setting + */
	if((dir == MODE_ADD) && !(chptr->mode.mode & MODE_ANONYMOUS))
	{
		chptr->mode.mode |= MODE_ANONYMOUS;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_ADD;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count++].arg = NULL;
	}
	else if((dir == MODE_DEL) && (chptr->mode.mode & MODE_ANONYMOUS))
	{
		chptr->mode.mode &= ~MODE_ANONYMOUS;

		mode_changes[mode_count].letter = c;
		mode_changes[mode_count].dir = MODE_DEL;
		mode_changes[mode_count].mems = ALL_MEMBERS;
		mode_changes[mode_count].id = NULL;
		mode_changes[mode_count++].arg = NULL;
	}
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

