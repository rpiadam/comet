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

static const char edit_desc[] = "Provides message editing functionality";

static void m_edit(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message edit_msgtab = {
	"EDIT", 0, 0, 0, 0,
	{mg_ignore, {m_edit, 2}, mg_ignore, mg_ignore, mg_ignore, {m_edit, 2}}
};

mapi_clist_av1 edit_clist[] = { &edit_msgtab, NULL };

struct edited_message {
	char *msgid;
	char *original;
	char *edited;
	time_t edit_time;
	rb_dlink_node node;
};

static rb_dictionary_t *edited_messages;

static void
m_edit(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct edited_message *edit;
	const char *msgid;
	const char *newtext;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2])) {
		sendto_one_notice(source_p, ":*** Syntax: EDIT <msgid> <new text>");
		return;
	}

	msgid = parv[1];
	newtext = parv[2];

	/* Find original message */
	edit = rb_dictionary_retrieve(edited_messages, msgid);
	if (edit == NULL) {
		sendto_one_notice(source_p, ":*** Message not found or cannot be edited");
		return;
	}

	/* Update message */
	rb_free(edit->edited);
	edit->edited = rb_strdup(newtext);
	edit->edit_time = rb_current_time();

	/* Send edit notification to channel/users */
	/* Implementation would send edited message to recipients */
	sendto_one_notice(source_p, ":*** Message edited");
}

static int
modinit(void)
{
	edited_messages = rb_dictionary_create("edited_messages", rb_dictionary_str_casecmp);
	return 0;
}

static void
moddeinit(void)
{
	rb_dictionary_iter iter;
	struct edited_message *edit;

	RB_DICTIONARY_FOREACH(edit, &iter, edited_messages) {
		rb_free(edit->msgid);
		rb_free(edit->original);
		rb_free(edit->edited);
		rb_free(edit);
	}

	if (edited_messages != NULL) {
		rb_dictionary_destroy(edited_messages, NULL, NULL);
		edited_messages = NULL;
	}
}

DECLARE_MODULE_AV2(edit, modinit, moddeinit, edit_clist, NULL, NULL, NULL, NULL, edit_desc);

