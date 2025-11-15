/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_chansearch.c: CHANSEARCH command to search channels
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
#include "match.h"

static const char chansearch_desc[] = "Provides the CHANSEARCH command to search channels";

static void m_chansearch(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message chansearch_msgtab = {
	"CHANSEARCH", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_chansearch, 1}}
};

mapi_clist_av1 chansearch_clist[] = { &chansearch_msgtab, NULL };

static void
m_chansearch(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr;
	rb_dictionary_iter iter;
	int count = 0;
	int max = 50;

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: CHANSEARCH <pattern> [max]");
		return;
	}

	if (parc > 2 && !EmptyString(parv[2]))
		max = atoi(parv[2]);

	sendto_one_notice(source_p, ":*** Searching for channels matching: %s", parv[1]);

	RB_DICTIONARY_FOREACH(chptr, &iter, channel_dict) {
		if (count >= max)
			break;

		if (match(parv[1], chptr->chname) || 
		    (chptr->topic && match(parv[1], chptr->topic))) {
			sendto_one_notice(source_p, ":*** %s - %u members - %s",
				chptr->chname, rb_dlink_list_length(&chptr->members),
				chptr->topic ? chptr->topic : "(no topic)");
			count++;
		}
	}

	sendto_one_notice(source_p, ":*** End of search (%d channels found)", count);
}

DECLARE_MODULE_AV2(chansearch, NULL, NULL, chansearch_clist, NULL, NULL, NULL, NULL, chansearch_desc);

