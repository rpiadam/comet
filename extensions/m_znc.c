/*
 * FoxComet: a modern, highly scalable IRCv3 server
 * m_znc.c: ZNC account management commands
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
#include "numeric.h"
#include "logger.h"
#include "s_conf.h"
#include "msg.h"
#include "parse.h"
#include "messages.h"
#include <sys/wait.h>
#include <unistd.h>

static const char znc_desc[] = "Adds ZNC account management commands (ZNCRegister, ZNCList, ZNCDel, ZNCPasswd)";

/* Configuration - can be set via config file or environment */
static const char *znc_bin = NULL;
static const char *znc_config_dir = NULL;
static const char *znc_control_port = NULL;

/* Forward declarations */
static void m_zncregister(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static void m_znclist(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static void m_zncdel(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static void m_zncpasswd(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static void m_znchelp(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);

/* Command message tables */
struct Message zncregister_msgtab = {
	"ZNCRegister", 0, 0, 0, 0,
	{mg_unreg, {m_zncregister, 2}, mg_ignore, mg_ignore, mg_ignore, {m_zncregister, 2}}
};

struct Message znclist_msgtab = {
	"ZNCList", 0, 0, 0, 0,
	{mg_unreg, {m_znclist, 0}, mg_ignore, mg_ignore, mg_ignore, {m_znclist, 0}}
};

struct Message zncdel_msgtab = {
	"ZNCDel", 0, 0, 0, 0,
	{mg_unreg, {m_zncdel, 1}, mg_ignore, mg_ignore, mg_ignore, {m_zncdel, 1}}
};

struct Message zncpasswd_msgtab = {
	"ZNCPasswd", 0, 0, 0, 0,
	{mg_unreg, {m_zncpasswd, 2}, mg_ignore, mg_ignore, mg_ignore, {m_zncpasswd, 2}}
};

struct Message znchelp_msgtab = {
	"ZNCHelp", 0, 0, 0, 0,
	{mg_unreg, {m_znchelp, 0}, mg_ignore, mg_ignore, mg_ignore, {m_znchelp, 0}}
};

mapi_clist_av1 znc_clist[] = {
	&zncregister_msgtab,
	&znclist_msgtab,
	&zncdel_msgtab,
	&zncpasswd_msgtab,
	&znchelp_msgtab,
	NULL
};

/* Execute ZNC command via control interface */
static int
znc_exec_command(const char *username, const char *command, const char *args, char *output, size_t output_size)
{
	char cmd[512];
	FILE *fp;
	int ret = -1;

	/* Try to use znc command line if available */
	if (znc_bin != NULL && access(znc_bin, X_OK) == 0)
	{
		snprintf(cmd, sizeof(cmd), "%s -d %s %s %s %s 2>&1",
			znc_bin,
			znc_config_dir ? znc_config_dir : "/var/lib/znc",
			command,
			username ? username : "",
			args ? args : "");
	}
	else
	{
		/* Fallback: try to use znc control interface via socket */
		/* This is a simplified version - in production you'd want to use ZNC's control interface properly */
		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE,
			"ZNC: znc binary not found or not executable");
		return -1;
	}

	fp = popen(cmd, "r");
	if (fp == NULL)
		return -1;

	if (fgets(output, output_size, fp) != NULL)
		ret = 0;

	pclose(fp);
	return ret;
}

/* ZNCRegister - Register a new ZNC account */
static void
m_zncregister(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char output[512];
	char *username, *password;
	const char *nick;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2]))
	{
		sendto_one_notice(source_p, ":ZNCRegister syntax: ZNCRegister <username> <password>");
		sendto_one_notice(source_p, ":Example: ZNCRegister myuser mypassword");
		return;
	}

	username = LOCAL_COPY(parv[1]);
	password = LOCAL_COPY(parv[2]);

	/* Validate username (alphanumeric, underscore, dash) */
	for (char *p = username; *p; p++)
	{
		if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-')
		{
			sendto_one_notice(source_p, ":ZNCRegister: Username can only contain letters, numbers, underscores, and dashes");
			return;
		}
	}

	/* Use IRC nick as default if username not provided differently */
	nick = source_p->name;

	/* Create ZNC user account */
	if (znc_exec_command(username, "adduser", password, output, sizeof(output)) == 0)
	{
		sendto_one_notice(source_p, ":ZNC account '%s' has been created successfully!", username);
		sendto_one_notice(source_p, ":You can now connect to ZNC using:");
		sendto_one_notice(source_p, ":  Server: <your-znc-server>");
		sendto_one_notice(source_p, ":  Username: %s", username);
		sendto_one_notice(source_p, ":  Password: %s", password);
		ilog(L_MAIN, "ZNC account created: %s by %s", username, source_p->name);
	}
	else
	{
		if (strstr(output, "already exists") != NULL)
			sendto_one_notice(source_p, ":ZNCRegister: Account '%s' already exists", username);
		else
			sendto_one_notice(source_p, ":ZNCRegister: Failed to create account. %s", output);
		ilog(L_MAIN, "ZNC account creation failed for %s by %s: %s", username, source_p->name, output);
	}
}

/* ZNCList - List ZNC accounts (simplified - shows user's own account) */
static void
m_znclist(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char output[512];
	const char *username = source_p->name;

	/* Try to list user's account info */
	if (znc_exec_command(username, "listusers", NULL, output, sizeof(output)) == 0)
	{
		sendto_one_notice(source_p, ":Your ZNC account information:");
		sendto_one_notice(source_p, ":%s", output);
	}
	else
	{
		sendto_one_notice(source_p, ":ZNCList: Unable to retrieve account information");
		sendto_one_notice(source_p, ":You may not have a ZNC account yet. Use ZNCRegister to create one.");
	}
}

/* ZNCDel - Delete a ZNC account */
static void
m_zncdel(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char output[512];
	char *username;

	if (parc < 2 || EmptyString(parv[1]))
	{
		sendto_one_notice(source_p, ":ZNCDel syntax: ZNCDel <username>");
		sendto_one_notice(source_p, ":Warning: This will permanently delete your ZNC account!");
		return;
	}

	username = LOCAL_COPY(parv[1]);

	/* Security: Users can only delete their own account (matching IRC nick) */
	if (irccmp(username, source_p->name) != 0)
	{
		sendto_one_notice(source_p, ":ZNCDel: You can only delete your own account (matching your IRC nick)");
		return;
	}

	if (znc_exec_command(username, "deluser", NULL, output, sizeof(output)) == 0)
	{
		sendto_one_notice(source_p, ":ZNC account '%s' has been deleted successfully", username);
		ilog(L_MAIN, "ZNC account deleted: %s by %s", username, source_p->name);
	}
	else
	{
		sendto_one_notice(source_p, ":ZNCDel: Failed to delete account. %s", output);
		ilog(L_MAIN, "ZNC account deletion failed for %s by %s: %s", username, source_p->name, output);
	}
}

/* ZNCPasswd - Change ZNC account password */
static void
m_zncpasswd(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	char output[512];
	char *username, *password;
	const char *nick = source_p->name;

	if (parc < 3 || EmptyString(parv[1]) || EmptyString(parv[2]))
	{
		sendto_one_notice(source_p, ":ZNCPasswd syntax: ZNCPasswd <username> <newpassword>");
		return;
	}

	username = LOCAL_COPY(parv[1]);
	password = LOCAL_COPY(parv[2]);

	/* Security: Users can only change their own account password */
	if (irccmp(username, source_p->name) != 0)
	{
		sendto_one_notice(source_p, ":ZNCPasswd: You can only change your own account password");
		return;
	}

	if (znc_exec_command(username, "setpass", password, output, sizeof(output)) == 0)
	{
		sendto_one_notice(source_p, ":ZNC account password for '%s' has been changed successfully", username);
		ilog(L_MAIN, "ZNC password changed for %s by %s", username, source_p->name);
	}
	else
	{
		sendto_one_notice(source_p, ":ZNCPasswd: Failed to change password. %s", output);
		ilog(L_MAIN, "ZNC password change failed for %s by %s: %s", username, source_p->name, output);
	}
}

/* ZNCHelp - Show help for ZNC commands */
static void
m_znchelp(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	sendto_one_notice(source_p, ":=== ZNC Account Management Commands ===");
	sendto_one_notice(source_p, ":ZNCRegister <username> <password> - Create a new ZNC account");
	sendto_one_notice(source_p, ":ZNCList - List your ZNC account information");
	sendto_one_notice(source_p, ":ZNCPasswd <username> <newpassword> - Change your ZNC account password");
	sendto_one_notice(source_p, ":ZNCDel <username> - Delete your ZNC account");
	sendto_one_notice(source_p, ":ZNCHelp - Show this help message");
	sendto_one_notice(source_p, ":Note: You can only manage accounts matching your IRC nickname");
}

static int
_modinit(void)
{
	/* Try to find znc binary in common locations */
	const char *paths[] = {
		"/usr/bin/znc",
		"/usr/local/bin/znc",
		"/opt/znc/bin/znc",
		NULL
	};

	/* Check for ZNC binary */
	for (int i = 0; paths[i] != NULL; i++)
	{
		if (access(paths[i], X_OK) == 0)
		{
			znc_bin = paths[i];
			break;
		}
	}

	/* Try to find ZNC config directory */
	const char *config_paths[] = {
		"/var/lib/znc",
		"/home/znc/.znc",
		"~/.znc",
		NULL
	};

	for (int i = 0; config_paths[i] != NULL; i++)
	{
		char path[512];
		snprintf(path, sizeof(path), "%s/configs/znc.conf", config_paths[i]);
		if (access(path, R_OK) == 0)
		{
			znc_config_dir = config_paths[i];
			break;
		}
	}

	if (znc_bin == NULL)
	{
		ilog(L_MAIN, "m_znc: ZNC binary not found. ZNC commands will not work.");
		sendto_realops_snomask(SNO_GENERAL, L_NETWIDE,
			"m_znc: ZNC binary not found. Please configure znc_bin path.");
	}

	return 0;
}

static void
_moddeinit(void)
{
	/* Nothing to clean up */
}

DECLARE_MODULE_AV2(m_znc, _modinit, _moddeinit, znc_clist, NULL, NULL, NULL, NULL, znc_desc);

