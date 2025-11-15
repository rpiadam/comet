# Discord Relay Extension

This extension relays IRC messages to Discord via webhooks, allowing you to bridge IRC channels with Discord channels.

## Features

- **Automatic Message Relay**: Relays channel messages to Discord in real-time
- **Webhook Support**: Uses Discord webhooks (no bot token required)
- **Configurable**: Can relay all channels or specific channels
- **Private Message Support**: Optional relay of private messages
- **JSON Escaping**: Properly escapes special characters for Discord
- **IPv6/IPv4 Support**: Works with both IPv6 and IPv4

## Setup

### 1. Create a Discord Webhook

1. Go to your Discord server settings
2. Navigate to **Integrations** → **Webhooks**
3. Click **New Webhook**
4. Configure the webhook:
   - Name it (e.g., "IRC Relay")
   - Choose the channel where messages should appear
   - Copy the webhook URL

### 2. Configure the Module

Set the webhook URL as an environment variable:

```bash
export DISCORD_WEBHOOK_URL="https://discord.com/api/webhooks/YOUR_WEBHOOK_ID/YOUR_WEBHOOK_TOKEN"
```

### 3. Load the Module

Add to your `ircd.conf`:

```
loadmodule "extensions/discord_relay";
```

### 4. Restart or Rehash

```bash
# Restart the server, or
/REHASH
```

## Configuration Options

Currently, the module uses environment variables for configuration:

- `DISCORD_WEBHOOK_URL`: The Discord webhook URL (required)

By default, the module relays **all channel messages** when a webhook URL is configured.

## Message Format

Messages are sent to Discord in the format:
```
[#channel] <nickname> message text
```

For private messages (if enabled):
```
<nickname> message text
```

## Limitations

- **One-way relay**: Currently only IRC → Discord (Discord → IRC would require Discord Bot API)
- **Webhook rate limits**: Discord webhooks have rate limits (30 requests per minute)
- **Message length**: Messages are truncated to 2000 characters (Discord's limit)

## Future Enhancements

Possible improvements:
- Channel-specific webhook URLs
- Message filtering/formatting options
- Rate limiting to avoid Discord rate limits
- Bidirectional relay (Discord → IRC) using Discord Bot API
- Configuration via ircd.conf blocks

## Troubleshooting

### Messages not appearing in Discord

1. Check that the webhook URL is correct
2. Verify the webhook is still active in Discord
3. Check IRCd logs for errors
4. Ensure the module is loaded: `/MODLIST`

### Module not loading

1. Check that the module compiled successfully
2. Verify the module path in `ircd.conf`
3. Check IRCd logs for module loading errors

## Security Notes

- **Webhook URLs are sensitive**: They allow anyone to post to your Discord channel
- **Environment variables**: Consider using a secure method to set environment variables
- **Private messages**: Be careful when enabling private message relay

