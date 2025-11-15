/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_sadmin.c: SADMIN command for server admin info
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

static const char sadmin_desc[] = "Provides the SADMIN command to show server admin information";

static void m_sadmin(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message sadmin_msgtab = {
	"SADMIN", 0, 0, 0, 0,
	{mg_ignore, {m_sadmin, 0}, mg_ignore, mg_ignore, mg_ignore, {m_sadmin, 0}}
};

mapi_clist_av1 sadmin_clist[] = { &sadmin_msgtab, NULL };

static void
m_sadmin(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	sendto_one(source_p, form_str(RPL_ADMINME), me.name);
	sendto_one(source_p, form_str(RPL_ADMINLOC1), me.name, ServerInfo.admin_location1);
	sendto_one(source_p, form_str(RPL_ADMINLOC2), me.name, ServerInfo.admin_location2);
	sendto_one(source_p, form_str(RPL_ADMINEMAIL), me.name, ServerInfo.admin_email);
}

DECLARE_MODULE_AV2(sadmin, NULL, NULL, sadmin_clist, NULL, NULL, NULL, NULL, sadmin_desc);

