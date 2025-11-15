/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * cap_labeled_response.c: implement the labeled-response IRCv3 capability
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

static const char cap_labeled_response_desc[] = "Provides the labeled-response client capability";

unsigned int CLICAP_LABELED_RESPONSE = 0;

mapi_cap_list_av2 cap_labeled_response_cap_list[] = {
	{ MAPI_CAP_CLIENT, "labeled-response", NULL, &CLICAP_LABELED_RESPONSE },
	{ 0, NULL, NULL, NULL },
};

DECLARE_MODULE_AV2(cap_labeled_response, NULL, NULL, NULL, NULL, NULL, cap_labeled_response_cap_list, NULL, cap_labeled_response_desc);

