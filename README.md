# Aura — TWRPG Fork

[![Build & Release](https://github.com/zeebratootie/aura-bot/actions/workflows/build.yml/badge.svg)](https://github.com/zeebratootie/aura-bot/actions/workflows/build.yml)

A fork of [ivojulca/aura-bot](https://gitlab.com/ivojulca/aura-bot) tuned for hosting
**The World RPG (TWRPG)** on [EuroBattle.net](https://eurobattle.net). Adds:

- `trust_admins` patch — root admins and moderators are spoofcheck-verified at join; no `!sc` whisper needed
- TWRPG `config-example.ini` defaults (realm_13 EuroBattle.net, WC3 1.28, port 6115)
- GitHub Actions CI that builds the binary and publishes a release tarball automatically

---

## Quick VPS install (~2 min with pre-built binary)

```bash
# 1. Download the setup script from the gist
curl -fsSL https://gist.githubusercontent.com/zeebratootie/3e6e557456620d2407b4872d63fa2bfe/raw/download-gist.sh | bash

# 2. Run it — AURA_RELEASE_URL skips compilation entirely
AURA_RELEASE_URL='https://github.com/zeebratootie/aura-bot/releases/download/latest/aura-linux-x64.tar.gz' \
  ./setup-vps-hostbot.sh 'crucibles,[dreamer]' botuser 'password' BotName

# Parameters: <admins> <bnet_username> <bnet_password> <bot_display_name> [map_url]
```

The setup script handles everything: deps, map download, WC3 files, config.ini, cron watchdog, and bash aliases (`startbot`, `stopbot`, `botlog`).

### Compile from source (~25 min)

```bash
curl -fsSL https://gist.githubusercontent.com/zeebratootie/3e6e557456620d2407b4872d63fa2bfe/raw/download-gist.sh | bash
./setup-vps-hostbot.sh 'crucibles,[dreamer]' botuser 'password' BotName
```

See [BUILDING.md](BUILDING.md) for manual build instructions.

---

## Pre-built releases

GitHub Actions builds `aura-linux-x64.tar.gz` on every push to master and on every `v*` tag:

| Release | URL |
|---------|-----|
| Latest master | `https://github.com/zeebratootie/aura-bot/releases/download/latest/aura-linux-x64.tar.gz` |
| Versioned | `https://github.com/zeebratootie/aura-bot/releases/download/vX.Y.Z/aura-linux-x64.tar.gz` |

The tarball contains: `aura` (the binary), `libstorm.so`, `libbncsutil.so`.

To tag a new versioned release:

```bash
git tag v1.0.0 && git push github v1.0.0
```

---

## Managing admins

```bash
# Add root admins (auto-restarts the bot)
./add-rootadmin.sh newguy [dreamer]

# Remove
./add-rootadmin.sh --remove oldguy

# Also add to sudo_users
./add-rootadmin.sh --sudo poweruser

# Show current lists
./add-rootadmin.sh --list
```

Script lives in [twrpg-gist](https://gist.github.com/zeebratootie/3e6e557456620d2407b4872d63fa2bfe).

---

## Spoofcheck / trust_admins

By default, Aura only grants admin powers after a user whispers `sc` to the bot to verify
their account. This fork adds `realm_N.unverified_users.trust_admins = yes`: anyone joining
from the realm under a name listed in `realm_13.admins` or the moderator DB is marked
verified at join automatically.

The setup script enables this by default. To disable:

```ini
realm_13.unverified_users.trust_admins = no
```

See [TWRPG_BOT_GUIDE.md](TWRPG_BOT_GUIDE.md) for the full configuration reference.

---

## Day-to-day operation

```bash
startbot    # start the bot
stopbot     # stop the bot
botlog      # tail the log
fa          # show the aura process

# Update map or config — re-run setup, auto-restarts bot if running
./setup-vps-hostbot.sh 'admins' user pass Name '<new_map_url>'

# Force recompile
FORCE_REBUILD=1 ./setup-vps-hostbot.sh ...
```

In battle.net: `/w BotName !load twrpg` → `!host` → players join → `!start`

---

## Documentation

| File | Contents |
|------|----------|
| [TWRPG_BOT_GUIDE.md](TWRPG_BOT_GUIDE.md) | Full TWRPG setup, admin tiers, spoofcheck config |
| [CONFIG.md](CONFIG.md) | All config.ini keys |
| [COMMANDS.md](COMMANDS.md) | All bot commands |
| [CLI.md](CLI.md) | Command-line interface reference |
| [BUILDING.md](BUILDING.md) | Manual build instructions |
| [NETWORKING.md](NETWORKING.md) | Network / NAT / GProxy guide |
| [FEATURES.md](FEATURES.md) | Feature overview |

---

## License

This fork is MIT licensed, same as the upstream project.
Original work copyright [2024-2025] Leonardo Julca.
See [LICENSE](LICENSE) for the full text and third-party library licenses.

Upstream: https://gitlab.com/ivojulca/aura-bot
