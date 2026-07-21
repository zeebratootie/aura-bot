# TWRPG Aura-Bot — Setup, Features & Configuration Guide

How the whole TWRPG host-bot pipeline fits together, what the bot can do, and how to
configure the parts you actually touch: spoofcheck (`sc`), admin tiers, and the helper
scripts in `twrpg-gist/`.

---

## 1. How the whole setup works

Three repos work together:

| Repo | Role |
|------|------|
| `aura-bot/` | The bot itself (C++ source, forked from ivojulca/aura-bot). Compiled on the VPS or shipped as a pre-built tarball. |
| `twrpg-gist/` | Automation scripts published as a GitHub Gist. A fresh Linux VPS becomes a 24/7 TWRPG host bot by running one script. |
| `twrpg-hostbot-guide/` | A styled single-page how-to (GitHub Pages) that walks a non-technical user through renting a VPS and running the gist scripts. |

### The automation flow

1. **`download-gist.sh`** — bootstrap. Pulls the *latest* `setup-vps-hostbot.sh` from the
   gist (`gist.githubusercontent.com/zeebratootie/...`) with a `?cachebust=$(date +%s)`
   query param so GitHub's raw CDN never serves a stale copy. Falls back from `curl` to
   `wget` automatically.

2. **`setup-vps-hostbot.sh <admins> <bot_username> <bot_password> <bot_name> [map_url] [map_file]`**
   — the whole install, idempotent (safe to re-run for map/config updates):
   - Installs runtime deps (and build deps only when compiling).
   - Clones aura-bot to `/root/aura-bot`, creates `maps/` and `logs/`.
   - Writes `/root/restart_aura.sh` + a `*/1` crontab entry → the bot auto-restarts
     within a minute if it ever dies.
   - Installs `.bashrc` aliases: `startbot`, `stopbot`, `botlog`, `fa` (find process),
     `ea`/`sa` (edit/source bashrc).
   - Writes the TWRPG map config `mapcfgs/twre.ini` (12 slots, 2 teams) and
     `aliases.ini` (`twrpg = twre.ini`) so `!load twrpg` works.
   - Downloads the map (URL arg) and the WC3 1.28 game files (gdown from Google Drive).
   - Builds `config.ini` from `config-example.ini` via ~50 `sed` edits: EuroBattle.net is
     `realm_13` (only enabled realm), game version 1.28, `!` command trigger, port 6115,
     GProxy reconnect, IPv4-only, and the admin lists (see §3).
   - Compiles StormLib → BNCSUtil → aura (lite build: `AURABUILD_CPR=0 DPP=0 MINIUPNP=0`,
     ~25-35 min, auto-adds swap on low-RAM VPSes) — **or** skips all compilation when
     `AURA_RELEASE_URL` points at a pre-built tarball (~2 min install).
   - If the bot is already running, kills and restarts it so a re-run applies the new
     map/config immediately.

3. **The bot runs as**: `./aura --homedir /root/aura-bot --exec "load twrpg" --exec-as "<first-admin>@server.eurobattle.net"`
   — the first IGN in the admins list is the "owner" the startup commands execute as.

### The guide site

`twrpg-hostbot-guide/index.html` is the human-facing version of the above: a dark
fantasy-styled landing page (Cinzel/Manrope/JetBrains Mono, gold-on-obsidian) hosted on
GitHub Pages that tells players exactly which two commands to paste into a fresh VPS.
The site never goes stale because it points at `download-gist.sh`, which always fetches
the newest setup script from the gist.

---

## 2. Feature highlights

- Hosts LAN + PvPGN realms (EuroBattle.net) simultaneously; up to 12-player lobbies.
- Map aliasing (`!load twrpg`), map caching, HCL/game modes.
- Spoofcheck / account verification (§4), admin & sudo permission tiers (§3).
- GProxy legacy reconnect support (players can survive disconnects).
- Auto-restart via cron watchdog; logs to `logs/aura_out.log` (`botlog` alias).
- Optional Discord (DPP) and web (CPR) integrations — disabled in the lite VPS build.

Key in-game/chat commands (trigger `!`, full list in `COMMANDS.md`):
`!host`, `!load twrpg`, `!start`, `!kick`, `!swap`, `!staff <name>` (grant moderator),
`!checkme` / `!check <player>` (shows Owner / Admin / Root Admin status), `!sc`.

---

## 3. Admin tiers — who can do what

From lowest to highest:

| Tier | Where it lives | How to grant |
|------|----------------|--------------|
| **Verified** | runtime (per game) | Player whispers `sc` to the bot, or auto `/whois` check passes |
| **Owner** | per-game | The player who created/owns the current lobby |
| **Moderator ("admin"/staff)** | SQLite DB (`aura.db`) | `!staff <name>` in chat — persists across restarts |
| **Root admin** | `config.ini` → `realm_13.admins` | Edit config + restart (use `add-rootadmin.sh`, §5) |
| **Sudo** | `config.ini` → `realm_13.sudo_users` | Edit config + restart; can run dangerous/host-level commands |

Notes:
- Root admins are matched **case-insensitively** against the comma-separated
  `realm_13.admins` list.
- By default aura only grants admin powers to **spoofcheck-verified** users. Our fork
  adds `realm_13.unverified_users.trust_admins = yes` (enabled by the setup script):
  players joining from the realm under an admin/moderator name are marked verified at
  join, so **admins and root admins never need to whisper `sc`**. Trade-off: anyone
  who joins claiming an admin's exact name gets those powers — acceptable on a
  private TWRPG bot, but set it to `no` if that ever becomes a concern.
- The setup script seeds both `sudo_users` and `admins` with the `<admins>` argument
  you passed it (e.g. `crucibles,[dreamer]`).
- Config is read at startup only → changing admins requires a bot restart.

---

## 4. Spoofcheck (`sc`) — what it is and how to configure it

### What it does

On PvPGN realms anyone can join a lobby claiming any name. Spoofcheck verifies a
joining player actually owns the battle.net account they're using:

1. **Automatic**: when hosting a public game the bot sends `/whois <player>` for each
   joiner; if the realm reports that account is really in this game, the player is
   marked verified.
2. **Manual**: the player whispers the bot `s`, `sc`, or `spoofcheck`
   (`/w <botname> sc`). Receiving a whisper proves account ownership, so the bot marks
   them verified. The in-game `!sc` command just prints instructions telling players
   to do this.

Verification is what unlocks command usage and admin/root-admin recognition (§3).

### The four config keys (per realm, `realm_13.*` — or `global_realm.*` for defaults)

All are booleans, **all default to `no`**:

```ini
# Actively /whois-check every joining player (strict spoofcheck)
realm_13.unverified_users.always_verify = no

# Unverified players can't use bot commands in the lobby/game
realm_13.unverified_users.reject_commands = no

# Unverified players can't start the game
realm_13.unverified_users.reject_start = no

# Kick unverified players from the lobby automatically
realm_13.unverified_users.auto_kick = no

# (Fork-only) Trust admins/moderators without spoofcheck — they are marked
# verified the moment they join from the realm, no "sc" whisper needed
realm_13.unverified_users.trust_admins = yes
```

### Common configurations

**Relaxed (current TWRPG default — nothing set):** anyone can join and play; players
only need to whisper `sc` if they want to use commands that require verification
(e.g. to be recognized as admin).

**Strict — require verification to interact:**
```bash
cd /root/aura-bot
cat >> config.ini << 'EOF'
realm_13.unverified_users.always_verify = yes
realm_13.unverified_users.reject_commands = yes
realm_13.unverified_users.reject_start = yes
EOF
stopbot && startbot
```

**Fortress — verified players only in the lobby:** add
`realm_13.unverified_users.auto_kick = yes` as well.

### Disabling spoofcheck enforcement

Since every key defaults to `no`, "disabling sc" just means removing/setting these
keys to `no`:

```bash
cd /root/aura-bot
sed -i 's/^realm_13.unverified_users.always_verify =.*/realm_13.unverified_users.always_verify = no/'   config.ini
sed -i 's/^realm_13.unverified_users.reject_commands =.*/realm_13.unverified_users.reject_commands = no/' config.ini
sed -i 's/^realm_13.unverified_users.reject_start =.*/realm_13.unverified_users.reject_start = no/'     config.ini
sed -i 's/^realm_13.unverified_users.auto_kick =.*/realm_13.unverified_users.auto_kick = no/'           config.ini
stopbot && startbot
```

You cannot disable the *manual* whisper verification (`/w bot sc`) — it's harmless and
is how admins prove their identity, so you never want to.

---

## 5. Managing root admins with `add-rootadmin.sh`

`twrpg-gist/add-rootadmin.sh` edits `realm_13.admins` (and optionally
`realm_13.sudo_users`) in `/root/aura-bot/config.ini`, dedupes case-insensitively, and
restarts the bot so the change takes effect.

```bash
# Add one or more root admins
./add-rootadmin.sh crucibles [dreamer]

# Add as root admin AND sudo user
./add-rootadmin.sh --sudo newguy

# Remove a root admin
./add-rootadmin.sh --remove oldguy

# Show current lists, change nothing
./add-rootadmin.sh --list

# Edit config only, don't restart the bot
./add-rootadmin.sh --no-restart someone
```

Options: `--sudo` (also touch `sudo_users`), `--remove`, `--list`, `--no-restart`,
`--realm N` (default 13), `--config PATH` (default `/root/aura-bot/config.ini`).

With `trust_admins = yes` (the default in our setup) the new admin's powers work
immediately — no `sc` whisper needed. If `trust_admins` is off, they must whisper
`sc` to the bot once per session first (§4).

---

## 6. Quick operational reference

```bash
startbot        # start the bot (alias)
stopbot         # kill the bot
botlog          # tail -f the log
fa              # show the aura process
./setup-vps-hostbot.sh <admins> <user> <pass> <name> '<new_map_url>'   # update map/config
FORCE_REBUILD=1 ./setup-vps-hostbot.sh ...                              # force recompile
AURA_RELEASE_URL='<tarball-url>' ./setup-vps-hostbot.sh ...             # prebuilt, ~2 min
```

In game / battle.net chat: `/w <botname> !load twrpg` → `!host` → players join →
`!start`. Whisper `sc` to the bot to verify.
