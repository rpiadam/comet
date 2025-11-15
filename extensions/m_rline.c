/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_rline.c: RLINE command for realname/gecos bans (alias for XLINE)
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
#include "match.h"
#include "bandbi.h"
#include "logger.h"

/* Forward declaration - apply_xline is in modules/m_xline.c */
extern void apply_xline(struct Client *source_p, const char *name, const char *reason, int temp_time, bool propagated);
extern struct ConfItem *find_xline_mask(const char *);

static const char rline_desc[] = "Provides the RLINE command for realname/gecos bans (alias for XLINE)";

static void m_rline(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void m_unrline(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message rline_msgtab = {
	"RLINE", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_rline, 2}}
};

struct Message unrline_msgtab = {
	"UNRLINE", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_unrline, 1}}
};

mapi_clist_av1 rline_clist[] = { &rline_msgtab, &unrline_msgtab, NULL };

static bool
valid_rline(struct Client *source_p, const char *gecos, const char *reason)
{
	if(EmptyString(reason))
	{
		sendto_one(source_p, form_str(ERR_NEEDMOREPARAMS),
			   get_id(&me, source_p), get_id(source_p, source_p), "RLINE");
		return false;
	}

	if(!valid_wild_card_simple(gecos))
	{
		sendto_one_notice(source_p,
				  ":Please include at least %d non-wildcard "
				  "characters with the rline",
				  ConfigFileEntry.min_nonwildcard_simple);
		return false;
	}

	return true;
}

static void
m_rline(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char *mask;
	char *reason = "No reason given";
	int duration = 0;
	int loc = 1;

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: RLINE [duration] <gecos_mask> :<reason>");
		return;
	}

	/* Check for duration */
	if (parc > 2) {
		duration = valid_temp_time(parv[1]);
		if (duration >= 0) {
			loc = 2;
		}
	}

	mask = (char *)parv[loc];
	if (parc > loc + 1 && !EmptyString(parv[loc + 1]))
		reason = (char *)parv[loc + 1];

	if(!valid_rline(source_p, mask, reason))
		return;

	/* Check if already rlined */
	if(find_xline_mask(mask) != NULL)
	{
		sendto_one_notice(source_p, ":[%s] already R-Lined", mask);
		return;
	}

	/* Create RLINE using apply_xline (same as XLINE) */
	apply_xline(source_p, mask, reason, duration, false);

	sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s issued RLINE: %s - %s",
		source_p->name, mask, reason);
}

static void
m_unrline(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char *mask;
	struct ConfItem *aconf;
	rb_dlink_node *ptr;

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: UNRLINE <gecos_mask>");
		return;
	}

	mask = (char *)parv[1];

	/* Find and remove RLINE (XLINE) - duplicate remove_xline logic */
	RB_DLINK_FOREACH(ptr, xline_conf_list.head)
	{
		aconf = ptr->data;

		if(!irccmp(aconf->host, mask))
		{
			rb_dlinkDelete(ptr, &xline_conf_list);
			bandb_del(BANDB_XLINE, aconf->host, NULL);
			free_conf(aconf);
			check_xlines();
			sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s removed RLINE: %s",
				source_p->name, mask);
			sendto_one_notice(source_p, ":*** RLINE removed for %s", mask);
			ilog(L_KLINE, "R %s 0 %s", get_oper_name(source_p), mask);
			return;
		}
	}

	sendto_one_notice(source_p, ":*** No RLINE found for %s", mask);
}

DECLARE_MODULE_AV2(rline, NULL, NULL, rline_clist, NULL, NULL, NULL, NULL, rline_desc);

