# Discord Integration - Quick Reference

## Do I Need a New Token?

**NO** - Reuse your existing bot token if you have one.
- Bot tokens don't expire
- One token = One bot application
- Keep it secret!

---

## File to Edit

**Location:** `config-example.ini` (Lines 930-980)

**On VPS:** Copy to container → `docker cp config-example.ini aura-running:/app/config-example.ini`

---

## Essential Config Changes

```ini
# 1. ENABLE DISCORD
discord.enabled = yes

# 2. ADD YOUR BOT TOKEN
discord.token = YOUR_BOT_TOKEN_HERE

# 3. ALLOW YOUR SERVER
discord.invites.mode = allow_list
discord.invites.list = YOUR_SERVER_ID

# 4. SET PERMISSIONS
discord.commands.custom_host.permissions = admin
discord.sudo_users = YOUR_USER_ID
```

---

## Quick Setup Checklist

- [ ] Step 1: Get/Create Discord Bot Token
  - Go to: https://discord.com/developers/applications
  - Create new app → Add Bot → Copy token
  
- [ ] Step 2: Enable Scopes & Intents
  - Scopes: `bot` + `applications.commands`
  - Intents: Enable "MESSAGE CONTENT INTENT"
  
- [ ] Step 3: Invite Bot to Server
  - Use OAuth2 URL from Developer Portal
  
- [ ] Step 4: Get IDs
  - Server ID: Right-click server → Copy ID
  - User ID: Right-click yourself → Copy ID
  
- [ ] Step 5: Edit config-example.ini
  - Enable Discord
  - Add bot token
  - Add server ID
  - Add user ID
  
- [ ] Step 6: Deploy to VPS
  - `docker cp config-example.ini aura-running:/app/config-example.ini`
  - `docker restart aura-running`
  
- [ ] Step 7: Test
  - `/aura` or `/host` in Discord
  - Check logs: `docker logs aura-running | grep discord`

---

## Discord Commands

### `/host MAP TITLE`
```
/host map:twrpg title:MyGame
```
Hosts a game directly from Discord!

### `/aura COMMAND PARAMS`
```
/aura command:pub target:TestGame
```
Run any Aura command from Discord

---

## Where to Get Each Value

| Value | Where to Find |
|-------|---------------|
| Bot Token | Discord Dev Portal → Bot → Token |
| Server ID | Right-click server name → Copy ID |
| User ID | Right-click your name → Copy ID |
| Application ID | Discord Dev Portal → General → Application ID |

---

## Test Command

```bash
docker logs aura-running | grep -i discord
```

Should show:
```
[DISCORD] Joined server <<YourServer>> (#123456...)
```

---

## File Locations

**Local:** `c:\Users\Admin\Desktop\Git\aura-bot\config-example.ini`

**VPS:** `/app/config-example.ini` (inside container)

**Push to VPS:**
```bash
docker cp config-example.ini aura-running:/app/config-example.ini
docker restart aura-running
```

---

## Support Resources

- Full Guide: `DISCORD_SETUP_GUIDE.md`
- Host Command Details: `DISCORD_HOST_GAME_COMMAND.md`
- Discord Dev Portal: https://discord.com/developers/applications
- DPP (Discord++ Library): https://dpp.dev/

---

**TL;DR:** 
1. Get bot token from Discord
2. Edit config-example.ini with token + IDs
3. Copy to VPS container
4. Restart
5. Use `/host` command!
