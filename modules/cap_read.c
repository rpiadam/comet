/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * cap_read.c: implement the draft/read IRCv3 capability
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
#include "s_serv.h"
#include "msgbuf.h"
#include "parse.h"
#include "hash.h"
#include "channel.h"

static const char cap_read_desc[] = "Provides the draft/read client capability for read receipts";

unsigned int CLICAP_READ = 0;

static void m_read(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void hook_privmsg_channel_read(void *);
static void hook_privmsg_user_read(void *);

struct Message read_msgtab = {
	"READ", 0, 0, 0, 0,
	{mg_unreg, {m_read, 2}, mg_ignore, mg_ignore, mg_ignore, {m_read, 2}}
};

mapi_clist_av1 read_clist[] = { &read_msgtab, NULL };

mapi_hfn_list_av1 read_hfnlist[] = {
	{ "privmsg_channel", hook_privmsg_channel_read },
	{ "privmsg_user", hook_privmsg_user_read },
	{ NULL, NULL }
};

mapi_cap_list_av2 cap_read_cap_list[] = {
	{ MAPI_CAP_CLIENT, "draft/read", NULL, &CLICAP_READ },
	{ 0, NULL, NULL, NULL },
};

DECLARE_MODULE_AV2(cap_read, NULL, NULL, read_clist, NULL, read_hfnlist, cap_read_cap_list, NULL, cap_read_desc);

/* Handle READ command: READ <target> <msgid> */
static void
m_read(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Client *target_p;
	struct Channel *chptr;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2]))
		return;

	if (!IsCapable(source_p, CLICAP_READ))
		return;

	/* Find target (channel or user) */
	if (IsChanPrefix(parv[1][0]))
	{
		chptr = find_channel(parv[1]);
		if (chptr == NULL || !IsMember(source_p, chptr))
			return;

		/* Send read receipt to all members with draft/read capability */
		rb_dlink_node *ptr;
		struct membership *msptr;
		RB_DLINK_FOREACH(ptr, chptr->members.head)
		{
			msptr = ptr->data;
			if (IsCapable(msptr->client_p, CLICAP_READ) && msptr->client_p != source_p)
			{
				sendto_one(msptr->client_p, ":%s READ %s %s",
					source_p->name, chptr->chname, parv[2]);
			}
		}
	}
	else
	{
		target_p = find_person(parv[1]);
		if (target_p == NULL || !IsCapable(target_p, CLICAP_READ))
			return;

		/* Send read receipt to target */
		sendto_one(target_p, ":%s READ %s %s",
			source_p->name, target_p->name, parv[2]);
	}
}

/* Add msgid tag to PRIVMSG/NOTICE for read receipts */
static void
hook_privmsg_channel_read(void *data_)
{
	hook_data_privmsg_channel *data = data_;
	struct MsgBuf *msgbuf = data->msgbuf;
	size_t i;
	bool has_msgid = false;

	if (!IsCapable(data->source_p, CLICAP_READ))
		return;

	/* Generate msgid if not present */
	for (i = 0; i < msgbuf->n_tags; i++)
	{
		if (msgbuf->tags[i].key && !strcmp(msgbuf->tags[i].key, "msgid"))
		{
			has_msgid = true;
			break;
		}
	}
	if (!has_msgid)
	{
		static unsigned int msgid_counter = 0;
		char msgid[64];
		snprintf(msgid, sizeof(msgid), "%s-%u-%lu",
			data->source_p->id, msgid_counter++, (unsigned long)rb_current_time());
		msgbuf_append_tag(msgbuf, "msgid", msgid, CLICAP_READ);
	}
}

static void
hook_privmsg_user_read(void *data_)
{
	hook_data_privmsg_user *data = data_;
	struct MsgBuf *msgbuf = data->msgbuf;
	size_t i;
	bool has_msgid = false;

	if (!IsCapable(data->source_p, CLICAP_READ))
		return;

	/* Generate msgid if not present */
	for (i = 0; i < msgbuf->n_tags; i++)
	{
		if (msgbuf->tags[i].key && !strcmp(msgbuf->tags[i].key, "msgid"))
		{
			has_msgid = true;
			break;
		}
	}
	if (!has_msgid)
	{
		static unsigned int msgid_counter = 0;
		char msgid[64];
		snprintf(msgid, sizeof(msgid), "%s-%u-%lu",
			data->source_p->id, msgid_counter++, (unsigned long)rb_current_time());
		msgbuf_append_tag(msgbuf, "msgid", msgid, CLICAP_READ);
	}
}

