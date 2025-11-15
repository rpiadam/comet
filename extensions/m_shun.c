/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_shun.c: SHUN command for network-wide silence
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
#include "s_conf.h"
#include "s_newconf.h"
#include "hostmask.h"
#include "hook.h"

static const char shun_desc[] = "Provides the SHUN command for network-wide user silence";

static void m_shun(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void m_unshun(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void m_shunlist(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void hook_privmsg_channel(void *);
static void hook_privmsg_user(void *);

mapi_hfn_list_av1 shun_hfnlist[] = {
	{ "privmsg_channel", hook_privmsg_channel },
	{ "privmsg_user", hook_privmsg_user },
	{ NULL, NULL }
};

struct Message shun_msgtab = {
	"SHUN", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_shun, 2}}
};

struct Message unshun_msgtab = {
	"UNSHUN", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_unshun, 1}}
};

struct Message shunlist_msgtab = {
	"SHUNLIST", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_shunlist, 0}}
};

mapi_clist_av1 shun_clist[] = { &shun_msgtab, &unshun_msgtab, &shunlist_msgtab, NULL };

struct shun_entry {
	char *mask;
	char *reason;
	time_t when;
	time_t expire;
	rb_dlink_node node;
};

static rb_dlink_list shun_list;

static bool
is_shunned(struct Client *client_p)
{
	rb_dlink_node *ptr;
	char hostmask[BUFSIZE];

	snprintf(hostmask, sizeof(hostmask), "%s!%s@%s",
		client_p->name, client_p->username, client_p->host);

	RB_DLINK_FOREACH(ptr, shun_list.head) {
		struct shun_entry *shun = ptr->data;
		if (shun->expire > 0 && shun->expire < rb_current_time()) {
			/* Expired - remove it */
			rb_dlinkDelete(ptr, &shun_list);
			rb_free(shun->mask);
			rb_free(shun->reason);
			rb_free(shun);
			continue;
		}
		if (match(shun->mask, hostmask) == 0)
			return true;
	}

	return false;
}

static void
hook_privmsg_channel(void *data_)
{
	hook_data_privmsg_channel *data = data_;

	if (data->msgtype == MESSAGE_TYPE_PRIVMSG && is_shunned(data->source_p)) {
		/* Block message */
		data->approved = 1;
	}
}

static void
hook_privmsg_user(void *data_)
{
	hook_data_privmsg_user *data = data_;

	if (data->msgtype == MESSAGE_TYPE_PRIVMSG && is_shunned(data->source_p)) {
		/* Block message */
		data->approved = 1;
	}
}

static void
m_shun(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char *mask;
	char *reason = "No reason given";
	struct shun_entry *shun;
	int duration = 0;
	int loc = 1;

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: SHUN [duration] <user@host> :<reason>");
		return;
	}

	/* Check for duration */
	if (parc > 2) {
		duration = valid_temp_time(parv[1]);
		if (duration >= 0) {
			loc = 2;
		}
	}

	mask = (char *)parv[loc];
	if (parc > loc + 1 && !EmptyString(parv[loc + 1]))
		reason = (char *)parv[loc + 1];

	/* Create shun entry */
	shun = rb_malloc(sizeof(struct shun_entry));
	shun->mask = rb_strdup(mask);
	shun->reason = rb_strdup(reason);
	shun->when = rb_current_time();
	shun->expire = duration > 0 ? rb_current_time() + duration : 0;

	rb_dlinkAdd(shun, &shun->node, &shun_list);

	sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s issued SHUN: %s - %s",
		source_p->name, mask, reason);
	sendto_one_notice(source_p, ":*** SHUN issued for %s", mask);
}

static void
m_unshun(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char *mask;
	rb_dlink_node *ptr, *next;
	bool found = false;

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: UNSHUN <user@host>");
		return;
	}

	mask = (char *)parv[1];

	RB_DLINK_FOREACH_SAFE(ptr, next, shun_list.head) {
		struct shun_entry *shun = ptr->data;
		if (match(shun->mask, mask) == 0) {
			rb_dlinkDelete(ptr, &shun_list);
			rb_free(shun->mask);
			rb_free(shun->reason);
			rb_free(shun);
			found = true;
		}
	}

	if (found) {
		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE, "%s removed SHUN: %s",
			source_p->name, mask);
		sendto_one_notice(source_p, ":*** SHUN removed for %s", mask);
	} else {
		sendto_one_notice(source_p, ":*** No SHUN found for %s", mask);
	}
}

static void
m_shunlist(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	rb_dlink_node *ptr;
	struct shun_entry *shun;
	int count = 0;

	sendto_one_notice(source_p, ":*** SHUN List:");

	RB_DLINK_FOREACH(ptr, shun_list.head) {
		shun = ptr->data;
		if (shun->expire > 0 && shun->expire < rb_current_time())
			continue; /* Skip expired */

		count++;
		if (shun->expire > 0) {
			time_t remaining = shun->expire - rb_current_time();
			sendto_one_notice(source_p, ":*** %d. %s - %s (expires in %ld seconds)",
				count, shun->mask, shun->reason, (long)remaining);
		} else {
			sendto_one_notice(source_p, ":*** %d. %s - %s (permanent)",
				count, shun->mask, shun->reason);
		}
	}

	if (count == 0)
		sendto_one_notice(source_p, ":*** No active SHUNs");
	else
		sendto_one_notice(source_p, ":*** End of SHUN list (%d entries)", count);
}

static int
modinit(void)
{
	return 0;
}

static void
moddeinit(void)
{
	rb_dlink_node *ptr, *next;

	RB_DLINK_FOREACH_SAFE(ptr, next, shun_list.head) {
		struct shun_entry *shun = ptr->data;
		rb_dlinkDelete(ptr, &shun_list);
		rb_free(shun->mask);
		rb_free(shun->reason);
		rb_free(shun);
	}
}

DECLARE_MODULE_AV2(shun, modinit, moddeinit, shun_clist, NULL, shun_hfnlist, NULL, NULL, shun_desc);

