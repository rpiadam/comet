# ZNC Account Management Extension

This extension provides built-in IRC commands for users to manage their ZNC (ZNC Bouncer) accounts directly from IRC.

## Features

- **ZNCRegister**: Create new ZNC accounts
- **ZNCList**: View account information
- **ZNCPasswd**: Change account passwords
- **ZNCDel**: Delete accounts
- **ZNCHelp**: Show help information

## Requirements

- ZNC must be installed and accessible
- ZNC binary must be in PATH or configured
- ZNC config directory must be accessible

## Setup

### 1. Install ZNC

Make sure ZNC is installed on your system:

```bash
# Debian/Ubuntu
sudo apt-get install znc

# RHEL/CentOS/Fedora
sudo dnf install znc

# Or compile from source
# https://github.com/znc/znc
```

### 2. Configure ZNC

The module will automatically search for ZNC in common locations:
- `/usr/bin/znc`
- `/usr/local/bin/znc`
- `/opt/znc/bin/znc`

ZNC config directories searched:
- `/var/lib/znc`
- `/home/znc/.znc`
- `~/.znc`

### 3. Load the Module

Add to your `ircd.conf`:

```
loadmodule "extensions/m_znc";
```

### 4. Restart or Rehash

```bash
# Restart the server, or
/REHASH
```

## Commands

### ZNCRegister

Create a new ZNC account.

**Syntax**: `ZNCRegister <username> <password>`

**Example**:
```
/msg YourNick ZNCRegister myuser mypassword123
```

**Notes**:
- Username must match your IRC nickname (for security)
- Username can only contain letters, numbers, underscores, and dashes
- Password should be strong and secure

### ZNCList

View your ZNC account information.

**Syntax**: `ZNCList`

**Example**:
```
/msg YourNick ZNCList
```

### ZNCPasswd

Change your ZNC account password.

**Syntax**: `ZNCPasswd <username> <newpassword>`

**Example**:
```
/msg YourNick ZNCPasswd myuser newpassword456
```

**Security**: You can only change passwords for accounts matching your IRC nickname.

### ZNCDel

Delete your ZNC account.

**Syntax**: `ZNCDel <username>`

**Example**:
```
/msg YourNick ZNCDel myuser
```

**Warning**: This permanently deletes your ZNC account and all associated data!

**Security**: You can only delete accounts matching your IRC nickname.

### ZNCHelp

Show help for all ZNC commands.

**Syntax**: `ZNCHelp`

## Usage Example

1. **Register a new account**:
   ```
   /ZNCRegister mynick mypassword
   ```

2. **Connect to ZNC**:
   - Server: `your-znc-server.example.com`
   - Port: `6667` (or your ZNC port)
   - Username: `mynick`
   - Password: `mypassword`

3. **Change password later**:
   ```
   /ZNCPasswd mynick newpassword
   ```

4. **View account info**:
   ```
   /ZNCList
   ```

## Security Features

- **Nickname matching**: Users can only manage accounts matching their IRC nickname
- **Input validation**: Usernames are validated for allowed characters
- **Logging**: All account operations are logged

## Troubleshooting

### "ZNC binary not found"

1. Make sure ZNC is installed
2. Check that ZNC binary is in PATH
3. Verify ZNC binary is executable
4. Check IRCd logs for the exact path being searched

### "Failed to create account"

1. Check ZNC is running and accessible
2. Verify ZNC config directory permissions
3. Check IRCd logs for detailed error messages
4. Ensure ZNC has write permissions to its config directory

### Commands not working

1. Verify the module is loaded: `/MODLIST`
2. Check that you're using the correct command syntax
3. Ensure your IRC nickname matches the username you're trying to manage

## Technical Details

The module uses ZNC's command-line interface to manage accounts:
- `znc -d <config_dir> adduser <username> <password>`
- `znc -d <config_dir> deluser <username>`
- `znc -d <config_dir> setpass <username> <password>`
- `znc -d <config_dir> listusers`

## Future Enhancements

Possible improvements:
- Configuration via ircd.conf blocks
- Support for ZNC control interface (socket-based)
- Network-specific account management
- Account limits and quotas
- Admin commands for managing all accounts

