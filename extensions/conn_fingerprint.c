/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * conn_fingerprint.c: Connection fingerprinting for ban evasion detection
 *
 * Copyright (c) 2024
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "stdinc.h"
#include "client.h"
#include "hash.h"
#include "hostmask.h"
#include "ircd.h"
#include "match.h"
#include "modules.h"
#include "msg.h"
#include "parse.h"
#include "send.h"
#include "s_user.h"
#include "numeric.h"
#include "s_serv.h"

static const char conn_fingerprint_desc[] = "Connection fingerprinting for security and ban evasion detection";

/* Fingerprint structure */
struct connection_fingerprint {
	char fingerprint[128];
	time_t created;
	time_t last_seen;
	unsigned int matches;
	rb_dlink_list accounts;
	rb_dlink_node node;
};

/* Account association */
struct fingerprint_account {
	char account[USERLEN + 1];
	char host[HOSTLEN + 1];
	time_t first_seen;
	time_t last_seen;
	rb_dlink_node node;
};

static rb_dictionary_t *fingerprint_dict;
static bool enabled = true;
static int collision_threshold = 3; /* Alert if same fingerprint used by N accounts */
static struct ev_entry *fingerprint_expire_ev;

/* Hook functions */
static void conn_fingerprint_new_local_user(void *data);
static void conn_fingerprint_client_exit(void *data);

mapi_hfn_list_av1 conn_fingerprint_hfnlist[] = {
	{ "new_local_user", conn_fingerprint_new_local_user },
	{ "client_exit", conn_fingerprint_client_exit },
	{ NULL, NULL }
};

/* Generate a simple fingerprint from connection characteristics */
static void
generate_fingerprint(struct Client *client_p, char *fp, size_t len)
{
	char buf[512];
	int n = 0;

	/* Combine various connection characteristics */
	n += snprintf(buf + n, sizeof(buf) - n, "%s:", client_p->sockhost);
	n += snprintf(buf + n, sizeof(buf) - n, "%s:", client_p->username);
	n += snprintf(buf + n, sizeof(buf) - n, "%s:", client_p->host);
	
	if (IsSecureClient(client_p))
		n += snprintf(buf + n, sizeof(buf) - n, "SSL:");

	/* Hash the combined string */
	/* In production, use a proper hash function like SHA256 */
	rb_strlcpy(fp, buf, len);
}

static struct connection_fingerprint *
find_or_create_fingerprint(const char *fp)
{
	struct connection_fingerprint *entry;

	entry = rb_dictionary_retrieve(fingerprint_dict, fp);
	if (entry != NULL) {
		entry->last_seen = rb_current_time();
		entry->matches++;
		return entry;
	}

	entry = rb_malloc(sizeof(struct connection_fingerprint));
	rb_strlcpy(entry->fingerprint, fp, sizeof(entry->fingerprint));
	entry->created = rb_current_time();
	entry->last_seen = rb_current_time();
	entry->matches = 1;

	rb_dictionary_add(fingerprint_dict, entry->fingerprint, entry);
	rb_dlinkAdd(entry, &entry->node, NULL);

	return entry;
}

static void
associate_account(struct connection_fingerprint *fp, struct Client *client_p)
{
	struct fingerprint_account *acc;
	rb_dlink_node *ptr;
	time_t now = rb_current_time();

	/* Check if account already associated */
	RB_DLINK_FOREACH(ptr, fp->accounts.head) {
		acc = ptr->data;
		if (strcmp(acc->account, client_p->name) == 0) {
			acc->last_seen = now;
			return;
		}
	}

	/* New account association */
	acc = rb_malloc(sizeof(struct fingerprint_account));
	rb_strlcpy(acc->account, client_p->name, sizeof(acc->account));
	rb_strlcpy(acc->host, client_p->host, sizeof(acc->host));
	acc->first_seen = now;
	acc->last_seen = now;

	rb_dlinkAdd(acc, &acc->node, &fp->accounts);
}

static void
check_collisions(struct connection_fingerprint *fp, struct Client *client_p)
{
	if (rb_dlink_list_length(&fp->accounts) >= collision_threshold) {
		rb_dlink_node *ptr;
		char accounts_list[512] = "";
		int n = 0;

		RB_DLINK_FOREACH(ptr, fp->accounts.head) {
			struct fingerprint_account *acc = ptr->data;
			if (n > 0)
				n += snprintf(accounts_list + n, sizeof(accounts_list) - n, ", ");
			n += snprintf(accounts_list + n, sizeof(accounts_list) - n, "%s", acc->account);
		}

		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE,
			"Fingerprint collision detected: %s accounts share fingerprint %s: %s",
			rb_dlink_list_length(&fp->accounts) + 1, fp->fingerprint, accounts_list);
	}
}

static void
conn_fingerprint_new_local_user(void *data)
{
	struct Client *client_p = data;
	struct connection_fingerprint *fp;
	char fingerprint[128];

	if (!enabled || !MyClient(client_p))
		return;

	generate_fingerprint(client_p, fingerprint, sizeof(fingerprint));
	fp = find_or_create_fingerprint(fingerprint);
	associate_account(fp, client_p);
	check_collisions(fp, client_p);
}

static void
conn_fingerprint_client_exit(void *data)
{
	/* Cleanup handled by expiration */
}

static void
fingerprint_expire(void *unused)
{
	rb_dictionary_iter iter;
	struct connection_fingerprint *fp;
	time_t now = rb_current_time();
	time_t expire_time = now - 86400; /* 24 hours */

	RB_DICTIONARY_FOREACH(fp, &iter, fingerprint_dict) {
		if (fp->last_seen < expire_time) {
			rb_dlink_node *ptr, *next;

			RB_DLINK_FOREACH_SAFE(ptr, next, fp->accounts.head) {
				rb_dlinkDelete(ptr, &fp->accounts);
				rb_free(ptr->data);
			}

			rb_dlinkDelete(&fp->node, NULL);
			rb_dictionary_delete(fingerprint_dict, fp->fingerprint);
			rb_free(fp);
		}
	}
}

static int
modinit(void)
{
	fingerprint_dict = rb_dictionary_create("fingerprints", rb_dictionary_str_casecmp);
	fingerprint_expire_ev = rb_event_addish("fingerprint_expire", fingerprint_expire, NULL, 3600);
	return 0;
}

static void
moddeinit(void)
{
	rb_dictionary_iter iter;
	struct connection_fingerprint *fp;

	if (fingerprint_expire_ev != NULL)
		rb_event_delete(fingerprint_expire_ev);

	RB_DICTIONARY_FOREACH(fp, &iter, fingerprint_dict) {
		rb_dlink_node *ptr, *next;

		RB_DLINK_FOREACH_SAFE(ptr, next, fp->accounts.head) {
			rb_dlinkDelete(ptr, &fp->accounts);
			rb_free(ptr->data);
		}

		rb_dlinkDelete(&fp->node, NULL);
		rb_free(fp);
	}

	if (fingerprint_dict != NULL) {
		rb_dictionary_destroy(fingerprint_dict, NULL, NULL);
		fingerprint_dict = NULL;
	}
}

DECLARE_MODULE_AV2(conn_fingerprint, modinit, moddeinit, NULL, NULL, conn_fingerprint_hfnlist, NULL, NULL, conn_fingerprint_desc);

