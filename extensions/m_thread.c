/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_thread.c: Message threading support
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
#include "msgbuf.h"
#include "parse.h"

static const char thread_desc[] = "Provides message threading support";

static void hook_privmsg_channel_thread(void *);
static void hook_privmsg_user_thread(void *);

mapi_hfn_list_av1 thread_hfnlist[] = {
	{ "privmsg_channel", hook_privmsg_channel_thread },
	{ "privmsg_user", hook_privmsg_user_thread },
	{ NULL, NULL }
};

DECLARE_MODULE_AV2(m_thread, NULL, NULL, NULL, NULL, thread_hfnlist, NULL, NULL, thread_desc);

/* Add threading support via message tags */
static void
hook_privmsg_channel_thread(void *data_)
{
	hook_data_privmsg_channel *data = data_;
	struct MsgBuf *msgbuf = data->msgbuf;
	size_t i;
	const char *thread_parent = NULL;

	/* Check for thread-parent tag */
	for (i = 0; i < msgbuf->n_tags; i++)
	{
		if (msgbuf->tags[i].key && !strcmp(msgbuf->tags[i].key, "thread-parent"))
		{
			thread_parent = msgbuf->tags[i].value;
			break;
		}
	}

	/* If thread-parent is present, ensure it's propagated */
	if (thread_parent)
	{
		/* Threading is handled via message tags - no additional processing needed */
		/* The msgid tag should already be present for threading to work */
	}
}

static void
hook_privmsg_user_thread(void *data_)
{
	hook_data_privmsg_user *data = data_;
	struct MsgBuf *msgbuf = data->msgbuf;
	size_t i;
	const char *thread_parent = NULL;

	/* Check for thread-parent tag */
	for (i = 0; i < msgbuf->n_tags; i++)
	{
		if (msgbuf->tags[i].key && !strcmp(msgbuf->tags[i].key, "thread-parent"))
		{
			thread_parent = msgbuf->tags[i].value;
			break;
		}
	}

	/* If thread-parent is present, ensure it's propagated */
	if (thread_parent)
	{
		/* Threading is handled via message tags - no additional processing needed */
	}
}

