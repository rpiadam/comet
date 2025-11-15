/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_chathistory.c: CHATHISTORY command for querying message history
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
#include "parse.h"
#include "numeric.h"

static const char chathistory_desc[] = "Provides CHATHISTORY command for querying message history";

static void m_chathistory(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message chathistory_msgtab = {
	"CHATHISTORY", 0, 0, 0, 0,
	{mg_unreg, {m_chathistory, 2}, mg_ignore, mg_ignore, mg_ignore, {m_chathistory, 2}}
};

mapi_clist_av1 chathistory_clist[] = { &chathistory_msgtab, NULL };

DECLARE_MODULE_AV2(m_chathistory, NULL, NULL, chathistory_clist, NULL, NULL, NULL, NULL, chathistory_desc);

/* Note: This requires chm_history extension to be loaded */
/* The history_dict is internal to chm_history, so we'll need to query it differently */
/* For now, this is a placeholder that can be enhanced when chm_history exports its API */

struct history_entry {
	char *nick;
	char *text;
	time_t timestamp;
	rb_dlink_node node;
};

struct channel_history {
	struct Channel *chptr;
	rb_dlink_list messages;
	rb_dlink_node node;
};

static void
m_chathistory(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr;
	const char *target;
	const char *query_type;
	const char *query_param;
	struct channel_history *hist;
	rb_dlink_node *ptr;
	struct history_entry *entry;
	int count = 0;
	int limit = 50;
	time_t target_time = 0;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2]))
	{
		sendto_one_numeric(source_p, ERR_NEEDMOREPARAMS, form_str(ERR_NEEDMOREPARAMS), "CHATHISTORY");
		return;
	}

	if (!IsCapable(source_p, CLICAP_CHATHISTORY))
		return;

	target = parv[1];
	query_type = parv[2];
	query_param = parc > 3 ? parv[3] : NULL;

	if (parc > 4 && !EmptyString(parv[4]))
		limit = atoi(parv[4]);
	if (limit > 100)
		limit = 100;
	if (limit < 1)
		limit = 50;

	if (!IsChanPrefix(target[0]))
	{
		sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL, form_str(ERR_NOSUCHCHANNEL), target);
		return;
	}

	chptr = find_channel(target);
	if (chptr == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL, form_str(ERR_NOSUCHCHANNEL), target);
		return;
	}

	if (!IsMember(source_p, chptr))
	{
		sendto_one_numeric(source_p, ERR_NOTONCHANNEL, form_str(ERR_NOTONCHANNEL), chptr->chname);
		return;
	}

	/* Get history from chm_history extension if available */
	if (history_dict == NULL)
	{
		sendto_one_notice(source_p, ":*** History not available for this channel");
		return;
	}

	hist = rb_dictionary_retrieve(history_dict, chptr->chname);
	if (hist == NULL)
	{
		sendto_one_notice(source_p, ":*** No history available for %s", chptr->chname);
		return;
	}

	if (query_param != NULL)
		target_time = atoi(query_param);

	/* Query types: LATEST, AROUND, BEFORE, AFTER */
	if (!strcmp(query_type, "LATEST"))
	{
		/* Send latest N messages */
		RB_DLINK_FOREACH_REVERSE(ptr, hist->messages.tail)
		{
			if (count >= limit)
				break;
			entry = ptr->data;
			sendto_one(source_p, ":%s!%s@%s PRIVMSG %s :%s",
				entry->nick, entry->nick, "history", chptr->chname, entry->text);
			count++;
		}
	}
	else if (!strcmp(query_type, "AROUND") && query_param != NULL)
	{
		/* Send messages around a timestamp */
		rb_dlink_node *start_ptr = NULL;
		RB_DLINK_FOREACH(ptr, hist->messages.head)
		{
			entry = ptr->data;
			if (entry->timestamp >= target_time)
			{
				start_ptr = ptr;
				break;
			}
		}
		if (start_ptr != NULL)
		{
			/* Send messages before and after */
			rb_dlink_node *p;
			int before = limit / 2;
			int after = limit - before;
			int sent = 0;

			/* Go backwards first */
			p = start_ptr;
			for (int i = 0; i < before && p != NULL; i++)
			{
				if (p->prev == NULL)
					break;
				p = p->prev;
			}

			/* Send from here */
			for (p = p ? p : hist->messages.head; p != NULL && sent < limit; p = p->next)
			{
				entry = p->data;
				if (abs((long)(entry->timestamp - target_time)) > 3600) /* 1 hour window */
					break;
				sendto_one(source_p, ":%s!%s@%s PRIVMSG %s :%s",
					entry->nick, entry->nick, "history", chptr->chname, entry->text);
				sent++;
			}
		}
	}
	else if (!strcmp(query_type, "BEFORE") && query_param != NULL)
	{
		/* Send messages before a timestamp */
		RB_DLINK_FOREACH_REVERSE(ptr, hist->messages.tail)
		{
			if (count >= limit)
				break;
			entry = ptr->data;
			if (entry->timestamp >= target_time)
				continue;
			sendto_one(source_p, ":%s!%s@%s PRIVMSG %s :%s",
				entry->nick, entry->nick, "history", chptr->chname, entry->text);
			count++;
		}
	}
	else if (!strcmp(query_type, "AFTER") && query_param != NULL)
	{
		/* Send messages after a timestamp */
		RB_DLINK_FOREACH(ptr, hist->messages.head)
		{
			if (count >= limit)
				break;
			entry = ptr->data;
			if (entry->timestamp <= target_time)
				continue;
			sendto_one(source_p, ":%s!%s@%s PRIVMSG %s :%s",
				entry->nick, entry->nick, "history", chptr->chname, entry->text);
			count++;
		}
	}
	else
	{
		sendto_one_notice(source_p, ":*** Invalid query type. Use: LATEST, AROUND, BEFORE, or AFTER");
		return;
	}

	sendto_one_notice(source_p, ":*** End of history for %s (%d messages)", chptr->chname, count);
}

