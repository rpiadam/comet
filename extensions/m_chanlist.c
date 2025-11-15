/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_chanlist.c: CHANLIST command to list channels with filters
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
#include "hash.h"

static const char chanlist_desc[] = "Provides the CHANLIST command to list channels with filters";

static void m_chanlist(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message chanlist_msgtab = {
	"CHANLIST", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_chanlist, 0}}
};

mapi_clist_av1 chanlist_clist[] = { &chanlist_msgtab, NULL };

static void
m_chanlist(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr;
	rb_dictionary_iter iter;
	int count = 0;
	int max = 100;

	if (parc > 1 && !EmptyString(parv[1]))
		max = atoi(parv[1]);

	sendto_one_notice(source_p, ":*** Channel List (showing up to %d channels):", max);

	RB_DICTIONARY_FOREACH(chptr, &iter, channel_dict) {
		if (count >= max)
			break;

		sendto_one_notice(source_p, ":*** %s - %u members", chptr->chname,
			rb_dlink_list_length(&chptr->members));
		count++;
	}

	sendto_one_notice(source_p, ":*** End of channel list (%d channels shown)", count);
}

DECLARE_MODULE_AV2(chanlist, NULL, NULL, chanlist_clist, NULL, NULL, NULL, NULL, chanlist_desc);

