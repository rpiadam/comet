/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_search.c: SEARCH command for searching channel messages
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
#include "match.h"

static const char search_desc[] = "Provides SEARCH command for searching channel messages";

static void m_search(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message search_msgtab = {
	"SEARCH", 0, 0, 0, 0,
	{mg_unreg, {m_search, 2}, mg_ignore, mg_ignore, mg_ignore, {m_search, 2}}
};

mapi_clist_av1 search_clist[] = { &search_msgtab, NULL };

DECLARE_MODULE_AV2(m_search, NULL, NULL, search_clist, NULL, NULL, NULL, NULL, search_desc);

/* Access history from chm_history extension */
extern rb_dictionary *chm_history_dict_get(void);

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
m_search(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr;
	const char *target;
	const char *query;
	rb_dlink_node *ptr;
	struct membership *msptr;
	int count = 0;
	int limit = 20;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2]))
	{
		sendto_one_numeric(source_p, ERR_NEEDMOREPARAMS, form_str(ERR_NEEDMOREPARAMS), "SEARCH");
		return;
	}

	target = parv[1];
	query = parv[2];

	if (parc > 3 && !EmptyString(parv[3]))
		limit = atoi(parv[3]);
	if (limit > 50)
		limit = 50;
	if (limit < 1)
		limit = 20;

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

	if (!IsMember(source_p, chptr) && !IsOper(source_p))
	{
		sendto_one_numeric(source_p, ERR_NOTONCHANNEL, form_str(ERR_NOTONCHANNEL), chptr->chname);
		return;
	}

	/* Search message history if available */
	rb_dictionary *history_dict = chm_history_dict_get();
	struct channel_history *hist = NULL;
	if (history_dict != NULL) {
		hist = rb_dictionary_retrieve(history_dict, chptr->chname);
	}

	if (hist != NULL && rb_dlink_list_length(&hist->messages) > 0) {
		/* Search message history */
		struct history_entry *entry;
		sendto_one_notice(source_p, ":*** Searching message history in %s for: %s", chptr->chname, query);

		RB_DLINK_FOREACH_REVERSE(ptr, hist->messages.tail)
		{
			if (count >= limit)
				break;

			entry = ptr->data;
			/* Case-insensitive search in message text */
			if (entry->text != NULL && strcasestr(entry->text, query) != NULL) {
				count++;
				char time_str[64];
				struct tm *tm_info = localtime(&entry->timestamp);
				strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
				sendto_one_notice(source_p, ":*** [%s] <%s> %s", time_str, entry->nick, entry->text);
			}
		}

		sendto_one_notice(source_p, ":*** Search complete (%d message matches)", count);
	} else {
		/* Fallback: Search current channel members for matching nicks */
		sendto_one_notice(source_p, ":*** Searching members in %s for: %s", chptr->chname, query);

		RB_DLINK_FOREACH(ptr, chptr->members.head)
		{
			if (count >= limit)
				break;

			msptr = ptr->data;
			if (match(query, msptr->client_p->name) == 0)
			{
				count++;
				if (is_chanop(msptr))
					sendto_one_notice(source_p, ":*** @%s!%s@%s", msptr->client_p->name,
						msptr->client_p->username, msptr->client_p->host);
				else if (is_halfop(msptr))
					sendto_one_notice(source_p, ":*** %%%s!%s@%s", msptr->client_p->name,
						msptr->client_p->username, msptr->client_p->host);
				else if (is_voiced(msptr))
					sendto_one_notice(source_p, ":*** +%s!%s@%s", msptr->client_p->name,
						msptr->client_p->username, msptr->client_p->host);
				else
					sendto_one_notice(source_p, ":*** %s!%s@%s", msptr->client_p->name,
						msptr->client_p->username, msptr->client_p->host);
			}
		}

		sendto_one_notice(source_p, ":*** Search complete (%d member matches)", count);
	}
}

