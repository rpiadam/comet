/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_tline.c: TLINE command for temporary bans
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

static const char tline_desc[] = "Provides the TLINE command for temporary bans";

static void m_tline(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message tline_msgtab = {
	"TLINE", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_tline, 3}}
};

mapi_clist_av1 tline_clist[] = { &tline_msgtab, NULL };

static void
m_tline(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char user[USERLEN + 2];
	char host[HOSTLEN + 3];
	char *reason = "No reason given";
	struct ConfItem *aconf;
	int duration;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2])) {
		sendto_one_notice(source_p, ":*** Syntax: TLINE <user@host> <duration> :<reason>");
		return;
	}

	/* Parse user@host */
	if (strchr(parv[1], '@') == NULL) {
		sendto_one_notice(source_p, ":*** Invalid format. Use user@host");
		return;
	}

	sscanf(parv[1], "%[^@]@%s", user, host);

	duration = valid_temp_time(parv[2]);
	if (duration < 0) {
		sendto_one_notice(source_p, ":*** Invalid duration format");
		return;
	}

	/* Get reason */
	if (parc > 3 && !EmptyString(parv[3]))
		reason = (char *)parv[3];

	/* Create temporary ban */
	aconf = make_conf();
	aconf->status = CONF_KILL;
	aconf->lifetime = duration;
	aconf->flags |= CONF_FLAGS_TEMPORARY;
	add_conf_by_address(host, NULL, user, NULL, aconf);
	aconf->passwd = rb_strdup(reason);
	add_temp_kline(aconf);

	sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s issued TLINE: %s@%s for %d seconds - %s",
		source_p->name, user, host, duration, reason);
	sendto_one_notice(source_p, ":*** TLINE issued for %s@%s (duration: %d seconds)", user, host, duration);
}

DECLARE_MODULE_AV2(tline, NULL, NULL, tline_clist, NULL, NULL, NULL, NULL, tline_desc);

