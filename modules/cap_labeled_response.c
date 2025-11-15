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

static const char *find_label_tag(const struct MsgBuf *msgbuf);
static void hook_outbound_msgbuf_labeled(void *);

mapi_hfn_list_av1 cap_labeled_response_hfnlist[] = {
	{ "outbound_msgbuf", hook_outbound_msgbuf_labeled },
	{ NULL, NULL }
};

mapi_cap_list_av2 cap_labeled_response_cap_list[] = {
	{ MAPI_CAP_CLIENT, "labeled-response", NULL, &CLICAP_LABELED_RESPONSE },
	{ 0, NULL, NULL, NULL },
};

static const char *
find_label_tag(const struct MsgBuf *msgbuf)
{
	size_t i;
	for (i = 0; i < msgbuf->n_tags; i++) {
		if (msgbuf->tags[i].key && !strcmp(msgbuf->tags[i].key, "label"))
			return msgbuf->tags[i].value;
	}
	return NULL;
}

static void
hook_outbound_msgbuf_labeled(void *data_)
{
	hook_data *data = data_;
	struct MsgBuf *msgbuf = data->arg1;
	struct Client *client_p = data->client;
	const char *label;

	if (!IsCapable(client_p, CLICAP_LABELED_RESPONSE))
		return;

	/* Check if there's a label tag in the original request */
	/* This is handled by storing the label in the client's pending response context */
	/* For now, we just ensure the capability is registered */
	label = find_label_tag(msgbuf);
	if (label != NULL) {
		/* Label found - responses to this client should include this label */
		/* The actual label propagation happens in sendto_one_numeric and similar functions */
	}
}

DECLARE_MODULE_AV2(cap_labeled_response, NULL, NULL, NULL, NULL, cap_labeled_response_hfnlist, cap_labeled_response_cap_list, NULL, cap_labeled_response_desc);

