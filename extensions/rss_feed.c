/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * rss_feed.c: RSS feed fetching and display
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
#include "msg.h"
#include "parse.h"
#include "modules.h"
#include "numeric.h"
#include "hook.h"
#include "event.h"
#include "rb_lib.h"

static const char rss_feed_desc[] = "Provides RSS feed fetching and display";

static void m_rss(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void m_rssadd(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void m_rssdel(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void m_rsslist(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void rss_update(void *);

struct rss_feed {
	char *url;
	char *channel;
	time_t last_check;
	time_t last_update;
	char *last_title;
	char *last_link;
	rb_dlink_node node;
};

static rb_dlink_list rss_feeds;
static rb_event *rss_update_event;

struct Message rss_msgtab = {
	"RSS", 0, 0, 0, 0,
	{mg_unreg, {m_rss, 1}, mg_ignore, mg_ignore, mg_ignore, {m_rss, 1}}
};

struct Message rssadd_msgtab = {
	"RSSADD", 0, 0, 0, 0,
	{mg_unreg, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_rssadd, 2}}
};

struct Message rssdel_msgtab = {
	"RSSDEL", 0, 0, 0, 0,
	{mg_unreg, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_rssdel, 1}}
};

struct Message rsslist_msgtab = {
	"RSSLIST", 0, 0, 0, 0,
	{mg_unreg, {m_rsslist, 0}, mg_ignore, mg_ignore, mg_ignore, {m_rsslist, 0}}
};

mapi_clist_av1 rss_feed_clist[] = { &rss_msgtab, &rssadd_msgtab, &rssdel_msgtab, &rsslist_msgtab, NULL };

static void
rss_update(void *unused)
{
	rb_dlink_node *ptr;
	struct rss_feed *feed;
	time_t now = rb_current_time();

	/* Check feeds every 5 minutes */
	RB_DLINK_FOREACH(ptr, rss_feeds.head) {
		feed = ptr->data;
		
		if (now - feed->last_check > 300) { /* 5 minutes */
			feed->last_check = now;
			
			/* TODO: Actually fetch RSS feed using HTTP */
			/* For now, just update the timestamp */
			/* In a real implementation, you would:
			 * 1. Use rb_commio or similar to fetch the RSS feed
			 * 2. Parse the XML
			 * 3. Check for new items
			 * 4. Send notices to the channel if new items found
			 */
		}
	}
}

static void
m_rss(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char *url;
	struct Channel *chptr = NULL;

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: RSS <url> [channel]");
		return;
	}

	url = (char *)parv[1];

	if (parc > 2 && !EmptyString(parv[2])) {
		chptr = find_channel(parv[2]);
		if (chptr == NULL) {
			sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL, form_str(ERR_NOSUCHCHANNEL), parv[2]);
			return;
		}
	}

	/* TODO: Fetch and parse RSS feed */
	/* For now, just show a placeholder */
	sendto_one_notice(source_p, ":*** RSS Feed: %s", url);
	if (chptr) {
		sendto_channel_local(ALL_MEMBERS, chptr, ":*** RSS Feed update: %s", url);
	} else {
		sendto_one_notice(source_p, ":*** RSS feed functionality requires HTTP fetching (not yet implemented)");
	}
}

static void
m_rssadd(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char *url, *channel;
	struct rss_feed *feed;
	struct Channel *chptr;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2])) {
		sendto_one_notice(source_p, ":*** Syntax: RSSADD <url> <channel>");
		return;
	}

	url = (char *)parv[1];
	channel = (char *)parv[2];

	chptr = find_channel(channel);
	if (chptr == NULL) {
		sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL, form_str(ERR_NOSUCHCHANNEL), channel);
		return;
	}

	/* Check if feed already exists */
	rb_dlink_node *ptr;
	RB_DLINK_FOREACH(ptr, rss_feeds.head) {
		feed = ptr->data;
		if (!strcmp(feed->url, url) && !strcmp(feed->channel, channel)) {
			sendto_one_notice(source_p, ":*** RSS feed already exists for %s in %s", url, channel);
			return;
		}
	}

	feed = rb_malloc(sizeof(struct rss_feed));
	feed->url = rb_strdup(url);
	feed->channel = rb_strdup(channel);
	feed->last_check = rb_current_time();
	feed->last_update = 0;
	feed->last_title = NULL;
	feed->last_link = NULL;

	rb_dlinkAddAlloc(feed, &rss_feeds);

	sendto_one_notice(source_p, ":*** RSS feed added: %s -> %s", url, channel);
	sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s added RSS feed: %s -> %s",
		source_p->name, url, channel);
}

static void
m_rssdel(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char *url;
	rb_dlink_node *ptr, *next;
	struct rss_feed *feed;
	bool found = false;

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: RSSDEL <url>");
		return;
	}

	url = (char *)parv[1];

	RB_DLINK_FOREACH_SAFE(ptr, next, rss_feeds.head) {
		feed = ptr->data;
		if (!strcmp(feed->url, url)) {
			rb_dlinkDelete(ptr, &rss_feeds);
			rb_free(feed->url);
			rb_free(feed->channel);
			if (feed->last_title)
				rb_free(feed->last_title);
			if (feed->last_link)
				rb_free(feed->last_link);
			rb_free(feed);
			found = true;
		}
	}

	if (found) {
		sendto_one_notice(source_p, ":*** RSS feed removed: %s", url);
		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s removed RSS feed: %s",
			source_p->name, url);
	} else {
		sendto_one_notice(source_p, ":*** RSS feed not found: %s", url);
	}
}

static void
m_rsslist(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	rb_dlink_node *ptr;
	struct rss_feed *feed;
	int count = 0;

	sendto_one_notice(source_p, ":*** RSS Feed List:");

	RB_DLINK_FOREACH(ptr, rss_feeds.head) {
		feed = ptr->data;
		count++;
		sendto_one_notice(source_p, ":*** %d. %s -> %s (last check: %lu seconds ago)",
			count, feed->url, feed->channel,
			(unsigned long)(rb_current_time() - feed->last_check));
	}

	if (count == 0)
		sendto_one_notice(source_p, ":*** No RSS feeds configured");
	else
		sendto_one_notice(source_p, ":*** End of RSS feed list (%d feeds)", count);
}

static int
modinit(void)
{
	/* Update RSS feeds every 5 minutes */
	rss_update_event = rb_event_addish("rss_update", rss_update, NULL, 300);
	return 0;
}

static void
moddeinit(void)
{
	rb_dlink_node *ptr, *next;
	struct rss_feed *feed;

	if (rss_update_event)
		rb_event_delete(rss_update_event);

	RB_DLINK_FOREACH_SAFE(ptr, next, rss_feeds.head) {
		feed = ptr->data;
		rb_dlinkDelete(ptr, &rss_feeds);
		rb_free(feed->url);
		rb_free(feed->channel);
		if (feed->last_title)
			rb_free(feed->last_title);
		if (feed->last_link)
			rb_free(feed->last_link);
		rb_free(feed);
	}
}

DECLARE_MODULE_AV2(rss_feed, modinit, moddeinit, rss_feed_clist, NULL, NULL, NULL, NULL, rss_feed_desc);

