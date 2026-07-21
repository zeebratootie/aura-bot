# Discord Integration Setup - Step by Step Guide

## Question: Do I Need a New Discord Bot Token?

**You CAN reuse the same token IF you already have a Discord bot created.** The token is permanent for that bot application and doesn't expire.

However, if you:
- Don't have a Discord bot yet → Create a new one
- Lost your token → Regenerate it (old token becomes invalid)
- Want a separate bot for production → Create a new one

---

## Step-by-Step Setup

### STEP 1: Create/Get Discord Bot Token

**If you already have a bot token, skip to Step 2.**

#### Create a New Discord Bot:

1. Go to: https://discord.com/developers/applications
2. Click **"New Application"** button
3. Give it a name (e.g., "Aura Bot")
4. Click **"Create"**
5. Go to **"Bot"** tab on the left
6. Click **"Add Bot"**
7. Click **"Reset Token"** or view existing token
8. Click **"Copy"** to copy your token
9. **Save this token somewhere safe** - you'll need it in Step 3

**TOKEN FORMAT:** Looks like `MTk4NjIyNDgzNzYxMjgwMzU4.CmRlOQ.ZAmWGmMZRP7pUe0NLqicZ...`

---

### STEP 2: Configure Bot Permissions & Intents

**Still in Discord Developer Portal:**

#### A. Add Scopes & Permissions:

1. Go to **"OAuth2"** tab → **"URL Generator"**
2. Under **Scopes**, check:
   - ✅ `bot`
   - ✅ `applications.commands`
3. Under **Permissions**, check:
   - ✅ `Send Messages`
   - ✅ `Send Messages in Threads`
   - ✅ `Manage Messages`
   - ✅ `Read Message History`
   - ✅ `Use External Emojis`
   - ✅ `Use Slash Commands`

4. Copy the generated URL (you'll use this to invite the bot)

#### B. Enable Message Content Intent:

1. Go back to **"Bot"** tab
2. Scroll down to **"Privileged Gateway Intents"**
3. Enable ✅ **"MESSAGE CONTENT INTENT"**
4. Click **"Save Changes"**

---

### STEP 3: Invite Bot to Your Discord Server

1. Use the URL from Step 2A (or generate it again)
2. Open the URL in your browser
3. Select your Discord server
4. Click **"Authorize"**
5. Complete the CAPTCHA
6. Bot should now appear in your server! ✓

---

### STEP 4: Get Your Discord Server ID and User ID

#### A. Get Your Server ID:

1. In Discord, right-click on your server name
2. Click **"Copy Server ID"**
3. Save this ID (looks like: `1234567890123456789`)

#### B. Get Your User ID:

1. Right-click on your own Discord username
2. Click **"Copy User ID"**
3. Save this ID (looks like: `9876543210987654321`)

---

### STEP 5: Edit the Config File

**File to edit:** `config-example.ini` (or `config.ini` on VPS)

**Location:** Line 930-975

Make these changes:

```ini
##########################
# DISCORD CONFIGURATION #
##########################

# CHANGE THIS to "yes"
discord.enabled = yes

# PASTE YOUR BOT TOKEN HERE
discord.token = YOUR_BOT_TOKEN_HERE

# UNCOMMENT and REPLACE with your APPLICATION_ID (found in Discord Developer Portal → General)
discord.invites.url = https://discord.com/oauth2/authorize?client_id=<YOUR_APPLICATION_ID>&permissions=274877941760&scope=bot+applications.commands

# SET TO YOUR SERVER ID (allow only your server)
discord.invites.mode = allow_list
discord.invites.list = YOUR_SERVER_ID_HERE

# ALLOW ALL DIRECT MESSAGES (or set to allow_list with your user ID)
discord.direct_messages.mode = all
# discord.direct_messages.list = YOUR_USER_ID_HERE

# COMMAND PREFIX (default is "aura")
discord.commands.namespace = aura

# WHO CAN USE /host COMMAND
# Options: verified, admin, sudoer, everyone
discord.commands.custom_host.permissions = admin

# YOUR USER ID (grants you all permissions)
discord.sudo_users = YOUR_USER_ID_HERE

# OPTIONAL: Log games to Discord
discord.log_games.enabled = no
discord.log_games.channels = 
```

---

## EXAMPLE FILLED CONFIG:

```ini
discord.enabled = yes
discord.token = MTk4NjIyNDgzNzYxMjgwMzU4.CmRlOQ.ZAmWGmMZRP7pUe0NLqicZ1234567890
discord.invites.url = https://discord.com/oauth2/authorize?client_id=987654321098765432&permissions=274877941760&scope=bot+applications.commands
discord.invites.mode = allow_list
discord.invites.list = 1234567890123456789
discord.direct_messages.mode = all
discord.commands.namespace = aura
discord.commands.custom_host.permissions = admin
discord.sudo_users = 9876543210987654321
discord.log_games.enabled = no
```

---

## STEP 6: Deploy to VPS

### Option A: Copy config to running VPS container

```bash
docker cp config-example.ini aura-running:/app/config-example.ini
docker restart aura-running
```

### Option B: Rebuild and redeploy

```bash
# Push updated image (if needed)
docker push hayahaydev/aura-bot:latest

# On VPS:
docker pull hayahaydev/aura-bot:latest
docker rm -f aura-running
docker run -d --restart unless-stopped --name aura-running -p 6112:6112 -p 6113:6113/tcp -p 6113:6113/udp -p 6115:6115 hayahaydev/aura-bot:latest
```

---

## STEP 7: Verify Discord Integration Works

### Check Docker logs:

```bash
docker logs aura-running | grep -i discord
```

**Expected output:**
```
[DISCORD] Joined server <<YourServerName>> (#1234567890123456789).
```

### Test the command in Discord:

In your Discord server, try:
```
/aura command:pub target:TestGame
```

Or use the shortcut:
```
/host map:twrpg title:TestGame
```

---

## Permissions Explained

### `discord.commands.custom_host.permissions`:

- **`verified`** - Anyone who can verify in Discord
- **`admin`** - Server admins only
- **`sudoer`** - Bot sudoers only (set in `discord.sudo_users`)
- **`everyone`** - Anyone can use (not recommended)

---

## Finding Your IDs

### Discord Server ID:
- Right-click server name → Copy Server ID

### Discord User ID:
- Right-click your username → Copy User ID

### Bot Application ID:
- Discord Developer Portal → Applications → Your App → General → Application ID

---

## Troubleshooting

### "Command not appearing in Discord"
- Make sure bot is in your server
- MESSAGE_CONTENT INTENT is enabled
- Reload Discord (`Ctrl+R`)

### "Bot not responding"
- Check `discord.enabled = yes` in config
- Check token is correct
- Verify bot token hasn't been regenerated
- Check Discord logs in container

### "Permission denied"
- Set `discord.commands.custom_host.permissions = admin`
- Add your User ID to `discord.sudo_users`
- Make sure you're admin in the Discord server

### "Bot left the server"
- Usually because server ID is in `deny_list`
- Check `discord.invites.mode` and `discord.invites.list`

---

## Quick Reference

| Config Key | What to Put | Example |
|-----------|-----------|---------|
| `discord.enabled` | yes/no | yes |
| `discord.token` | Bot token from Discord | MTk4NjIy... |
| `discord.invites.list` | Your server ID | 1234567890123456789 |
| `discord.sudo_users` | Your user ID | 9876543210987654321 |
| `discord.commands.namespace` | Command prefix | aura |
| `discord.commands.custom_host.permissions` | Permission level | admin |

---

## Next Steps

After setting up:
1. Restart your bot container
2. Verify it connects to Discord (check logs)
3. Try `/aura` or `/host` command in your server
4. Game should host directly from Discord!

If you have your bot token already, start at **Step 4**.
