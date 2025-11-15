/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * geoip_block.c: GeoIP-based connection blocking extension
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
#include "hostmask.h"
#include "ircd.h"
#include "match.h"
#include "modules.h"
#include "msg.h"
#include "parse.h"
#include "send.h"
#include "s_user.h"
#include "numeric.h"
#include "reject.h"

static const char geoip_block_desc[] = "Block connections based on geographic location";

/* Configuration */
static bool enabled = false;
static rb_dlink_list block_countries;
static rb_dlink_list allow_countries;
static rb_dlink_list block_asns;
static rb_dlink_list allow_asns;
static bool require_auth_for_blocked = false;

/* Hook functions */
static void geoip_block_new_local_user(void *data);

mapi_hfn_list_av1 geoip_block_hfnlist[] = {
	{ "new_local_user", geoip_block_new_local_user },
	{ NULL, NULL }
};

struct geoip_country {
	char code[3];
	rb_dlink_node node;
};

struct geoip_asn {
	char asn[32];
	rb_dlink_node node;
};

/* GeoIP lookup using external API or MaxMind library */
static char *geoip_api_url = NULL;
static char cached_country[3] = {0};
static char cached_asn[32] = {0};
static struct sockaddr_storage cached_addr;
static time_t cache_time = 0;
#define GEOIP_CACHE_TTL 3600

/* Extract IP address string from sockaddr */
static void
get_ip_string(struct sockaddr *addr, char *buf, size_t buflen)
{
	if (addr->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)addr;
		rb_inet_ntop(AF_INET, &sin->sin_addr, buf, buflen);
	} else if (addr->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;
		rb_inet_ntop(AF_INET6, &sin6->sin6_addr, buf, buflen);
	} else {
		buf[0] = '\0';
	}
}

/* Simple country code lookup - supports MaxMind GeoIP2 or external API */
static const char *
get_country_code(struct sockaddr *addr)
{
	char ip_str[INET6_ADDRSTRLEN];
	static char country_code[3] = {0};
	
	if (addr == NULL)
		return NULL;

	get_ip_string(addr, ip_str, sizeof(ip_str));
	if (ip_str[0] == '\0')
		return NULL;

#ifdef HAVE_GEOIP2
	/* If MaxMind GeoIP2 is available, use it here */
	/* Example: MMDB_lookup_string(mmdb, ip_str, &result); */
	/* For now, we'll use a simple approach */
#endif

	/* For now, return NULL (no blocking) unless configured with external API */
	/* In production, integrate MaxMind GeoIP2 library here */
	/* The framework is ready - just need to add the actual lookup */
	
	/* Check if this is a local/private IP */
	if (addr->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)addr;
		uint32_t ip = ntohl(sin->sin_addr.s_addr);
		/* Private IP ranges */
		if ((ip >= 0x0A000000 && ip <= 0x0AFFFFFF) || /* 10.0.0.0/8 */
		    (ip >= 0xAC100000 && ip <= 0xAC1FFFFF) || /* 172.16.0.0/12 */
		    (ip >= 0xC0A80000 && ip <= 0xC0A8FFFF)) { /* 192.168.0.0/16 */
			return NULL; /* Don't block private IPs */
		}
	}

	/* Placeholder - return NULL to allow connection */
	/* To enable: integrate MaxMind GeoIP2 or configure external API */
	return NULL;
}

/* Simple ASN lookup - supports MaxMind GeoIP2 or external API */
static const char *
get_asn(struct sockaddr *addr)
{
	char ip_str[INET6_ADDRSTRLEN];
	
	if (addr == NULL)
		return NULL;

	get_ip_string(addr, ip_str, sizeof(ip_str));
	if (ip_str[0] == '\0')
		return NULL;

#ifdef HAVE_GEOIP2
	/* If MaxMind GeoIP2 is available, use it here */
	/* Example: MMDB_lookup_string(mmdb, ip_str, &result); */
#endif

	/* Placeholder - return NULL to allow connection */
	/* To enable: integrate MaxMind GeoIP2 or configure external API */
	return NULL;
}

static bool
is_country_blocked(const char *country_code)
{
	rb_dlink_node *ptr;

	if (country_code == NULL)
		return false;

	RB_DLINK_FOREACH(ptr, block_countries.head) {
		struct geoip_country *country = ptr->data;
		if (strcasecmp(country->code, country_code) == 0)
			return true;
	}

	return false;
}

static bool
is_country_allowed(const char *country_code)
{
	rb_dlink_node *ptr;

	if (rb_dlink_list_length(&allow_countries) == 0)
		return true; /* No restrictions */

	if (country_code == NULL)
		return false;

	RB_DLINK_FOREACH(ptr, allow_countries.head) {
		struct geoip_country *country = ptr->data;
		if (strcasecmp(country->code, country_code) == 0)
			return true;
	}

	return false;
}

static bool
is_asn_blocked(const char *asn)
{
	rb_dlink_node *ptr;

	if (asn == NULL)
		return false;

	RB_DLINK_FOREACH(ptr, block_asns.head) {
		struct geoip_asn *asn_entry = ptr->data;
		if (strcasecmp(asn_entry->asn, asn) == 0)
			return true;
	}

	return false;
}

static bool
is_asn_allowed(const char *asn)
{
	rb_dlink_node *ptr;

	if (rb_dlink_list_length(&allow_asns) == 0)
		return true; /* No restrictions */

	if (asn == NULL)
		return false;

	RB_DLINK_FOREACH(ptr, allow_asns.head) {
		struct geoip_asn *asn_entry = ptr->data;
		if (strcasecmp(asn_entry->asn, asn) == 0)
			return true;
	}

	return false;
}

static void
geoip_block_new_local_user(void *data)
{
	struct Client *client_p = data;
	const char *country_code;
	const char *asn;
	struct ConfItem *aconf;

	if (!enabled || !MyClient(client_p))
		return;

	/* Skip if exempt */
	aconf = find_address_conf(client_p->host, client_p->sockhost,
				client_p->username,
				IsGotId(client_p) ? client_p->username : client_p->username,
				(struct sockaddr *) &client_p->localClient->ip,
				GET_SS_FAMILY(&client_p->localClient->ip),
				client_p->localClient->auth_user);
	if (aconf != NULL && (aconf->status & CONF_EXEMPTKLINE))
		return;

	country_code = get_country_code((struct sockaddr *)&client_p->localClient->ip);
	asn = get_asn((struct sockaddr *)&client_p->localClient->ip);

	/* Check country blocking */
	if (is_country_blocked(country_code) || !is_country_allowed(country_code)) {
		sendto_realops_snomask(SNO_REJ, L_NETWIDE,
			"GeoIP blocked: %s (%s@%s) [%s] from country %s",
			client_p->name, client_p->username, client_p->host,
			client_p->sockhost, country_code ? country_code : "unknown");
		
		if (require_auth_for_blocked && !IsUser(client_p)) {
			/* Allow but require authentication */
			return;
		}

		sendto_one(client_p, form_str(ERR_YOUREBANNEDCREEP),
			me.name, client_p->name, "Connection from blocked country");
		add_reject(client_p, NULL, NULL, NULL, "GeoIP blocked");
		exit_client(NULL, client_p, &me, "GeoIP blocked");
		return;
	}

	/* Check ASN blocking */
	if (is_asn_blocked(asn) || !is_asn_allowed(asn)) {
		sendto_realops_snomask(SNO_REJ, L_NETWIDE,
			"GeoIP blocked: %s (%s@%s) [%s] from ASN %s",
			client_p->name, client_p->username, client_p->host,
			client_p->sockhost, asn ? asn : "unknown");
		
		if (require_auth_for_blocked && !IsUser(client_p)) {
			/* Allow but require authentication */
			return;
		}

		sendto_one(client_p, form_str(ERR_YOUREBANNEDCREEP),
			me.name, client_p->name, "Connection from blocked ASN");
		add_reject(client_p, NULL, NULL, NULL, "GeoIP blocked");
		exit_client(NULL, client_p, &me, "GeoIP blocked");
		return;
	}
}

static void
add_block_country(const char *code)
{
	struct geoip_country *country;

	if (code == NULL || strlen(code) != 2)
		return;

	country = rb_malloc(sizeof(struct geoip_country));
	rb_strlcpy(country->code, code, sizeof(country->code));
	rb_dlinkAdd(country, &country->node, &block_countries);
}

static void
add_allow_country(const char *code)
{
	struct geoip_country *country;

	if (code == NULL || strlen(code) != 2)
		return;

	country = rb_malloc(sizeof(struct geoip_country));
	rb_strlcpy(country->code, code, sizeof(country->code));
	rb_dlinkAdd(country, &country->node, &allow_countries);
}

static void
add_block_asn(const char *asn)
{
	struct geoip_asn *asn_entry;

	if (asn == NULL)
		return;

	asn_entry = rb_malloc(sizeof(struct geoip_asn));
	rb_strlcpy(asn_entry->asn, asn, sizeof(asn_entry->asn));
	rb_dlinkAdd(asn_entry, &asn_entry->node, &block_asns);
}

static void
add_allow_asn(const char *asn)
{
	struct geoip_asn *asn_entry;

	if (asn == NULL)
		return;

	asn_entry = rb_malloc(sizeof(struct geoip_asn));
	rb_strlcpy(asn_entry->asn, asn, sizeof(asn_entry->asn));
	rb_dlinkAdd(asn_entry, &asn_entry->node, &allow_asns);
}

static int
modinit(void)
{
	/* Initialize lists */
	/* Configuration would be loaded from ircd.conf here */
	/* For now, this is a framework that can be extended */
	return 0;
}

static void
moddeinit(void)
{
	rb_dlink_node *ptr, *next;

	RB_DLINK_FOREACH_SAFE(ptr, next, block_countries.head) {
		rb_dlinkDelete(ptr, &block_countries);
		rb_free(ptr->data);
	}

	RB_DLINK_FOREACH_SAFE(ptr, next, allow_countries.head) {
		rb_dlinkDelete(ptr, &allow_countries);
		rb_free(ptr->data);
	}

	RB_DLINK_FOREACH_SAFE(ptr, next, block_asns.head) {
		rb_dlinkDelete(ptr, &block_asns);
		rb_free(ptr->data);
	}

	RB_DLINK_FOREACH_SAFE(ptr, next, allow_asns.head) {
		rb_dlinkDelete(ptr, &allow_asns);
		rb_free(ptr->data);
	}
}

DECLARE_MODULE_AV2(geoip_block, modinit, moddeinit, NULL, NULL, geoip_block_hfnlist, NULL, NULL, geoip_block_desc);

