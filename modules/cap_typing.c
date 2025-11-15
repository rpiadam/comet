/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * cap_typing.c: implement the draft/typing IRCv3 capability
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
#include "s_serv.h"
#include "msgbuf.h"

static const char cap_typing_desc[] = "Provides the draft/typing client capability for typing indicators";

unsigned int CLICAP_TYPING = 0;

mapi_cap_list_av2 cap_typing_cap_list[] = {
	{ MAPI_CAP_CLIENT, "draft/typing", NULL, &CLICAP_TYPING },
	{ 0, NULL, NULL, NULL },
};

DECLARE_MODULE_AV2(cap_typing, NULL, NULL, NULL, NULL, NULL, cap_typing_cap_list, NULL, cap_typing_desc);

