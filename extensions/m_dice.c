/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_dice.c: DICE command for rolling dice
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
#include "channel.h"

static const char dice_desc[] = "Provides the DICE command for rolling dice";

static void m_dice(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message dice_msgtab = {
	"DICE", 0, 0, 0, 0,
	{mg_ignore, {m_dice, 0}, mg_ignore, mg_ignore, mg_ignore, {m_dice, 0}}
};

mapi_clist_av1 dice_clist[] = { &dice_msgtab, NULL };

static void
m_dice(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	int sides = 6;
	int count = 1;
	int total = 0;
	int i, roll;
	char buf[512];
	int n = 0;

	if (parc > 1 && !EmptyString(parv[1])) {
		if (strchr(parv[1], 'd') != NULL) {
			/* Format: XdY */
			sscanf(parv[1], "%dd%d", &count, &sides);
		} else {
			sides = atoi(parv[1]);
		}
	}

	if (sides < 2 || sides > 100)
		sides = 6;
	if (count < 1 || count > 10)
		count = 1;

	n = snprintf(buf, sizeof(buf), ":*** %s rolled ", source_p->name);

	for (i = 0; i < count; i++) {
		roll = (rb_random() % sides) + 1;
		total += roll;
		if (i > 0)
			n += snprintf(buf + n, sizeof(buf) - n, "+ ");
		n += snprintf(buf + n, sizeof(buf) - n, "%d ", roll);
	}

	if (count > 1)
		snprintf(buf + n, sizeof(buf) - n, "= %d", total);

	if (parc > 2 && !EmptyString(parv[2])) {
		/* Send to channel */
		struct Channel *chptr = find_channel(parv[2]);
		if (chptr != NULL) {
			sendto_channel_local(ALL_MEMBERS, chptr, "%s", buf);
			return;
		}
	}

	sendto_one_notice(source_p, "%s", buf);
}

DECLARE_MODULE_AV2(dice, NULL, NULL, dice_clist, NULL, NULL, NULL, NULL, dice_desc);

