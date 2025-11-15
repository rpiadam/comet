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

static rb_dictionary_t *deletable_messages;

static void
m_delete(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *msgid;

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: DELETE <msgid>");
		return;
	}

	msgid = parv[1];

	/* Check if message exists and can be deleted */
	if (rb_dictionary_retrieve(deletable_messages, msgid) == NULL) {
		sendto_one_notice(source_p, ":*** Message not found or cannot be deleted");
		return;
	}

	/* Delete message and notify recipients */
	rb_dictionary_delete(deletable_messages, msgid);
	sendto_one_notice(source_p, ":*** Message deleted");
}

static int
modinit(void)
{
	deletable_messages = rb_dictionary_create("deletable_messages", rb_dictionary_str_casecmp);
	return 0;
}

static void
moddeinit(void)
{
	if (deletable_messages != NULL) {
		rb_dictionary_destroy(deletable_messages, NULL, NULL);
		deletable_messages = NULL;
	}
}

DECLARE_MODULE_AV2(delete, modinit, moddeinit, delete_clist, NULL, NULL, NULL, NULL, delete_desc);

