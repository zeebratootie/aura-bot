# Config File Changes - Exact Edits Needed

## File: `config-example.ini` (Lines 930-980)

### Current State:
```ini
discord.enabled = no
discord.token = 
discord.invites.mode = allow_list
discord.invites.list = 
discord.direct_messages.mode = all
discord.direct_messages.list = 
discord.commands.namespace = aura
discord.commands.custom_host.permissions = verified
discord.sudo_users = 
discord.log_games.enabled = no
discord.log_games.channels = 
```

---

### Changes Needed:

#### Edit 1: Enable Discord
**Find:**
```
discord.enabled = no
```

**Replace with:**
```
discord.enabled = yes
```

---

#### Edit 2: Add Your Bot Token
**Find:**
```
discord.token = 
```

**Replace with (paste your actual token):**
```
discord.token = MTk4NjIyNDgzNzYxMjgwMzU4.CmRlOQ.ZAmWGmMZRP7pUe0NLqicZ1234567890
```

---

#### Edit 3: Set Server Allowlist
**Find:**
```
discord.invites.list = 
```

**Replace with (your server ID):**
```
discord.invites.list = 1234567890123456789
```

---

#### Edit 4: Set Permissions for /host Command
**Find:**
```
discord.commands.custom_host.permissions = verified
```

**Replace with:**
```
discord.commands.custom_host.permissions = admin
```

**Options:**
- `verified` - Verified users
- `admin` - Server admins
- `sudoer` - Bot sudoers (from discord.sudo_users)
- `everyone` - Anyone (not recommended)

---

#### Edit 5: Add Your User ID as Sudoer (Optional but Recommended)
**Find:**
```
discord.sudo_users = 
```

**Replace with (your user ID):**
```
discord.sudo_users = 9876543210987654321
```

---

## Summary of Changes

| Line | Change | From | To |
|------|--------|------|-----|
| 934 | Enable | `no` | `yes` |
| 940 | Token | `(empty)` | `YOUR_BOT_TOKEN` |
| 952 | Server List | `(empty)` | `YOUR_SERVER_ID` |
| 971 | Permissions | `verified` | `admin` |
| 975 | Sudoers | `(empty)` | `YOUR_USER_ID` |

---

## Values You Need

Before editing, gather these values:

### 1. Bot Token
- Go to: https://discord.com/developers/applications
- Click your application
- Click "Bot"
- Copy the token under USERNAME

**Format:** `MTk4NjIyNDgzNzYxMjgwMzU4.CmRlOQ.ZAmWGmMZRP7pUe0NLqicZ...`

### 2. Server ID
In Discord:
- Right-click your server name
- Click "Copy Server ID"

**Format:** `1234567890123456789` (19 digits)

### 3. User ID (Your ID)
In Discord:
- Right-click your username anywhere
- Click "Copy User ID"

**Format:** `9876543210987654321` (19 digits)

---

## After Editing: Deploy to VPS

### Option 1: If using local Docker
```bash
docker cp config-example.ini aura-running:/app/config-example.ini
docker restart aura-running
```

### Option 2: If on VPS
```bash
# SSH into VPS
ssh root@YOUR_VPS_IP

# Edit directly
nano /app/config-example.ini
# Make the changes above
# Press Ctrl+X, Y, Enter to save

# Restart container
docker restart aura-running

# Check logs
docker logs aura-running | tail -20
```

---

## Verify It Works

### Check logs for Discord connection:
```bash
docker logs aura-running | grep -i discord
```

**Should show:**
```
[DISCORD] Joined server <<YourServerName>> (#1234567890123456789).
```

### Test in Discord:
```
/host map:twrpg title:TestGame
```

Bot should respond: **"Hosting your game briefly!"**

---

## Line Numbers Reference

```
Line 930: # DISCORD CONFIGURATION #
Line 934: discord.enabled = no              ← Change to "yes"
Line 940: discord.token =                   ← Add your token
Line 948: discord.invites.mode =            ← Keep as "allow_list"
Line 952: discord.invites.list =            ← Add your server ID
Line 955: discord.direct_messages.mode =    ← Keep as "all"
Line 960: discord.direct_messages.list =    ← Leave empty (unless blocking users)
Line 968: discord.commands.namespace =      ← Keep as "aura" (or change prefix)
Line 971: discord.commands.custom_host.permissions = ← Change to "admin"
Line 975: discord.sudo_users =              ← Add your user ID
Line 979: discord.log_games.enabled =       ← Keep as "no" (optional)
Line 982: discord.log_games.channels =      ← Leave empty
```

---

## Common Mistakes to Avoid

❌ **DON'T:**
- Use quotes around token or IDs: `discord.token = "YOUR_TOKEN"` 
- Include angle brackets: `discord.sudo_users = <YOUR_ID>`
- Leave token blank but set enabled to yes
- Miss the MESSAGE CONTENT INTENT in Discord settings

✅ **DO:**
- Copy token exactly as shown
- Paste IDs without quotes
- Enable MESSAGE CONTENT INTENT
- Test with `/host` command

---

## If Something Goes Wrong

```bash
# View full config
docker exec aura-running cat /app/config-example.ini | grep discord

# Check error logs
docker logs aura-running | grep -i "error\|discord"

# Restart container
docker restart aura-running

# Wait and check again
sleep 5
docker logs aura-running | tail -30
```

---

**Next Step:** Edit `config-example.ini` with the changes above, then deploy to VPS!
