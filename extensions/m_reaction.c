/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_reaction.c: Reaction support for messages
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
#include "msgbuf.h"
#include "parse.h"

static const char reaction_desc[] = "Provides reaction support for messages";

static void m_reaction(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message reaction_msgtab = {
	"REACTION", 0, 0, 0, 0,
	{mg_unreg, {m_reaction, 3}, mg_ignore, mg_ignore, mg_ignore, {m_reaction, 3}}
};

mapi_clist_av1 reaction_clist[] = { &reaction_msgtab, NULL };

DECLARE_MODULE_AV2(m_reaction, NULL, NULL, reaction_clist, NULL, NULL, NULL, NULL, reaction_desc);

/* Handle REACTION command: REACTION <target> <msgid> <emoji> [action] */
/* action: "+" (add reaction), "-" (remove reaction) */
static void
m_reaction(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	struct Channel *chptr;
	const char *msgid, *emoji, *action = "+";

	if (parc < 4 || EmptyString(parv[1]) || EmptyString(parv[2]) || EmptyString(parv[3]))
		return;

	msgid = parv[2];
	emoji = parv[3];
	if (parc > 4 && !EmptyString(parv[4]))
		action = parv[4];

	/* Find target (channel or user) */
	if (IsChanPrefix(parv[1][0]))
	{
		chptr = find_channel(parv[1]);
		if (chptr == NULL || !IsMember(source_p, chptr))
			return;

		/* Send reaction to all members */
		rb_dlink_node *ptr;
		struct membership *msptr;
		RB_DLINK_FOREACH(ptr, chptr->members.head)
		{
			msptr = ptr->data;
			if (msptr->client_p != source_p)
			{
				sendto_one(msptr->client_p, ":%s REACTION %s %s %s %s",
					source_p->name, chptr->chname, msgid, emoji, action);
			}
		}
	}
	else
	{
		target_p = find_person(parv[1]);
		if (target_p == NULL)
			return;

		/* Send reaction to target */
		sendto_one(target_p, ":%s REACTION %s %s %s %s",
			source_p->name, target_p->name, msgid, emoji, action);
	}
}

