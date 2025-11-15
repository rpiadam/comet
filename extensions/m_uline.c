/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_uline.c: ULINE command for uplink server management
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

static const char uline_desc[] = "Provides the ULINE command for uplink server management";

static void m_uline(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message uline_msgtab = {
	"ULINE", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_uline, 1}}
};

mapi_clist_av1 uline_clist[] = { &uline_msgtab, NULL };

static void
m_uline(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: ULINE <server>");
		return;
	}

	/* ULINE implementation would mark server as uplink */
	sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s issued ULINE: %s",
		source_p->name, parv[1]);
	sendto_one_notice(source_p, ":*** ULINE issued for %s", parv[1]);
}

DECLARE_MODULE_AV2(uline, NULL, NULL, uline_clist, NULL, NULL, NULL, NULL, uline_desc);

