/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * cap_typing.c: implement the draft/typing IRCv3 capability
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
#include "s_serv.h"
#include "msgbuf.h"
#include "parse.h"
#include "hash.h"
#include "channel.h"

static const char cap_typing_desc[] = "Provides the draft/typing client capability for typing indicators";

unsigned int CLICAP_TYPING = 0;

static void m_typing(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message typing_msgtab = {
	"TYPING", 0, 0, 0, 0,
	{mg_unreg, {m_typing, 2}, mg_ignore, mg_ignore, mg_ignore, {m_typing, 2}}
};

mapi_clist_av1 typing_clist[] = { &typing_msgtab, NULL };

mapi_cap_list_av2 cap_typing_cap_list[] = {
	{ MAPI_CAP_CLIENT, "draft/typing", NULL, &CLICAP_TYPING },
	{ 0, NULL, NULL, NULL },
};

DECLARE_MODULE_AV2(cap_typing, NULL, NULL, typing_clist, NULL, NULL, cap_typing_cap_list, NULL, cap_typing_desc);

/* Handle TYPING command: TYPING <target> <state> */
/* state: "active" (typing), "paused" (paused), "done" (stopped) */
static void
m_typing(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	struct Channel *chptr;
	const char *state;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2]))
		return;

	if (!IsCapable(source_p, CLICAP_TYPING))
		return;

	state = parv[2];

	/* Find target (channel or user) */
	if (IsChanPrefix(parv[1][0]))
	{
		chptr = find_channel(parv[1]);
		if (chptr == NULL || !IsMember(source_p, chptr))
			return;

		/* Send typing indicator to all members with draft/typing capability */
		rb_dlink_node *ptr;
		struct membership *msptr;
		RB_DLINK_FOREACH(ptr, chptr->members.head)
		{
			msptr = ptr->data;
			if (IsCapable(msptr->client_p, CLICAP_TYPING) && msptr->client_p != source_p)
			{
				sendto_one(msptr->client_p, ":%s TYPING %s %s",
					source_p->name, chptr->chname, state);
			}
		}
	}
	else
	{
		target_p = find_person(parv[1]);
		if (target_p == NULL || !IsCapable(target_p, CLICAP_TYPING))
			return;

		/* Send typing indicator to target */
		sendto_one(target_p, ":%s TYPING %s %s",
			source_p->name, target_p->name, state);
	}
}

