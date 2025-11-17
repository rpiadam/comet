/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_topic_history.c: TOPICHISTORY command for viewing topic change history
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

static const char topic_history_desc[] = "Provides TOPICHISTORY command for viewing topic change history";

static void m_topic_history(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
struct topic_history_entry {
	char *topic;
	char *setter;
	time_t timestamp;
	rb_dlink_node node;
};

struct channel_topic_history {
	struct Channel *chptr;
	rb_dlink_list history;
	rb_dlink_node node;
};

static rb_dictionary *topic_history_dict;

struct Message topic_history_msgtab = {
	"TOPICHISTORY", 0, 0, 0, 0,
	{mg_unreg, {m_topic_history, 1}, mg_ignore, mg_ignore, mg_ignore, {m_topic_history, 1}}
};

mapi_clist_av1 topic_history_clist[] = { &topic_history_msgtab, NULL };

/* Note: Topic history tracking would require hooking into set_channel_topic() */
/* For now, this command shows the current topic information */
/* Full history tracking can be added when a topic_change hook is available */

static void
m_topic_history(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr;
	struct channel_topic_history *hist;
	rb_dlink_node *ptr;
	struct topic_history_entry *entry;
	int count = 0;

	if (parc < 2 || EmptyString(parv[1]))
	{
		sendto_one_numeric(source_p, ERR_NEEDMOREPARAMS, form_str(ERR_NEEDMOREPARAMS), "TOPICHISTORY");
		return;
	}

	chptr = find_channel(parv[1]);
	if (chptr == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL, form_str(ERR_NOSUCHCHANNEL), parv[1]);
		return;
	}

	if (!IsMember(source_p, chptr) && !IsOper(source_p))
	{
		sendto_one_numeric(source_p, ERR_NOTONCHANNEL, form_str(ERR_NOTONCHANNEL), chptr->chname);
		return;
	}

	/* Show current topic information */
	if (chptr->topic == NULL)
	{
		sendto_one_notice(source_p, ":*** No topic set for %s", chptr->chname);
		return;
	}

	sendto_one_notice(source_p, ":*** Current topic for %s:", chptr->chname);
	sendto_one_notice(source_p, ":*** Topic: %s", chptr->topic);
	if (chptr->topic_info != NULL)
		sendto_one_notice(source_p, ":*** Set by: %s on %s", chptr->topic_info, ctime(&chptr->topic_time));
	sendto_one_notice(source_p, ":*** Note: Full topic history requires topic_change hook support");
}

static int
modinit(void)
{
	topic_history_dict = rb_dictionary_create("topic_history", rb_dictionary_str_casecmp);
	return 0;
}

static void
moddeinit(void)
{
	if (topic_history_dict != NULL)
	{
		rb_dictionary_iter iter;
		struct channel_topic_history *hist;
		rb_dlink_node *ptr, *next_ptr;

		RB_DICTIONARY_FOREACH(hist, &iter, topic_history_dict)
		{
			RB_DLINK_FOREACH_SAFE(ptr, next_ptr, hist->history.head)
			{
				struct topic_history_entry *entry = ptr->data;
				rb_dlinkDelete(ptr, &hist->history);
				rb_free(entry->topic);
				rb_free(entry->setter);
				rb_free(entry);
			}
		}
		rb_dictionary_destroy(topic_history_dict, NULL, NULL);
		topic_history_dict = NULL;
	}
}

DECLARE_MODULE_AV2(m_topic_history, modinit, moddeinit, topic_history_clist, NULL, NULL, NULL, NULL, topic_history_desc);

