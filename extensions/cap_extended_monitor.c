/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * cap_extended_monitor.c: extended-monitor capability
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

static const char cap_extended_monitor_desc[] = "Provides the extended-monitor client capability";

unsigned int CLICAP_EXTENDED_MONITOR = 0;

mapi_cap_list_av2 cap_extended_monitor_cap_list[] = {
	{ MAPI_CAP_CLIENT, "extended-monitor", NULL, &CLICAP_EXTENDED_MONITOR },
	{ 0, NULL, NULL, NULL },
};

DECLARE_MODULE_AV2(cap_extended_monitor, NULL, NULL, NULL, NULL, NULL, cap_extended_monitor_cap_list, NULL, cap_extended_monitor_desc);

