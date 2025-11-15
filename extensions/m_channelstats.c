/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_channelstats.c: CHANNELSTATS command for detailed channel statistics
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

static const char channelstats_desc[] = "Provides CHANNELSTATS command for detailed channel statistics";

static void m_channelstats(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message channelstats_msgtab = {
	"CHANNELSTATS", 0, 0, 0, 0,
	{mg_unreg, {m_channelstats, 1}, mg_ignore, mg_ignore, mg_ignore, {m_channelstats, 1}}
};

mapi_clist_av1 channelstats_clist[] = { &channelstats_msgtab, NULL };

DECLARE_MODULE_AV2(m_channelstats, NULL, NULL, channelstats_clist, NULL, NULL, NULL, NULL, channelstats_desc);

static void
m_channelstats(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr;
	rb_dlink_node *ptr;
	struct membership *msptr;
	int ops = 0, halfops = 0, voices = 0, regular = 0;
	int bans = 0, excepts = 0, invex = 0, quiets = 0;
	time_t now = rb_current_time();

	if (parc < 2 || EmptyString(parv[1]))
	{
		sendto_one_numeric(source_p, ERR_NEEDMOREPARAMS, form_str(ERR_NEEDMOREPARAMS), "CHANNELSTATS");
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
		if (is_chanop(msptr))
			ops++;
		else if (is_halfop(msptr))
			halfops++;
		else if (is_voiced(msptr))
			voices++;
		else
			regular++;
	}

	/* Count ban lists */
	bans = rb_dlink_list_length(&chptr->banlist);
	excepts = rb_dlink_list_length(&chptr->exceptlist);
	invex = rb_dlink_list_length(&chptr->invexlist);
	quiets = rb_dlink_list_length(&chptr->quietlist);

	sendto_one_notice(source_p, ":*** Statistics for %s:", chptr->chname);
	sendto_one_notice(source_p, ":*** Members: %d total (Ops: %d, Halfops: %d, Voices: %d, Regular: %d)",
		ops + halfops + voices + regular, ops, halfops, voices, regular);
	sendto_one_notice(source_p, ":*** Ban Lists: Bans: %d, Exceptions: %d, Invite Exceptions: %d, Quiets: %d",
		bans, excepts, invex, quiets);
	sendto_one_notice(source_p, ":*** Channel Age: %ld seconds (created: %s)",
		(long)(now - chptr->channelts), ctime(&chptr->channelts));
	if (chptr->topic != NULL)
	{
		sendto_one_notice(source_p, ":*** Topic: %s", chptr->topic);
		sendto_one_notice(source_p, ":*** Topic Age: %ld seconds", (long)(now - chptr->topic_time));
	}
}

