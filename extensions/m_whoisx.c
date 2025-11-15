/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_whoisx.c: WHOISX command for extended WHOIS information
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

static const char whoisx_desc[] = "Provides WHOISX command for extended WHOIS information";

static void m_whoisx(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message whoisx_msgtab = {
	"WHOISX", 0, 0, 0, 0,
	{mg_unreg, {m_whoisx, 1}, mg_ignore, mg_ignore, mg_ignore, {m_whoisx, 1}}
};

mapi_clist_av1 whoisx_clist[] = { &whoisx_msgtab, NULL };

DECLARE_MODULE_AV2(m_whoisx, NULL, NULL, whoisx_clist, NULL, NULL, NULL, NULL, whoisx_desc);

static void
m_whoisx(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	rb_dlink_node *ptr;
	struct membership *msptr;
	int channel_count = 0;
	int op_count = 0;
	int halfop_count = 0;
	int voice_count = 0;

	if (parc < 2 || EmptyString(parv[1]))
	{
		sendto_one_numeric(source_p, ERR_NEEDMOREPARAMS, form_str(ERR_NEEDMOREPARAMS), "WHOISX");
		return;
	}

	target_p = find_person(parv[1]);
	if (target_p == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHNICK, form_str(ERR_NOSUCHNICK), parv[1]);
		return;
	}

	/* Count channels and status */
	RB_DLINK_FOREACH(ptr, target_p->user->channel.head)
	{
		msptr = ptr->data;
		channel_count++;
		if (is_chanop(msptr))
			op_count++;
		else if (is_halfop(msptr))
			halfop_count++;
		else if (is_voiced(msptr))
			voice_count++;
	}

	sendto_one_notice(source_p, ":*** Extended information for %s:", target_p->name);
	sendto_one_notice(source_p, ":*** Channels: %d total (Ops: %d, Halfops: %d, Voices: %d)",
		channel_count, op_count, halfop_count, voice_count);
	sendto_one_notice(source_p, ":*** User: %s!%s@%s", target_p->name, target_p->username, target_p->host);
	if (target_p->info != NULL)
		sendto_one_notice(source_p, ":*** Realname: %s", target_p->info);
	if (IsOper(target_p))
		sendto_one_notice(source_p, ":*** IRC Operator");
	if (IsAway(target_p))
		sendto_one_notice(source_p, ":*** Away: %s", target_p->user->away);
}

