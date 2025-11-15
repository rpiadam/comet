/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_chaninfo.c: CHANINFO command for channel information
 *
 * Copyright (c) 2024
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 */

#include "stdinc.h"
#include "client.h"
#include "channel.h"
#include "ircd.h"
#include "send.h"
#include "s_user.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "numeric.h"
#include "match.h"

static const char chaninfo_desc[] = "Provides the CHANINFO command to show channel information";

static void m_chaninfo(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message chaninfo_msgtab = {
	"CHANINFO", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_chaninfo, 1}}
};

mapi_clist_av1 chaninfo_clist[] = { &chaninfo_msgtab, NULL };

static void
m_chaninfo(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr;

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: CHANINFO <channel>");
		return;
	}

	chptr = find_channel(parv[1]);
	if (chptr == NULL) {
		sendto_one_notice(source_p, ":*** Channel %s not found", parv[1]);
		return;
	}

	sendto_one_notice(source_p, ":*** Channel: %s", chptr->chname);
	sendto_one_notice(source_p, ":*** Topic: %s", chptr->topic ? chptr->topic : "(none)");
	sendto_one_notice(source_p, ":*** Members: %u", rb_dlink_list_length(&chptr->members));
	sendto_one_notice(source_p, ":*** Created: %s", rb_ctime(&chptr->channelts));
	sendto_one_notice(source_p, ":*** Modes: +%s", chptr->mode.mode ? "..." : "(none)");
}

DECLARE_MODULE_AV2(chaninfo, NULL, NULL, chaninfo_clist, NULL, NULL, NULL, NULL, chaninfo_desc);

