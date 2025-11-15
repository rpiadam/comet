/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_clearchan.c: CLEARCHAN command to clear channel modes/bans
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
#include "chmode.h"
#include "hash.h"

static const char clearchan_desc[] = "Provides the CLEARCHAN command to clear channel modes/bans";

static void m_clearchan(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message clearchan_msgtab = {
	"CLEARCHAN", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_clearchan, 1}}
};

mapi_clist_av1 clearchan_clist[] = { &clearchan_msgtab, NULL };

static void
m_clearchan(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr;
	const char *what = "all";

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: CLEARCHAN <channel> [bans|modes|all]");
		return;
	}

	chptr = find_channel(parv[1]);
	if (chptr == NULL) {
		sendto_one_notice(source_p, ":*** Channel %s not found", parv[1]);
		return;
	}

	if (parc > 2 && !EmptyString(parv[2]))
		what = parv[2];

	if (strcasecmp(what, "bans") == 0 || strcasecmp(what, "all") == 0) {
		/* Clear bans */
		rb_dlink_node *ptr, *next;
		int cleared = 0;

		RB_DLINK_FOREACH_SAFE(ptr, next, chptr->banlist.head) {
			struct Ban *ban = ptr->data;
			rb_dlinkDelete(ptr, &chptr->banlist);
			free_ban(ban);
			cleared++;
		}

		RB_DLINK_FOREACH_SAFE(ptr, next, chptr->quietlist.head) {
			struct Ban *ban = ptr->data;
			rb_dlinkDelete(ptr, &chptr->quietlist);
			free_ban(ban);
			cleared++;
		}

		sendto_one_notice(source_p, ":*** Cleared %d bans on %s", cleared, chptr->chname);
	}

	if (strcasecmp(what, "modes") == 0 || strcasecmp(what, "all") == 0) {
		/* Clear non-essential modes */
		chptr->mode.mode = 0;
		chptr->mode.limit = 0;
		chptr->mode.key[0] = '\0';
		sendto_one_notice(source_p, ":*** Cleared modes on %s", chptr->chname);
	}

	sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s cleared %s on %s",
		source_p->name, what, chptr->chname);
}

DECLARE_MODULE_AV2(clearchan, NULL, NULL, clearchan_clist, NULL, NULL, NULL, NULL, clearchan_desc);

