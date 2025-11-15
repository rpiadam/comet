/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_gline.c: GLINE command for global bans
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
#include "s_newconf.h"
#include "hostmask.h"

static const char gline_desc[] = "Provides the GLINE command for global bans";

static void m_gline(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void ms_gline(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message gline_msgtab = {
	"GLINE", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_gline, 2}}
};

struct Message gungline_msgtab = {
	"GUNGLINE", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_gline, 1}}
};

mapi_clist_av1 gline_clist[] = { &gline_msgtab, &gungline_msgtab, NULL };

static void
m_gline(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char user[USERLEN + 2];
	char host[HOSTLEN + 3];
	char *reason = "No reason given";
	struct ConfItem *aconf;
	int tkline_time = 0;
	int loc = 1;

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: GLINE <user@host> [duration] :<reason>");
		return;
	}

	/* Parse user@host */
	if (strchr(parv[1], '@') == NULL) {
		sendto_one_notice(source_p, ":*** Invalid format. Use user@host");
		return;
	}

	sscanf(parv[1], "%[^@]@%s", user, host);

	/* Check for duration */
	if (parc > 2) {
		tkline_time = valid_temp_time(parv[2]);
		if (tkline_time >= 0)
			loc = 3;
	}

	/* Get reason */
	if (parc > loc && !EmptyString(parv[loc]))
		reason = (char *)parv[loc];

	/* Create GLINE (similar to KLINE but network-wide) */
	aconf = make_conf();
	aconf->status = CONF_KILL;
	aconf->lifetime = tkline_time;
	add_conf_by_address(host, NULL, user, NULL, aconf);
	aconf->passwd = rb_strdup(reason);

	sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s issued GLINE: %s@%s - %s",
		source_p->name, user, host, reason);
	sendto_one_notice(source_p, ":*** GLINE issued for %s@%s", user, host);
}

static void
ms_gline(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	/* Server-to-server GLINE propagation */
}

DECLARE_MODULE_AV2(gline, NULL, NULL, gline_clist, NULL, NULL, NULL, NULL, gline_desc);

