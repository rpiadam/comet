/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * chm_history.c: channel mode +H (history)
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
#include "chmode.h"
#include "hash.h"
#include "match.h"

static const char chm_history_desc[] = "Adds channel mode +H, which stores and replays recent messages";

static unsigned int mode_history;
#define MAX_HISTORY_MESSAGES 100
#define DEFAULT_HISTORY_EXPIRE_TIME 3600  /* 1 hour default expiration */

static time_t history_expire_time = DEFAULT_HISTORY_EXPIRE_TIME;
static struct ev_entry *history_expire_ev;

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

static rb_dictionary_t *history_dict;

/* Export history_dict for use by other modules */
rb_dictionary_t *chm_history_dict_get(void)
{
	return history_dict;
}

static void hook_privmsg_channel(void *);
static void hook_channel_join(void *);
static void expire_history_messages(void *);

mapi_hfn_list_av1 chm_history_hfnlist[] = {
	{ "privmsg_channel", hook_privmsg_channel },
	{ "channel_join", hook_channel_join },
	{ NULL, NULL }
};

static struct channel_history *
get_channel_history(struct Channel *chptr)
{
	struct channel_history *hist;

	hist = rb_dictionary_retrieve(history_dict, chptr->chname);
	if (hist == NULL) {
		hist = rb_malloc(sizeof(struct channel_history));
		hist->chptr = chptr;
		rb_dictionary_add(history_dict, chptr->chname, hist);
	}

	return hist;
}

static void
add_history_message(struct Channel *chptr, struct Client *source_p, const char *text)
{
	struct channel_history *hist;
	struct history_entry *entry;

	if (!(chptr->mode.mode & mode_history))
		return;

	hist = get_channel_history(chptr);

	entry = rb_malloc(sizeof(struct history_entry));
	entry->nick = rb_strdup(source_p->name);
	entry->text = rb_strdup(text);
	entry->timestamp = rb_current_time();

	rb_dlinkAdd(entry, &entry->node, &hist->messages);

	/* Limit history size */
	while (rb_dlink_list_length(&hist->messages) > MAX_HISTORY_MESSAGES) {
		rb_dlink_node *ptr = hist->messages.head;
		struct history_entry *old = ptr->data;
		rb_dlinkDelete(ptr, &hist->messages);
		rb_free(old->nick);
		rb_free(old->text);
		rb_free(old);
	}
}

static void
expire_history_messages(void *unused)
{
	rb_dictionary_iter iter;
	struct channel_history *hist;
	time_t now = rb_current_time();
	time_t expire_time = now - history_expire_time;

	RB_DICTIONARY_FOREACH(hist, &iter, history_dict) {
		rb_dlink_node *ptr, *next;
		
		RB_DLINK_FOREACH_SAFE(ptr, next, hist->messages.head) {
			struct history_entry *entry = ptr->data;
			
			/* Remove expired messages */
			if (entry->timestamp < expire_time) {
				rb_dlinkDelete(ptr, &hist->messages);
				rb_free(entry->nick);
				rb_free(entry->text);
				rb_free(entry);
			}
		}
		
		/* Clean up empty history entries */
		if (rb_dlink_list_length(&hist->messages) == 0) {
			rb_dictionary_delete(history_dict, hist->chptr->chname);
			rb_free(hist);
		}
	}
}

static void
replay_history(struct Client *client_p, struct Channel *chptr)
{
	struct channel_history *hist;
	rb_dlink_node *ptr;
	struct history_entry *entry;
	int count = 0;
	int max = 20;

	if (!(chptr->mode.mode & mode_history))
		return;

	hist = rb_dictionary_retrieve(history_dict, chptr->chname);
	if (hist == NULL)
		return;

	/* Send last N messages */
	RB_DLINK_FOREACH_REVERSE(ptr, hist->messages.tail) {
		if (count >= max)
			break;
		entry = ptr->data;
		/* Send recent messages to joining user */
		sendto_one(client_p, ":%s!%s@%s PRIVMSG %s :%s",
			entry->nick, entry->nick, "history", chptr->chname, entry->text);
		count++;
	}
}

static void
hook_privmsg_channel(void *data_)
{
	hook_data_privmsg_channel *data = data_;

	if (data->msgtype == MESSAGE_TYPE_PRIVMSG)
		add_history_message(data->chptr, data->source_p, data->text);
}

static void
hook_channel_join(void *data_)
{
	hook_data_channel_activity *data = data_;

	if (MyClient(data->client) && data->chptr != NULL)
		replay_history(data->client, data->chptr);
}

static int
_modinit(void)
{
	mode_history = cflag_add('H', chm_simple);
	if (mode_history == 0) {
		ierror("chm_history: unable to allocate cmode slot for +H");
		return -1;
	}

	history_dict = rb_dictionary_create("channel_history", rb_dictionary_str_casecmp);
	history_expire_ev = rb_event_addish("history_expire", expire_history_messages, NULL, 300);
	return 0;
}

static void
_moddeinit(void)
{
	rb_dictionary_iter iter;
	struct channel_history *hist;
	rb_dlink_node *ptr, *next;

	if (history_expire_ev != NULL) {
		rb_event_delete(history_expire_ev);
		history_expire_ev = NULL;
	}

	RB_DICTIONARY_FOREACH(hist, &iter, history_dict) {
		RB_DLINK_FOREACH_SAFE(ptr, next, hist->messages.head) {
			struct history_entry *entry = ptr->data;
			rb_dlinkDelete(ptr, &hist->messages);
			rb_free(entry->nick);
			rb_free(entry->text);
			rb_free(entry);
		}
		rb_free(hist);
	}

	if (history_dict != NULL) {
		rb_dictionary_destroy(history_dict, NULL, NULL);
		history_dict = NULL;
	}

	cflag_orphan('H');
}

DECLARE_MODULE_AV2(chm_history, _modinit, _moddeinit, NULL, NULL, chm_history_hfnlist, NULL, NULL, chm_history_desc);

