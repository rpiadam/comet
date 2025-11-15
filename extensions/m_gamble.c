/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_gamble.c: Various gambling games (dice, coin flip, etc.)
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

static const char gamble_desc[] = "Provides gambling games (dice, coin flip, roulette)";

static void m_dicegamble(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void m_coinflip(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void m_roulette(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message dicegamble_msgtab = {
	"DICEGAMBLE", 0, 0, 0, 0,
	{mg_unreg, {m_dicegamble, 2}, mg_ignore, mg_ignore, mg_ignore, {m_dicegamble, 2}}
};

struct Message coinflip_msgtab = {
	"COINFLIP", 0, 0, 0, 0,
	{mg_unreg, {m_coinflip, 2}, mg_ignore, mg_ignore, mg_ignore, {m_coinflip, 2}}
};

struct Message roulette_msgtab = {
	"ROULETTE", 0, 0, 0, 0,
	{mg_unreg, {m_roulette, 2}, mg_ignore, mg_ignore, mg_ignore, {m_roulette, 2}}
};

mapi_clist_av1 gamble_clist[] = { &dicegamble_msgtab, &coinflip_msgtab, &roulette_msgtab, NULL };

static void
m_dicegamble(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	int bet, guess, roll;
	int payout = 0;
	struct Channel *chptr = NULL;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2])) {
		sendto_one_notice(source_p, ":*** Syntax: DICEGAMBLE <bet> <guess (1-6)>");
		return;
	}

	bet = atoi(parv[1]);
	guess = atoi(parv[2]);

	if (bet < 1 || bet > 100) {
		sendto_one_notice(source_p, ":*** Bet must be between 1 and 100");
		return;
	}

	if (guess < 1 || guess > 6) {
		sendto_one_notice(source_p, ":*** Guess must be between 1 and 6");
		return;
	}

	roll = (rb_random() % 6) + 1;

	if (roll == guess) {
		payout = bet * 6; /* 6x payout for exact match */
	}

	if (source_p->user && source_p->user->channel)
		chptr = source_p->user->channel->chptr;

	if (payout > 0) {
		if (chptr) {
			sendto_channel_local(ALL_MEMBERS, chptr, ":*** %s rolled %d (guessed %d) - WIN! Payout: %d", 
				source_p->name, roll, guess, payout);
		} else {
			sendto_one_notice(source_p, ":*** You rolled %d (guessed %d) - WIN! Payout: %d", 
				roll, guess, payout);
		}
	} else {
		if (chptr) {
			sendto_channel_local(ALL_MEMBERS, chptr, ":*** %s rolled %d (guessed %d) - Lose (bet: %d)", 
				source_p->name, roll, guess, bet);
		} else {
			sendto_one_notice(source_p, ":*** You rolled %d (guessed %d) - Lose (bet: %d)", 
				roll, guess, bet);
		}
	}
}

static void
m_coinflip(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	int bet;
	const char *guess, *result;
	int payout = 0;
	struct Channel *chptr = NULL;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2])) {
		sendto_one_notice(source_p, ":*** Syntax: COINFLIP <bet> <heads|tails>");
		return;
	}

	bet = atoi(parv[1]);
	guess = parv[2];

	if (bet < 1 || bet > 100) {
		sendto_one_notice(source_p, ":*** Bet must be between 1 and 100");
		return;
	}

	if (strcasecmp(guess, "heads") != 0 && strcasecmp(guess, "tails") != 0) {
		sendto_one_notice(source_p, ":*** Guess must be 'heads' or 'tails'");
		return;
	}

	result = (rb_random() % 2) == 0 ? "heads" : "tails";

	if (!strcasecmp(result, guess)) {
		payout = bet * 2; /* 2x payout */
	}

	if (source_p->user && source_p->user->channel)
		chptr = source_p->user->channel->chptr;

	if (payout > 0) {
		if (chptr) {
			sendto_channel_local(ALL_MEMBERS, chptr, ":*** %s flipped %s (guessed %s) - WIN! Payout: %d", 
				source_p->name, result, guess, payout);
		} else {
			sendto_one_notice(source_p, ":*** You flipped %s (guessed %s) - WIN! Payout: %d", 
				result, guess, payout);
		}
	} else {
		if (chptr) {
			sendto_channel_local(ALL_MEMBERS, chptr, ":*** %s flipped %s (guessed %s) - Lose (bet: %d)", 
				source_p->name, result, guess, bet);
		} else {
			sendto_one_notice(source_p, ":*** You flipped %s (guessed %s) - Lose (bet: %d)", 
				result, guess, bet);
		}
	}
}

static void
m_roulette(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	int bet, guess, spin;
	int payout = 0;
	struct Channel *chptr = NULL;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2])) {
		sendto_one_notice(source_p, ":*** Syntax: ROULETTE <bet> <number (0-36) or red|black|even|odd>");
		return;
	}

	bet = atoi(parv[1]);

	if (bet < 1 || bet > 100) {
		sendto_one_notice(source_p, ":*** Bet must be between 1 and 100");
		return;
	}

	spin = rb_random() % 37; /* 0-36 */

	/* Parse guess */
	if (strcasecmp(parv[2], "red") == 0) {
		/* Red numbers: 1,3,5,7,9,12,14,16,18,19,21,23,25,27,30,32,34,36 */
		int reds[] = {1,3,5,7,9,12,14,16,18,19,21,23,25,27,30,32,34,36};
		int i;
		for (i = 0; i < sizeof(reds)/sizeof(reds[0]); i++) {
			if (spin == reds[i]) {
				payout = bet * 2;
				break;
			}
		}
	} else if (strcasecmp(parv[2], "black") == 0) {
		/* Black numbers: 2,4,6,8,10,11,13,15,17,20,22,24,26,28,29,31,33,35 */
		int blacks[] = {2,4,6,8,10,11,13,15,17,20,22,24,26,28,29,31,33,35};
		int i;
		for (i = 0; i < sizeof(blacks)/sizeof(blacks[0]); i++) {
			if (spin == blacks[i]) {
				payout = bet * 2;
				break;
			}
		}
	} else if (strcasecmp(parv[2], "even") == 0) {
		if (spin > 0 && spin % 2 == 0) {
			payout = bet * 2;
		}
	} else if (strcasecmp(parv[2], "odd") == 0) {
		if (spin > 0 && spin % 2 == 1) {
			payout = bet * 2;
		}
	} else {
		guess = atoi(parv[2]);
		if (guess < 0 || guess > 36) {
			sendto_one_notice(source_p, ":*** Number must be between 0 and 36");
			return;
		}
		if (spin == guess) {
			payout = bet * 36; /* 36x payout for exact number */
		}
	}

	if (source_p->user && source_p->user->channel)
		chptr = source_p->user->channel->chptr;

	if (payout > 0) {
		if (chptr) {
			sendto_channel_local(ALL_MEMBERS, chptr, ":*** %s spun %d (guessed %s) - WIN! Payout: %d", 
				source_p->name, spin, parv[2], payout);
		} else {
			sendto_one_notice(source_p, ":*** You spun %d (guessed %s) - WIN! Payout: %d", 
				spin, parv[2], payout);
		}
	} else {
		if (chptr) {
			sendto_channel_local(ALL_MEMBERS, chptr, ":*** %s spun %d (guessed %s) - Lose (bet: %d)", 
				source_p->name, spin, parv[2], bet);
		} else {
			sendto_one_notice(source_p, ":*** You spun %d (guessed %s) - Lose (bet: %d)", 
				spin, parv[2], bet);
		}
	}
}

DECLARE_MODULE_AV2(gamble, NULL, NULL, gamble_clist, NULL, NULL, NULL, NULL, gamble_desc);

