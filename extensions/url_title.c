/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * url_title.c: Fetch and display URL titles
 *
 * Copyright (c) 2024
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 */

#include "stdinc.h"
#include "client.h"
#include "ircd.h"
#include "send.h"
#include "s_user.h"
#include "modules.h"
#include "hook.h"
#include "match.h"

static const char url_title_desc[] = "Fetches and displays URL titles from messages";

static void hook_privmsg_channel(void *);
static void hook_privmsg_user(void *);

mapi_hfn_list_av1 url_title_hfnlist[] = {
	{ "privmsg_channel", hook_privmsg_channel },
	{ "privmsg_user", hook_privmsg_user },
	{ NULL, NULL }
};

static bool
is_url(const char *text)
{
	return (strncasecmp(text, "http://", 7) == 0 ||
		strncasecmp(text, "https://", 8) == 0);
}

static void
hook_privmsg_channel(void *data_)
{
	hook_data_privmsg_channel *data = data_;
	const char *url;

	/* Simple URL detection - would need more sophisticated parsing */
	if (is_url(data->text)) {
		url = data->text;
		/* Would fetch URL title here and send notice */
		/* sendto_one_notice(data->source_p, ":*** URL Title: ..."); */
	}
}

static void
hook_privmsg_user(void *data_)
{
	hook_data_privmsg_user *data = data_;
	const char *url;

	if (is_url(data->text)) {
		url = data->text;
		/* Would fetch URL title here and send notice */
	}
}

DECLARE_MODULE_AV2(url_title, NULL, NULL, NULL, NULL, url_title_hfnlist, NULL, NULL, url_title_desc);

