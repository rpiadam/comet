/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_setname.c: SETNAME command for changing realname/gecos
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

static const char setname_desc[] = "Provides the SETNAME command to change realname/gecos";

static void m_setname(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message setname_msgtab = {
	"SETNAME", 0, 0, 0, 0,
	{mg_ignore, {m_setname, 1}, mg_ignore, mg_ignore, mg_ignore, mg_ignore}
};

mapi_clist_av1 setname_clist[] = { &setname_msgtab, NULL };

static void
m_setname(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	if (!MyClient(source_p))
		return;

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_numeric(source_p, ERR_NEEDMOREPARAMS, form_str(ERR_NEEDMOREPARAMS), "SETNAME");
		return;
	}

	if (strlen(parv[1]) > REALLEN) {
		sendto_one_notice(source_p, ":*** Realname too long (max %d characters)", REALLEN);
		return;
	}

	rb_strlcpy(source_p->info, parv[1], sizeof(source_p->info));
	sendto_common_channels_local(source_p, NOCAPS, NOCAPS, ":%s!%s@%s CHGHOST %s :%s",
		source_p->name, source_p->username, source_p->host,
		source_p->host, source_p->info);
	sendto_one_notice(source_p, ":*** Realname changed to: %s", source_p->info);
}

DECLARE_MODULE_AV2(setname, NULL, NULL, setname_clist, NULL, NULL, NULL, NULL, setname_desc);

