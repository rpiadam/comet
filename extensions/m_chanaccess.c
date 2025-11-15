/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_chanaccess.c: CHANACCESS command for viewing channel access lists
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
#include "hash.h"
#include "parse.h"
#include "numeric.h"

static const char chanaccess_desc[] = "Provides CHANACCESS command for viewing channel access lists";

static void m_chanaccess(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message chanaccess_msgtab = {
	"CHANACCESS", 0, 0, 0, 0,
	{mg_unreg, {m_chanaccess, 1}, mg_ignore, mg_ignore, mg_ignore, {m_chanaccess, 1}}
};

mapi_clist_av1 chanaccess_clist[] = { &chanaccess_msgtab, NULL };

DECLARE_MODULE_AV2(m_chanaccess, NULL, NULL, chanaccess_clist, NULL, NULL, NULL, NULL, chanaccess_desc);

static void
m_chanaccess(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr;
	rb_dlink_node *ptr;
	struct membership *msptr;
	const char *type = "all";
	int count = 0;

	if (parc < 2 || EmptyString(parv[1]))
	{
		sendto_one_numeric(source_p, ERR_NEEDMOREPARAMS, form_str(ERR_NEEDMOREPARAMS), "CHANACCESS");
		return;
	}

	chptr = find_channel(parv[1]);
	if (chptr == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL, form_str(ERR_NOSUCHCHANNEL), parv[1]);
		return;
	}

	if (parc > 2 && !EmptyString(parv[2]))
		type = parv[2];

	if (!IsMember(source_p, chptr) && !IsOper(source_p))
	{
		sendto_one_numeric(source_p, ERR_NOTONCHANNEL, form_str(ERR_NOTONCHANNEL), chptr->chname);
		return;
	}

	sendto_one_notice(source_p, ":*** Access list for %s (type: %s)", chptr->chname, type);

	RB_DLINK_FOREACH(ptr, chptr->members.head)
	{
		msptr = ptr->data;
		
		if (!strcmp(type, "ops") && !is_chanop(msptr))
			continue;
		if (!strcmp(type, "halfops") && !is_halfop(msptr))
			continue;
		if (!strcmp(type, "voices") && !is_voiced(msptr))
			continue;
		if (!strcmp(type, "regular") && (is_chanop(msptr) || is_halfop(msptr) || is_voiced(msptr)))
			continue;

		count++;
		if (is_chanop(msptr))
			sendto_one_notice(source_p, ":*** @%s", msptr->client_p->name);
		else if (is_halfop(msptr))
			sendto_one_notice(source_p, ":*** %%%s", msptr->client_p->name);
		else if (is_voiced(msptr))
			sendto_one_notice(source_p, ":*** +%s", msptr->client_p->name);
		else
			sendto_one_notice(source_p, ":*** %s", msptr->client_p->name);
	}

	sendto_one_notice(source_p, ":*** End of access list (%d entries)", count);
}

