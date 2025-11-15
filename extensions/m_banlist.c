/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_banlist.c: Enhanced ban management UI
 *
 * Copyright (c) 2024
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 */

#include "stdinc.h"
#include "modules.h"
#include "hook.h"
#include "client.h"
#include "ircd.h"
#include "send.h"
#include "s_user.h"
#include "channel.h"
#include "hash.h"
#include "numeric.h"
#include "parse.h"

static const char banlist_desc[] = "Provides enhanced ban management commands";

static void m_banlist(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void m_bansearch(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message banlist_msgtab = {
	"BANLIST", 0, 0, 0, 0,
	{mg_unreg, {m_banlist, 1}, mg_ignore, mg_ignore, mg_ignore, {m_banlist, 1}}
};

struct Message bansearch_msgtab = {
	"BANSEARCH", 0, 0, 0, 0,
	{mg_unreg, {m_bansearch, 2}, mg_ignore, mg_ignore, mg_ignore, {m_bansearch, 2}}
};

mapi_clist_av1 banlist_clist[] = { &banlist_msgtab, &bansearch_msgtab, NULL };

DECLARE_MODULE_AV2(m_banlist, NULL, NULL, banlist_clist, NULL, NULL, NULL, NULL, banlist_desc);

/* Enhanced BANLIST command: BANLIST <channel> [type] [search] */
/* type: "b" (bans), "e" (exempts), "I" (invex), "q" (quiets) */
static void
m_banlist(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr;
	rb_dlink_list *list = NULL;
	const char *type = "b";
	const char *search = NULL;
	rb_dlink_node *ptr;
	struct Ban *banptr;
	int count = 0;

	if (parc < 2 || EmptyString(parv[1]))
	{
		sendto_one_numeric(source_p, ERR_NEEDMOREPARAMS, form_str(ERR_NEEDMOREPARAMS), "BANLIST");
		return;
	}

	chptr = find_channel(parv[1]);
	if (chptr == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL, form_str(ERR_NOSUCHCHANNEL), parv[1]);
		return;
	}

	if (parc > 2 && !EmptyString(parv[2]))
		type = parv[2];
	if (parc > 3 && !EmptyString(parv[3]))
		search = parv[3];

	/* Determine which list to show */
	if (chptr != NULL)
	{
		switch (*type)
		{
			case 'b': list = &chptr->banlist; break;
			case 'e': list = &chptr->exceptlist; break;
			case 'I': list = &chptr->invexlist; break;
			case 'q': list = &chptr->quietlist; break;
			default: list = &chptr->banlist; break;
		}
	}
	else
	{
		return;
	}

	/* Check permissions */
	if (!is_chanop(find_channel_membership(chptr, source_p)) && !IsOper(source_p))
	{
		sendto_one_numeric(source_p, ERR_CHANOPRIVSNEEDED, form_str(ERR_CHANOPRIVSNEEDED), chptr->chname);
		return;
	}

	/* Send ban list */
	sendto_one_numeric(source_p, RPL_BANLIST, form_str(RPL_BANLIST), chptr->chname);
	
	RB_DLINK_FOREACH(ptr, list->head)
	{
		banptr = ptr->data;
		
		/* Apply search filter if provided */
		if (search && strstr(banptr->banstr, search) == NULL)
			continue;

		count++;
		sendto_one_numeric(source_p, RPL_BANLIST, "%s %s %s %lu",
			chptr->chname, banptr->banstr, banptr->who ? banptr->who : "*",
			(unsigned long)banptr->when);
	}

	sendto_one_numeric(source_p, RPL_ENDOFBANLIST, form_str(RPL_ENDOFBANLIST), chptr->chname);
}

/* BANSEARCH command: BANSEARCH <channel> <pattern> */
static void
m_bansearch(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr;
	rb_dlink_node *ptr;
	struct Ban *banptr;
	int count = 0;
	rb_dlink_list *lists[4];
	const char *types[] = { "b", "e", "I", "q" };
	int i;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2]))
	{
		sendto_one_numeric(source_p, ERR_NEEDMOREPARAMS, form_str(ERR_NEEDMOREPARAMS), "BANSEARCH");
		return;
	}

	chptr = find_channel(parv[1]);
	if (chptr == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL, form_str(ERR_NOSUCHCHANNEL), parv[1]);
		return;
	}

	/* Check permissions */
	if (!is_chanop(find_channel_membership(chptr, source_p)) && !IsOper(source_p))
	{
		sendto_one_numeric(source_p, ERR_CHANOPRIVSNEEDED, form_str(ERR_CHANOPRIVSNEEDED), chptr->chname);
		return;
	}

	/* Search all ban lists */
	lists[0] = &chptr->banlist;
	lists[1] = &chptr->exceptlist;
	lists[2] = &chptr->invexlist;
	lists[3] = &chptr->quietlist;

	for (i = 0; i < 4; i++)
	{
		RB_DLINK_FOREACH(ptr, lists[i]->head)
		{
			banptr = ptr->data;
			if (strstr(banptr->banstr, parv[2]) != NULL)
			{
				count++;
				sendto_one_numeric(source_p, RPL_BANLIST, "%s [%s] %s %s %lu",
					chptr->chname, types[i], banptr->banstr,
					banptr->who ? banptr->who : "*",
					(unsigned long)banptr->when);
			}
		}
	}

	if (count == 0)
		sendto_one_numeric(source_p, RPL_ENDOFBANLIST, form_str(ERR_NOSUCHNICK), parv[2]);
	else
		sendto_one_numeric(source_p, RPL_ENDOFBANLIST, form_str(RPL_ENDOFBANLIST), chptr->chname);
}

