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
#include "s_serv.h"
#include "channel.h"
#include "hash.h"
#include "parse.h"
#include "numeric.h"
#include "msgbuf.h"

static const char chathistory_desc[] = "Provides CHATHISTORY command for querying message history";

static void m_chathistory(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message chathistory_msgtab = {
	"CHATHISTORY", 0, 0, 0, 0,
	{mg_unreg, {m_chathistory, 2}, mg_ignore, mg_ignore, mg_ignore, {m_chathistory, 2}}
};

mapi_clist_av1 chathistory_clist[] = { &chathistory_msgtab, NULL };

DECLARE_MODULE_AV2(m_chathistory, NULL, NULL, chathistory_clist, NULL, NULL, NULL, NULL, chathistory_desc);

/* Note: This requires chm_history extension to be loaded */
/* Access history through exported function */
extern rb_dictionary_t *chm_history_dict_get(void);

/* CHATHISTORY capability - defined in modules/cap_chathistory.c */
extern unsigned int CLICAP_CHATHISTORY;
/* Server-time capability - defined in modules/cap_server_time.c */
extern unsigned int CLICAP_SERVER_TIME;

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

/* Helper function to send history message with optional server-time tag */
static void
send_history_message(struct Client *source_p, struct history_entry *entry, struct Channel *chptr)
{
	if (CLICAP_SERVER_TIME != 0 && IsCapable(source_p, CLICAP_SERVER_TIME)) {
		char time_str[64];
		struct tm *tm_info = gmtime(&entry->timestamp);
		strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S.000Z", tm_info);
		sendto_one(source_p, "@time=%s :%s!%s@%s PRIVMSG %s :%s",
			time_str, entry->nick, entry->nick, "history", chptr->chname, entry->text);
	} else {
		sendto_one(source_p, ":%s!%s@%s PRIVMSG %s :%s",
			entry->nick, entry->nick, "history", chptr->chname, entry->text);
	}
}

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

	/* Check for CHATHISTORY capability */
	if (CLICAP_CHATHISTORY != 0 && !IsCapable(source_p, CLICAP_CHATHISTORY)) {
		sendto_one_notice(source_p, ":*** CHATHISTORY requires the draft/chathistory capability");
		return;
	}

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
	rb_dictionary_t *history_dict = chm_history_dict_get();
	if (history_dict == NULL)
	{
		sendto_one_notice(source_p, ":*** History not available (chm_history extension not loaded)");
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
			send_history_message(source_p, entry, chptr);
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
				send_history_message(source_p, entry, chptr);
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
			send_history_message(source_p, entry, chptr);
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
			send_history_message(source_p, entry, chptr);
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

