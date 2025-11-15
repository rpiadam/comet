/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_sinfo.c: SINFO command for server information
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
#include "s_serv.h"

static const char sinfo_desc[] = "Provides the SINFO command to show server information";

static void m_sinfo(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message sinfo_msgtab = {
	"SINFO", 0, 0, 0, 0,
	{mg_ignore, {m_sinfo, 0}, mg_ignore, mg_ignore, mg_ignore, {m_sinfo, 0}}
};

mapi_clist_av1 sinfo_clist[] = { &sinfo_msgtab, NULL };

static void
m_sinfo(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	sendto_one_notice(source_p, ":*** Server: %s", me.name);
	sendto_one_notice(source_p, ":*** Network: %s", ServerInfo.network_name);
	sendto_one_notice(source_p, ":*** Uptime: %ld seconds", rb_current_time() - me.serv->boot_time);
	sendto_one_notice(source_p, ":*** Version: %s", version);
}

DECLARE_MODULE_AV2(sinfo, NULL, NULL, sinfo_clist, NULL, NULL, NULL, NULL, sinfo_desc);

