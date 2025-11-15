/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_slots.c: Slot machine gambling game
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

static const char slots_desc[] = "Provides slot machine gambling game";

static void m_slots(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message slots_msgtab = {
	"SLOTS", 0, 0, 0, 0,
	{mg_unreg, {m_slots, 1}, mg_ignore, mg_ignore, mg_ignore, {m_slots, 1}}
};

mapi_clist_av1 slots_clist[] = { &slots_msgtab, NULL };

static const char *symbols[] = {
	"🍒", "🍋", "🍊", "🍇", "🍉", "⭐", "💎", "7️⃣"
};

#define NUM_SYMBOLS (sizeof(symbols) / sizeof(symbols[0]))

static int
get_payout(const char *s1, const char *s2, const char *s3, int bet)
{
	/* Three of a kind */
	if (!strcmp(s1, s2) && !strcmp(s2, s3)) {
		if (!strcmp(s1, "💎"))
			return bet * 100; /* Diamond jackpot */
		if (!strcmp(s1, "7️⃣"))
			return bet * 50;  /* 7s jackpot */
		if (!strcmp(s1, "⭐"))
			return bet * 25;  /* Star jackpot */
		return bet * 10; /* Other three of a kind */
	}

	/* Two of a kind */
	if (!strcmp(s1, s2) || !strcmp(s2, s3) || !strcmp(s1, s3)) {
		return bet * 2;
	}

	return 0; /* No win */
}

static void
m_slots(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	int bet = 1;
	const char *s1, *s2, *s3;
	int payout;
	char result[256];
	struct Channel *chptr = NULL;

	if (parc > 1 && !EmptyString(parv[1])) {
		bet = atoi(parv[1]);
		if (bet < 1 || bet > 100) {
			sendto_one_notice(source_p, ":*** Bet must be between 1 and 100");
			return;
		}
	}

	/* Spin the reels */
	s1 = symbols[rb_random() % NUM_SYMBOLS];
	s2 = symbols[rb_random() % NUM_SYMBOLS];
	s3 = symbols[rb_random() % NUM_SYMBOLS];

	payout = get_payout(s1, s2, s3, bet);

	/* Format result */
	snprintf(result, sizeof(result), "[ %s | %s | %s ]", s1, s2, s3);

	/* Determine channel if in one */
	if (source_p->user && source_p->user->channel)
		chptr = source_p->user->channel->chptr;

	if (payout > 0) {
		if (chptr) {
			sendto_channel_local(ALL_MEMBERS, chptr, ":*** %s spins: %s - WIN! Payout: %d", 
				source_p->name, result, payout);
		} else {
			sendto_one_notice(source_p, ":*** You spin: %s - WIN! Payout: %d", result, payout);
		}
	} else {
		if (chptr) {
			sendto_channel_local(ALL_MEMBERS, chptr, ":*** %s spins: %s - No win (bet: %d)", 
				source_p->name, result, bet);
		} else {
			sendto_one_notice(source_p, ":*** You spin: %s - No win (bet: %d)", result, bet);
		}
	}
}

DECLARE_MODULE_AV2(slots, NULL, NULL, slots_clist, NULL, NULL, NULL, NULL, slots_desc);

