/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * chm_unfiltered.c: channel mode +u (unfiltered)
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
#include "chmode.h"
#include "hash.h"

static const char chm_unfiltered_desc[] = "Adds channel mode +u which allows unfiltered messages";

static unsigned int mode_unfiltered;

static int
_modinit(void)
{
	mode_unfiltered = cflag_add('u', chm_simple);
	if (mode_unfiltered == 0)
	{
		ierror("chm_unfiltered: unable to allocate cmode slot for +u");
		return -1;
	}
	return 0;
}

static void
_moddeinit(void)
{
	cflag_orphan('u');
}

DECLARE_MODULE_AV2(chm_unfiltered, _modinit, _moddeinit, NULL, NULL, NULL, NULL, NULL, chm_unfiltered_desc);

