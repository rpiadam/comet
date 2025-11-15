/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * chm_whois.c: channel mode +W (whois)
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
#include "numeric.h"

static const char chm_whois_desc[] = "Adds channel mode +W, which restricts WHOIS to channel operators";

static unsigned int mode_whois;
static rb_dictionary_t *whois_suppress_dict;

static void hook_doing_whois(void *);
static void hook_doing_whois_global(void *);

mapi_hfn_list_av1 chm_whois_hfnlist[] = {
	{ "doing_whois", hook_doing_whois },
	{ "doing_whois_global", hook_doing_whois_global },
	{ NULL, NULL }
};

static void
check_whois_suppress(struct Client *source_p, struct Client *target_p)
{
	struct membership *msptr;
	rb_dlink_node *ptr;
	bool hide_info = false;
	char key[BUFSIZE];

	if (!MyClient(source_p) || !IsPerson(target_p))
		return;

	/* Check if target is in any +W channel */
	RB_DLINK_FOREACH(ptr, target_p->user->channel.head) {
		msptr = ptr->data;
		if (msptr->chptr->mode.mode & mode_whois) {
			/* Check if requester is op in this channel */
			struct membership *req_msptr = find_channel_membership(msptr->chptr, source_p);
			if (req_msptr == NULL || !is_chanop(req_msptr)) {
				/* Hide WHOIS info for non-ops */
				hide_info = true;
				break;
			}
		}
	}

	/* If info should be hidden and requester is not an operator, mark for suppression */
	if (hide_info && !IsOper(source_p)) {
		snprintf(key, sizeof(key), "%s:%s", source_p->id, target_p->id);
		rb_dictionary_add(whois_suppress_dict, key, (void *)1);
	}
}

static void
hook_doing_whois(void *data_)
{
	hook_data_client *data = data_;
	char key[BUFSIZE];

	if (!MyClient(data->client) || !IsPerson(data->target))
		return;

	snprintf(key, sizeof(key), "%s:%s", data->client->id, data->target->id);
	if (rb_dictionary_retrieve(whois_suppress_dict, key) != NULL) {
		/* Suppress WHOIS by sending minimal response */
		sendto_one_numeric(data->client, ERR_NOSUCHNICK, form_str(ERR_NOSUCHNICK), data->target->name);
		rb_dictionary_delete(whois_suppress_dict, key);
	} else {
		check_whois_suppress(data->client, data->target);
	}
}

static void
hook_doing_whois_global(void *data_)
{
	hook_data_client *data = data_;
	check_whois_suppress(data->client, data->target);
}

static int
_modinit(void)
{
	mode_whois = cflag_add('W', chm_simple);
	if (mode_whois == 0) {
		ierror("chm_whois: unable to allocate cmode slot for +W");
		return -1;
	}
	whois_suppress_dict = rb_dictionary_create("whois_suppress", rb_dictionary_str_casecmp);
	return 0;
}

static void
_moddeinit(void)
{
	if (whois_suppress_dict != NULL) {
		rb_dictionary_destroy(whois_suppress_dict, NULL, NULL);
		whois_suppress_dict = NULL;
	}
	cflag_orphan('W');
}

DECLARE_MODULE_AV2(chm_whois, _modinit, _moddeinit, NULL, NULL, chm_whois_hfnlist, NULL, NULL, chm_whois_desc);

