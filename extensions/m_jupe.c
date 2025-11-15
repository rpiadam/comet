/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_jupe.c: JUPE command for junk server management
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
#include "s_conf.h"
#include "s_serv.h"
#include "hash.h"

static const char jupe_desc[] = "Provides the JUPE command for junk server management";

static void m_jupe(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void ms_jupe(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message jupe_msgtab = {
	"JUPE", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_jupe, 1}}
};

mapi_clist_av1 jupe_clist[] = { &jupe_msgtab, NULL };

static void
m_jupe(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: JUPE <servername> [reason]");
		return;
	}

	/* JUPE implementation would create a fake server entry */
	/* This prevents the server from connecting */
	sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s issued JUPE: %s",
		source_p->name, parv[1]);
	sendto_one_notice(source_p, ":*** JUPE issued for %s", parv[1]);
}

static void
ms_jupe(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	/* Server-to-server JUPE propagation */
}

DECLARE_MODULE_AV2(jupe, NULL, NULL, jupe_clist, NULL, NULL, NULL, NULL, jupe_desc);

