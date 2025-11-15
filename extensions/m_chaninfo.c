/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_chaninfo.c: CHANINFO command for channel statistics
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

static const char chaninfo_desc[] = "Provides CHANINFO command for channel information";

static void m_chaninfo(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message chaninfo_msgtab = {
	"CHANINFO", 0, 0, 0, 0,
	{mg_unreg, {m_chaninfo, 1}, mg_ignore, mg_ignore, mg_ignore, {m_chaninfo, 1}}
};

mapi_clist_av1 chaninfo_clist[] = { &chaninfo_msgtab, NULL };

DECLARE_MODULE_AV2(m_chaninfo, NULL, NULL, chaninfo_clist, NULL, NULL, NULL, NULL, chaninfo_desc);

static void
m_chaninfo(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr;
	rb_dlink_node *ptr;
	struct membership *msptr;
	int ops = 0, halfops = 0, voices = 0, members = 0;

	if (parc < 2 || EmptyString(parv[1]))
	{
		sendto_one_numeric(source_p, ERR_NEEDMOREPARAMS, form_str(ERR_NEEDMOREPARAMS), "CHANINFO");
		return;
	}

	chptr = find_channel(parv[1]);
	if (chptr == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL, form_str(ERR_NOSUCHCHANNEL), parv[1]);
		return;
	}

	if (!IsMember(source_p, chptr) && !IsOper(source_p))
	{
		sendto_one_numeric(source_p, ERR_NOTONCHANNEL, form_str(ERR_NOTONCHANNEL), chptr->chname);
		return;
	}

	/* Count members by status */
	RB_DLINK_FOREACH(ptr, chptr->members.head)
	{
		msptr = ptr->data;
		members++;
		if (is_chanop(msptr))
			ops++;
		else if (is_halfop(msptr))
			halfops++;
		else if (is_voiced(msptr))
			voices++;
	}

	sendto_one_notice(source_p, ":*** Channel: %s", chptr->chname);
	sendto_one_notice(source_p, ":*** Members: %d (Ops: %d, Halfops: %d, Voices: %d, Regular: %d)",
		members, ops, halfops, voices, members - ops - halfops - voices);
	sendto_one_notice(source_p, ":*** Created: %s", ctime(&chptr->channelts));
	if (chptr->topic != NULL)
	{
		sendto_one_notice(source_p, ":*** Topic: %s", chptr->topic);
		if (chptr->topic_info != NULL)
			sendto_one_notice(source_p, ":*** Topic set by: %s on %s", chptr->topic_info, ctime(&chptr->topic_time));
	}
	sendto_one_notice(source_p, ":*** Modes: %s", chptr->mode.mode ? "set" : "none");
}
