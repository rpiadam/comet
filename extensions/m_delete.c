/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_delete.c: Message deletion feature
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
#include "hash.h"

static const char delete_desc[] = "Provides message deletion functionality";

static void m_delete(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message delete_msgtab = {
	"DELETE", 0, 0, 0, 0,
	{mg_ignore, {m_delete, 1}, mg_ignore, mg_ignore, mg_ignore, {m_delete, 1}}
};

mapi_clist_av1 delete_clist[] = { &delete_msgtab, NULL };

/* Share tracked messages with m_edit */
extern rb_dictionary_t *tracked_messages;

struct tracked_message {
	char *msgid;
	struct Client *source_p;
	struct Channel *chptr;
	struct Client *target_p;
	char *text;
	time_t sent_time;
};

static void
m_delete(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct tracked_message *msg;
	const char *msgid;

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: DELETE <msgid>");
		return;
	}

	msgid = parv[1];

	/* Check if message exists and can be deleted */
	msg = rb_dictionary_retrieve(tracked_messages, msgid);
	if (msg == NULL) {
		sendto_one_notice(source_p, ":*** Message not found or cannot be deleted");
		return;
	}

	/* Check ownership or operator status */
	if (msg->source_p != source_p && !IsOper(source_p)) {
		sendto_one_notice(source_p, ":*** You can only delete your own messages");
		return;
	}

	/* Notify recipients */
	if (msg->chptr != NULL) {
		/* Channel message - notify channel */
		sendto_channel_local(ALL_MEMBERS, msg->chptr,
			":%s NOTICE %s :Message %s deleted by %s",
			me.name, msg->chptr->chname, msgid, source_p->name);
	} else if (msg->target_p != NULL) {
		/* Private message - notify recipient */
		sendto_one_notice(msg->target_p, ":*** Message %s from %s was deleted",
			msgid, msg->source_p->name);
	}

	/* Delete message */
	rb_dictionary_delete(tracked_messages, msgid);
	rb_free(msg->msgid);
	rb_free(msg->text);
	rb_free(msg);

	sendto_one_notice(source_p, ":*** Message deleted");
}

static int
modinit(void)
{
	/* Use tracked_messages from m_edit if available */
	/* If m_edit is not loaded, this will fail gracefully */
	return 0;
}

static void
moddeinit(void)
{
	/* Cleanup handled by m_edit */
}

DECLARE_MODULE_AV2(delete, modinit, moddeinit, delete_clist, NULL, NULL, NULL, NULL, delete_desc);

