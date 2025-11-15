/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_seen.c: SEEN command to track when users were last seen online
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
#include "s_serv.h"
#include "logger.h"

static const char seen_desc[] = "Provides SEEN command to track when users were last seen online";

/* Structure to store last seen information */
struct seen_entry {
	char *nick;
	char *nick_lower;  /* lowercase nickname for dictionary key */
	char *username;
	char *host;
	char *action;  /* "message", "join", "part", "quit", etc. */
	char *channel; /* channel name if applicable, NULL otherwise */
	time_t last_seen;
	rb_dlink_node node;
};

/* Dictionary to store seen entries keyed by nickname */
static rb_dictionary_t *seen_dict;

/* Forward declarations */
static void m_seen(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void hook_privmsg_user_seen(void *);
static void hook_privmsg_channel_seen(void *);
static void hook_channel_join_seen(void *);
static void hook_channel_part_seen(void *);
static void hook_client_exit_seen(void *);
static void update_seen(struct Client *, const char *, const char *);
static void free_seen_entry(void *);

struct Message seen_msgtab = {
	"SEEN", 0, 0, 0, 0,
	{mg_unreg, {m_seen, 1}, mg_ignore, mg_ignore, mg_ignore, {m_seen, 1}}
};

mapi_clist_av1 seen_clist[] = { &seen_msgtab, NULL };

mapi_hfn_list_av1 seen_hfnlist[] = {
	{ "privmsg_user", hook_privmsg_user_seen },
	{ "privmsg_channel", hook_privmsg_channel_seen },
	{ "channel_join", hook_channel_join_seen },
	{ "channel_part", hook_channel_part_seen },
	{ "client_exit", hook_client_exit_seen },
	{ NULL, NULL }
};

static int modinit(void);
static void moddeinit(void);

DECLARE_MODULE_AV2(m_seen, modinit, moddeinit, seen_clist, NULL, seen_hfnlist, NULL, NULL, seen_desc);

/* Update or create a seen entry */
static void
update_seen(struct Client *client_p, const char *action, const char *channel)
{
	struct seen_entry *entry;
	char *nick_lower;
	char nick_buf[NICKLEN + 1];

	if (client_p == NULL || client_p->name == NULL)
		return;

	/* Convert nickname to lowercase for case-insensitive lookup */
	rb_strlcpy(nick_buf, client_p->name, sizeof(nick_buf));
	rb_strlwr(nick_buf);

	entry = rb_dictionary_retrieve(seen_dict, nick_buf);
	if (entry == NULL)
	{
		/* Create new entry */
		entry = rb_malloc(sizeof(struct seen_entry));
		entry->nick = rb_strdup(client_p->name);
		entry->nick_lower = rb_strdup(nick_buf);
		entry->username = client_p->username ? rb_strdup(client_p->username) : NULL;
		entry->host = client_p->host ? rb_strdup(client_p->host) : NULL;
		entry->action = rb_strdup(action);
		entry->channel = channel ? rb_strdup(channel) : NULL;
		entry->last_seen = rb_current_time();
		rb_dictionary_add(seen_dict, entry->nick_lower, entry);
	}
	else
	{
		/* Update existing entry */
		rb_free(entry->nick);
		entry->nick = rb_strdup(client_p->name);
		if (entry->username)
			rb_free(entry->username);
		entry->username = client_p->username ? rb_strdup(client_p->username) : NULL;
		if (entry->host)
			rb_free(entry->host);
		entry->host = client_p->host ? rb_strdup(client_p->host) : NULL;
		if (entry->action)
			rb_free(entry->action);
		entry->action = rb_strdup(action);
		if (entry->channel)
			rb_free(entry->channel);
		entry->channel = channel ? rb_strdup(channel) : NULL;
		entry->last_seen = rb_current_time();
	}
}

/* Free a seen entry */
static void
free_seen_entry(rb_dictionary_element *delem, void *privdata)
{
	struct seen_entry *entry;

	if (delem == NULL || delem->data == NULL)
		return;

	entry = delem->data;

	if (entry->nick)
		rb_free(entry->nick);
	if (entry->nick_lower)
		rb_free(entry->nick_lower);
	if (entry->username)
		rb_free(entry->username);
	if (entry->host)
		rb_free(entry->host);
	if (entry->action)
		rb_free(entry->action);
	if (entry->channel)
		rb_free(entry->channel);
	rb_free(entry);
}

/* Hook: user sent a private message */
static void
hook_privmsg_user_seen(void *data_)
{
	hook_data_privmsg_user *data = data_;

	if (data->msgtype == MESSAGE_TYPE_PRIVMSG && MyClient(data->source_p))
		update_seen(data->source_p, "message", NULL);
}

/* Hook: user sent a channel message */
static void
hook_privmsg_channel_seen(void *data_)
{
	hook_data_privmsg_channel *data = data_;

	if (data->msgtype == MESSAGE_TYPE_PRIVMSG && MyClient(data->source_p))
		update_seen(data->source_p, "message", data->chptr->chname);
}

/* Hook: user joined a channel */
static void
hook_channel_join_seen(void *data_)
{
	hook_data_channel_activity *data = data_;

	if (MyClient(data->client))
		update_seen(data->client, "join", data->chptr->chname);
}

/* Hook: user parted a channel */
static void
hook_channel_part_seen(void *data_)
{
	hook_data_channel_activity *data = data_;

	if (MyClient(data->client))
		update_seen(data->client, "part", data->chptr->chname);
}

/* Hook: user quit/disconnected */
static void
hook_client_exit_seen(void *data_)
{
	hook_data_client_exit *data = data_;

	if (MyClient(data->target))
		update_seen(data->target, "quit", NULL);
}

/* SEEN command handler */
static void
m_seen(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct seen_entry *entry;
	struct Client *target_p;
	char nick_buf[NICKLEN + 1];
	char time_buf[128];
	time_t now;
	time_t diff;
	int days, hours, minutes, seconds;

	if (parc < 2 || EmptyString(parv[1]))
	{
		sendto_one_numeric(source_p, ERR_NEEDMOREPARAMS, form_str(ERR_NEEDMOREPARAMS), "SEEN");
		return;
	}

	/* Check if user is currently online */
	target_p = find_person(parv[1]);
	if (target_p != NULL)
	{
		sendto_one_notice(source_p, ":*** %s is currently online", target_p->name);
		return;
	}

	/* Look up in seen database */
	rb_strlcpy(nick_buf, parv[1], sizeof(nick_buf));
	rb_strlwr(nick_buf);

	entry = rb_dictionary_retrieve(seen_dict, nick_buf);
	if (entry == NULL)
	{
		sendto_one_notice(source_p, ":*** I have not seen %s", parv[1]);
		return;
	}

	/* Format time */
	rb_ctime(entry->last_seen, time_buf, sizeof(time_buf));

	/* Calculate time difference */
	now = rb_current_time();
	diff = now - entry->last_seen;

	days = diff / 86400;
	hours = (diff % 86400) / 3600;
	minutes = (diff % 3600) / 60;
	seconds = diff % 60;

	/* Send response */
	sendto_one_notice(source_p, ":*** %s was last seen %s ago", entry->nick, time_buf);

	if (days > 0)
		sendto_one_notice(source_p, ":*** That was %d day%s, %d hour%s, %d minute%s, and %d second%s ago",
			days, days == 1 ? "" : "s",
			hours, hours == 1 ? "" : "s",
			minutes, minutes == 1 ? "" : "s",
			seconds, seconds == 1 ? "" : "s");
	else if (hours > 0)
		sendto_one_notice(source_p, ":*** That was %d hour%s, %d minute%s, and %d second%s ago",
			hours, hours == 1 ? "" : "s",
			minutes, minutes == 1 ? "" : "s",
			seconds, seconds == 1 ? "" : "s");
	else if (minutes > 0)
		sendto_one_notice(source_p, ":*** That was %d minute%s and %d second%s ago",
			minutes, minutes == 1 ? "" : "s",
			seconds, seconds == 1 ? "" : "s");
	else
		sendto_one_notice(source_p, ":*** That was %d second%s ago",
			seconds, seconds == 1 ? "" : "s");

	sendto_one_notice(source_p, ":*** Last action: %s", entry->action);
	if (entry->channel)
		sendto_one_notice(source_p, ":*** In channel: %s", entry->channel);
	if (entry->username && entry->host)
		sendto_one_notice(source_p, ":*** User: %s!%s@%s", entry->nick, entry->username, entry->host);
}

static int
modinit(void)
{
	seen_dict = rb_dictionary_create("seen", rb_dictionary_str_casecmp);
	if (seen_dict == NULL)
		return -1;
	return 0;
}

static void
moddeinit(void)
{
	if (seen_dict != NULL)
	{
		rb_dictionary_destroy(seen_dict, free_seen_entry);
		seen_dict = NULL;
	}
}

