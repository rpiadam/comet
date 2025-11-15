/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * umode_bot.c: user mode +B (bot)
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

static const char umode_bot_desc[] = "Adds user mode +B which marks a user as a bot";

static int
_modinit(void)
{
	user_modes['B'] = find_umode_slot();
	construct_umodebuf();
	return 0;
}

static void
_moddeinit(void)
{
	user_modes['B'] = 0;
	construct_umodebuf();
}

DECLARE_MODULE_AV2(umode_bot, _modinit, _moddeinit, NULL, NULL, NULL, NULL, NULL, umode_bot_desc);

