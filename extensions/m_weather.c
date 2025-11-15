/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_weather.c: WEATHER command for weather information
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

static const char weather_desc[] = "Provides the WEATHER command for weather information";

static void m_weather(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message weather_msgtab = {
	"WEATHER", 0, 0, 0, 0,
	{mg_ignore, {m_weather, 1}, mg_ignore, mg_ignore, mg_ignore, {m_weather, 1}}
};

mapi_clist_av1 weather_clist[] = { &weather_msgtab, NULL };

static void
m_weather(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char *location;
	char response[512];

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: WEATHER <location> [channel]");
		return;
	}

	location = (char *)parv[1];

	/* Weather lookup - would integrate with weather API */
	/* For now, return placeholder */
	snprintf(response, sizeof(response),
		":*** Weather for %s: Temperature 72°F, Partly Cloudy, Humidity 65%%",
		location);

	if (parc > 2 && !EmptyString(parv[2])) {
		/* Send to channel */
		struct Channel *chptr = find_channel(parv[2]);
		if (chptr != NULL) {
			sendto_channel_local(ALL_MEMBERS, chptr, "%s", response);
			return;
		}
	}

	sendto_one_notice(source_p, "%s", response);
}

DECLARE_MODULE_AV2(weather, NULL, NULL, weather_clist, NULL, NULL, NULL, NULL, weather_desc);

