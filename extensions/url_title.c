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
#include "channel.h"
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
extract_url(const char *text, char *url, size_t url_len)
{
	const char *start = NULL, *end;
	const char *p;
	
	/* Find http:// or https:// (case-insensitive) */
	p = text;
	while (*p) {
		if ((strncasecmp(p, "http://", 7) == 0) || (strncasecmp(p, "https://", 8) == 0)) {
			start = p;
			break;
		}
		p++;
	}
	if (start == NULL)
		return false;
	
	/* Find end of URL (space, newline, or end of string) */
	end = start;
	while (*end && *end != ' ' && *end != '\n' && *end != '\r' && *end != '\t')
		end++;
	
	/* Copy URL */
	if (end - start >= url_len)
		return false;
	
	memcpy(url, start, end - start);
	url[end - start] = '\0';
	return true;
}

static void
hook_privmsg_channel(void *data_)
{
	hook_data_privmsg_channel *data = data_;
	char url[512];
	char title[256];
	
	if (data->msgtype != MESSAGE_TYPE_PRIVMSG)
		return;
	
	/* Extract URL from message */
	if (!extract_url(data->text, url, sizeof(url)))
		return;
	
	/* For now, just extract domain name as placeholder */
	/* Full HTTP fetching would require async I/O - can be added later */
	if (strncasecmp(url, "http://", 7) == 0) {
		const char *domain = url + 7;
		const char *slash = strchr(domain, '/');
		if (slash)
			snprintf(title, sizeof(title), "%.*s", (int)(slash - domain), domain);
		else
			snprintf(title, sizeof(title), "%s", domain);
	} else if (strncasecmp(url, "https://", 8) == 0) {
		const char *domain = url + 8;
		const char *slash = strchr(domain, '/');
		if (slash)
			snprintf(title, sizeof(title), "%.*s", (int)(slash - domain), domain);
		else
			snprintf(title, sizeof(title), "%s", domain);
	} else {
		return;
	}
	
	/* Send notice to channel about URL */
	sendto_channel_local(ALL_MEMBERS, data->chptr,
		":%s NOTICE %s :URL: %s", me.name, data->chptr->chname, title);
}

static void
hook_privmsg_user(void *data_)
{
	hook_data_privmsg_user *data = data_;
	char url[512];
	char title[256];
	
	if (data->msgtype != MESSAGE_TYPE_PRIVMSG)
		return;
	
	if (!extract_url(data->text, url, sizeof(url)))
		return;
	
	/* Extract domain */
	if (strncasecmp(url, "http://", 7) == 0) {
		const char *domain = url + 7;
		const char *slash = strchr(domain, '/');
		if (slash)
			snprintf(title, sizeof(title), "%.*s", (int)(slash - domain), domain);
		else
			snprintf(title, sizeof(title), "%s", domain);
	} else if (strncasecmp(url, "https://", 8) == 0) {
		const char *domain = url + 8;
		const char *slash = strchr(domain, '/');
		if (slash)
			snprintf(title, sizeof(title), "%.*s", (int)(slash - domain), domain);
		else
			snprintf(title, sizeof(title), "%s", domain);
	} else {
		return;
	}
	
	sendto_one_notice(data->target_p, ":*** URL: %s", title);
}

DECLARE_MODULE_AV2(url_title, NULL, NULL, NULL, NULL, url_title_hfnlist, NULL, NULL, url_title_desc);

