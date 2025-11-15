/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * umode_whois.c: user mode +W (whois)
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

static const char umode_whois_desc[] = "Adds user mode +W which restricts WHOIS to operators";

static rb_dictionary_t *whois_suppress_dict;

static void hook_doing_whois(void *);
static void hook_doing_whois_global(void *);

mapi_hfn_list_av1 umode_whois_hfnlist[] = {
	{ "doing_whois", hook_doing_whois },
	{ "doing_whois_global", hook_doing_whois_global },
	{ NULL, NULL }
};

static void
hook_doing_whois(void *data_)
{
	hook_data_client *data = data_;
	char key[BUFSIZE];

	if (!MyClient(data->client) || !IsPerson(data->target))
		return;

	/* If target has +W, only show WHOIS to operators */
	if ((data->target->umodes & user_modes['W']) && !IsOper(data->client)) {
		snprintf(key, sizeof(key), "%s:%s", data->client->id, data->target->id);
		rb_dictionary_add(whois_suppress_dict, key, (void *)1);
		/* Suppress WHOIS by sending minimal response */
		sendto_one_numeric(data->client, ERR_NOSUCHNICK, form_str(ERR_NOSUCHNICK), data->target->name);
		rb_dictionary_delete(whois_suppress_dict, key);
	}
}

static void
hook_doing_whois_global(void *data_)
{
	hook_data_client *data = data_;
	char key[BUFSIZE];

	if (!MyClient(data->client) || !IsPerson(data->target))
		return;

	/* If target has +W, only show WHOIS to operators */
	if ((data->target->umodes & user_modes['W']) && !IsOper(data->client)) {
		snprintf(key, sizeof(key), "%s:%s", data->client->id, data->target->id);
		rb_dictionary_add(whois_suppress_dict, key, (void *)1);
	}
}

static int
_modinit(void)
{
	user_modes['W'] = find_umode_slot();
	construct_umodebuf();
	whois_suppress_dict = rb_dictionary_create("whois_suppress_umode", rb_dictionary_str_casecmp);
	return 0;
}

static void
_moddeinit(void)
{
	if (whois_suppress_dict != NULL) {
		rb_dictionary_destroy(whois_suppress_dict, NULL, NULL);
		whois_suppress_dict = NULL;
	}
	user_modes['W'] = 0;
	construct_umodebuf();
}

DECLARE_MODULE_AV2(umode_whois, _modinit, _moddeinit, NULL, NULL, umode_whois_hfnlist, NULL, NULL, umode_whois_desc);

