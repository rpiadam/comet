/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_zline.c: ZLINE command for IP-based bans (alias for DLINE)
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
#include "s_conf.h"
#include "s_newconf.h"
#include "hostmask.h"
#include "bandbi.h"
#include "operhash.h"

static const char zline_desc[] = "Provides the ZLINE command for IP-based bans (alias for DLINE)";

static void m_zline(struct MsgBuf *, struct Client *, struct Client *, int, const char **);
static void m_unzline(struct MsgBuf *, struct Client *, struct Client *, int, const char **);

struct Message zline_msgtab = {
	"ZLINE", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_zline, 2}}
};

struct Message unzline_msgtab = {
	"UNZLINE", 0, 0, 0, 0,
	{mg_ignore, mg_not_oper, mg_ignore, mg_ignore, mg_ignore, {m_unzline, 1}}
};

mapi_clist_av1 zline_clist[] = { &zline_msgtab, &unzline_msgtab, NULL };

static void
apply_zline(struct Client *source_p, const char *dlhost, int tdline_time, char *reason)
{
	struct ConfItem *aconf;
	char *oper_reason;
	struct rb_sockaddr_storage daddr;
	int t = AF_INET, ty, b;
	const char *creason;

	ty = parse_netmask_strict(dlhost, &daddr, &b);
	if(ty != HM_IPV4 && ty != HM_IPV6)
	{
		sendto_one_notice(source_p, ":Invalid Z-Line [%s] - doesn't look like IP[/cidr]", dlhost);
		return;
	}
	if(ty == HM_IPV6)
		t = AF_INET6;
	else
		t = AF_INET;

	/* This means zlines wider than /16 cannot be set remotely */
	if(IsOperAdmin(source_p))
	{
		if(b < 8)
		{
			sendto_one_notice(source_p,
					  ":For safety, bitmasks less than 8 require conf access.");
			return;
		}
	}
	else
	{
		if(b < 16)
		{
			sendto_one_notice(source_p,
					  ":Zline bitmasks less than 16 are for admins only.");
			return;
		}
	}

	rb_set_time();

	aconf = make_conf();
	aconf->status = CONF_DLINE;
	aconf->created = rb_current_time();
	aconf->host = rb_strdup(dlhost);
	aconf->info.oper = operhash_add(get_oper_name(source_p));

	if(strlen(reason) > BANREASONLEN)
		reason[BANREASONLEN] = '\0';

	/* Look for an oper reason */
	if((oper_reason = strchr(reason, '|')) != NULL)
	{
		*oper_reason = '\0';
		oper_reason++;

		if(!EmptyString(oper_reason))
			aconf->spasswd = rb_strdup(oper_reason);
	}

	aconf->passwd = rb_strdup(reason);

	if(tdline_time > 0)
	{
		aconf->flags |= CONF_FLAGS_TEMPORARY;
		aconf->hold = rb_current_time() + tdline_time;
		aconf->lifetime = aconf->hold;
		add_temp_dline(aconf);
		sendto_realops_snomask(SNO_GENERAL, L_ALL,
				       "%s added temporary %d min. Z-Line for [%s] [%s]",
				       get_oper_name(source_p), tdline_time / 60,
				       aconf->host, reason);
		ilog(L_KLINE, "Z %s %d %s %s",
		     get_oper_name(source_p), tdline_time / 60, dlhost, reason);
		sendto_one_notice(source_p, ":Added temporary %d min. Z-Line [%s]",
				  tdline_time / 60, aconf->host);
	}
	else
	{
		bandb_add(BANDB_DLINE, source_p, aconf->host, NULL, aconf->passwd, NULL, 0);
		sendto_realops_snomask(SNO_GENERAL, L_ALL, "%s added Z-Line for [%s] [%s]",
				       get_oper_name(source_p), aconf->host, aconf->passwd);
		sendto_one_notice(source_p, ":Added Z-Line for [%s] [%s]",
				  aconf->host, aconf->passwd);
		ilog(L_KLINE, "Z %s 0 %s %s", get_oper_name(source_p), dlhost, aconf->passwd);
	}

	rb_dlinkAddAlloc(aconf, &dline_conf_list);
	check_dlines();
}

static void
m_zline(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char def[] = "No Reason";
	const char *dlhost;
	char *reason = def;
	int tdline_time = 0;
	int loc = 1;

	if(!IsOperK(source_p))
	{
		sendto_one(source_p, form_str(ERR_NOPRIVS), me.name, source_p->name, "kline");
		return;
	}

	if((tdline_time = valid_temp_time(parv[loc])) >= 0)
		loc++;

	if (loc >= parc)
	{
		sendto_one_notice(source_p, ":Need an IP to Z-Line");
		return;
	}

	dlhost = parv[loc];
	loc++;

	/* would break the protocol */
	if (*dlhost == ':')
	{
		sendto_one_notice(source_p, ":Invalid Z-Line [%s] - IP cannot start with :", dlhost);
		return;
	}

	if(parc >= loc + 1 && !EmptyString(parv[loc]))
		reason = LOCAL_COPY(parv[loc]);

	apply_zline(source_p, dlhost, tdline_time, reason);
}

static void
m_unzline(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	const char *dlhost;
	struct ConfItem *aconf;
	char buf[BUFSIZE];
	int masktype;

	if (parc < 2 || EmptyString(parv[1])) {
		sendto_one_notice(source_p, ":*** Syntax: UNZLINE <ip/cidr>");
		return;
	}

	dlhost = parv[1];

	masktype = parse_netmask(dlhost, NULL, NULL);
	if(masktype != HM_IPV4 && masktype != HM_IPV6)
	{
		sendto_one_notice(source_p, ":Invalid Z-Line [%s] - doesn't look like IP[/cidr]", dlhost);
		return;
	}

	aconf = find_exact_conf_by_address(dlhost, CONF_DLINE, NULL);
	if(aconf == NULL)
	{
		sendto_one_notice(source_p, ":No Z-Line for %s", dlhost);
		return;
	}

	rb_strlcpy(buf, aconf->host, sizeof buf);
	
	/* Check if it's a temporary dline */
	if(aconf->flags & CONF_FLAGS_TEMPORARY)
	{
		/* Remove from temp dlines - duplicate remove_temp_dline logic */
		rb_dlink_node *ptr;
		int i;
		bool found = false;

		for(i = 0; i < LAST_TEMP_TYPE; i++)
		{
			RB_DLINK_FOREACH(ptr, temp_dlines[i].head)
			{
				if(ptr->data == aconf)
				{
					rb_dlinkDelete(ptr, &temp_dlines[i]);
					found = true;
					break;
				}
			}
			if(found)
				break;
		}
		
		if(found)
		{
			sendto_one_notice(source_p, ":Removed temporary Z-Line [%s]", buf);
			sendto_realops_snomask(SNO_GENERAL, L_ALL, "%s removed temporary Z-Line [%s]",
					       get_oper_name(source_p), buf);
			ilog(L_KLINE, "UZ %s %s", get_oper_name(source_p), buf);
			free_conf(aconf);
			check_dlines();
			return;
		}
	}
	
	/* Permanent dline */
	bandb_del(BANDB_DLINE, aconf->host, NULL);
	delete_one_address_conf(aconf->host, aconf);
	sendto_one_notice(source_p, ":Removed Z-Line [%s]", buf);
	sendto_realops_snomask(SNO_GENERAL, L_ALL, "%s removed Z-Line [%s]",
			       get_oper_name(source_p), buf);
	ilog(L_KLINE, "UZ %s %s", get_oper_name(source_p), buf);
}

DECLARE_MODULE_AV2(zline, NULL, NULL, zline_clist, NULL, NULL, NULL, NULL, zline_desc);

