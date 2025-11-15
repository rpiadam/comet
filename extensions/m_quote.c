/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_quote.c: QUOTE command for random quotes
 *
 * Copyright (c) 2024
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 */

#include "stdinc.h"
#include "client.h"
#include "ircd.h"
#include "send.h"
#include "s_user.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "numeric.h"
#include "channel.h"

static const char quote_desc[] = "Provides the QUOTE command for random quotes";

static void m_quote(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message quote_msgtab = {
	"QUOTE", 0, 0, 0, 0,
	{mg_ignore, {m_quote, 0}, mg_ignore, mg_ignore, mg_ignore, {m_quote, 0}}
};

mapi_clist_av1 quote_clist[] = { &quote_msgtab, NULL };

static const char *quotes[] = {
	"The only way to do great work is to love what you do. - Steve Jobs",
	"Life is what happens to you while you're busy making other plans. - John Lennon",
	"Get busy living or get busy dying. - Stephen King",
	"The future belongs to those who believe in the beauty of their dreams. - Eleanor Roosevelt",
	"It is during our darkest moments that we must focus to see the light. - Aristotle",
	NULL
};

static void
m_quote(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	int count = 0;
	int i;
	const char *quote;

	/* Count quotes */
	for (i = 0; quotes[i] != NULL; i++)
		count++;

	if (count == 0) {
		sendto_one_notice(source_p, ":*** No quotes available");
		return;
	}

	quote = quotes[rb_random() % count];

	if (parc > 1 && !EmptyString(parv[1])) {
		/* Send to channel */
		struct Channel *chptr = find_channel(parv[1]);
		if (chptr != NULL) {
			sendto_channel_local(ALL_MEMBERS, chptr, ":*** %s", quote);
			return;
		}
	}

	sendto_one_notice(source_p, ":*** %s", quote);
}

DECLARE_MODULE_AV2(quote, NULL, NULL, quote_clist, NULL, NULL, NULL, NULL, quote_desc);

