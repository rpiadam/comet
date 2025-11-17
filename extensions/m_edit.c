/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_edit.c: Message editing feature
 *
 * Copyright (c) 2024
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 */

#include "stdinc.h"
#include "client.h"
#include "channel.h"
#include "ircd.h"
#include "send.h"
#include "s_user.h"
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "numeric.h"
#include "match.h"
#include "hook.h"
#include "hash.h"

static const char edit_desc[] = "Provides message editing functionality";

static void m_edit(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void hook_privmsg_channel(void *);
static void hook_privmsg_user(void *);

mapi_hfn_list_av1 edit_hfnlist[] = {
	{ "privmsg_channel", hook_privmsg_channel },
	{ "privmsg_user", hook_privmsg_user },
	{ NULL, NULL }
};

struct Message edit_msgtab = {
	"EDIT", 0, 0, 0, 0,
	{mg_ignore, {m_edit, 2}, mg_ignore, mg_ignore, mg_ignore, {m_edit, 2}}
};

mapi_clist_av1 edit_clist[] = { &edit_msgtab, NULL };

struct tracked_message {
	char *msgid;
	struct Client *source_p;
	struct Channel *chptr;
	struct Client *target_p;
	char *text;
	time_t sent_time;
	rb_dlink_node node;
};

static rb_dictionary *edited_messages;
static rb_dictionary *tracked_messages;
static unsigned int msgid_counter = 0;

static char *
generate_msgid(struct Client *source_p)
{
	char msgid[64];
	snprintf(msgid, sizeof(msgid), "%s-%u-%lu", 
		source_p->id, msgid_counter++, (unsigned long)rb_current_time());
	return rb_strdup(msgid);
}

static void
hook_privmsg_channel(void *data_)
{
	hook_data_privmsg_channel *data = data_;
	struct tracked_message *msg;
	char *msgid;

	if (data->msgtype != MESSAGE_TYPE_PRIVMSG)
		return;

	/* Track message for editing/deletion */
	msg = rb_malloc(sizeof(struct tracked_message));
	msgid = generate_msgid(data->source_p);
	msg->msgid = msgid;
	msg->source_p = data->source_p;
	msg->chptr = data->chptr;
	msg->target_p = NULL;
	msg->text = rb_strdup(data->text);
	msg->sent_time = rb_current_time();

	rb_dictionary_add(tracked_messages, msgid, msg);
	rb_dictionary_add(edited_messages, msgid, msg);
}

static void
hook_privmsg_user(void *data_)
{
	hook_data_privmsg_user *data = data_;
	struct tracked_message *msg;
	char *msgid;

	if (data->msgtype != MESSAGE_TYPE_PRIVMSG)
		return;

	/* Track message for editing/deletion */
	msg = rb_malloc(sizeof(struct tracked_message));
	msgid = generate_msgid(data->source_p);
	msg->msgid = msgid;
	msg->source_p = data->source_p;
	msg->chptr = NULL;
	msg->target_p = data->target_p;
	msg->text = rb_strdup(data->text);
	msg->sent_time = rb_current_time();

	rb_dictionary_add(tracked_messages, msgid, msg);
	rb_dictionary_add(edited_messages, msgid, msg);
}

static void
m_edit(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct tracked_message *msg;
	const char *msgid;
	const char *newtext;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2])) {
		sendto_one_notice(source_p, ":*** Syntax: EDIT <msgid> <new text>");
		return;
	}

	msgid = parv[1];
	newtext = parv[2];

	/* Find original message */
	msg = rb_dictionary_retrieve(edited_messages, msgid);
	if (msg == NULL) {
		sendto_one_notice(source_p, ":*** Message not found or cannot be edited");
		return;
	}

	/* Check ownership */
	if (msg->source_p != source_p) {
		sendto_one_notice(source_p, ":*** You can only edit your own messages");
		return;
	}

	/* Update message */
	rb_free(msg->text);
	msg->text = rb_strdup(newtext);

	/* Send edit notification */
	if (msg->chptr != NULL) {
		/* Channel message - notify channel */
		sendto_channel_local(ALL_MEMBERS, msg->chptr,
			":%s NOTICE %s :Message %s edited by %s",
			me.name, msg->chptr->chname, msgid, source_p->name);
	} else if (msg->target_p != NULL) {
		/* Private message - notify recipient */
		sendto_one_notice(msg->target_p, ":*** Message %s from %s was edited",
			msgid, source_p->name);
	}

	sendto_one_notice(source_p, ":*** Message edited");
}

static int
modinit(void)
{
	edited_messages = rb_dictionary_create("edited_messages", rb_dictionary_str_casecmp);
	tracked_messages = rb_dictionary_create("tracked_messages", rb_dictionary_str_casecmp);
	return 0;
}

static void
moddeinit(void)
{
	rb_dictionary_iter iter;
	struct tracked_message *msg;

	RB_DICTIONARY_FOREACH(msg, &iter, edited_messages) {
		rb_free(msg->msgid);
		rb_free(msg->text);
		rb_free(msg);
	}

	if (edited_messages != NULL) {
		rb_dictionary_destroy(edited_messages, NULL, NULL);
		edited_messages = NULL;
	}

	if (tracked_messages != NULL) {
		rb_dictionary_destroy(tracked_messages, NULL, NULL);
		tracked_messages = NULL;
	}
}

DECLARE_MODULE_AV2(edit, modinit, moddeinit, edit_clist, NULL, edit_hfnlist, NULL, NULL, edit_desc);

