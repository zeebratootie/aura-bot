# VPS Config Fix for JASS Maps

Run these commands on your VPS to enable JASS maps:

## Quick Fix (One Command)

```bash
docker exec aura-running sed -i 's/maps\.jass\.enabled = never/maps.jass.enabled = always/' /app/config-example.ini && docker restart aura-running
```

## Or Step by Step:

```bash
# 1. Edit the config inside the container
docker exec aura-running sed -i 's/maps\.jass\.enabled = never/maps.jass.enabled = always/' /app/config-example.ini

# 2. Restart the container to apply changes
docker restart aura-running

# 3. Verify it's working by checking logs
docker logs aura-running | tail -20
```

## What This Does:

- Changes `maps.jass.enabled = never` to `maps.jass.enabled = always`
- Restarts the container to apply the change
- Allows JASS maps like TWRPG to load

## If You Need to Copy the Full Config from Local:

### From your LOCAL machine:

```bash
# First, check your VPS IP and credentials
# Then copy the config:

scp config-example.ini root@VPS_IP:/tmp/

# Then SSH into VPS and run:
docker cp /tmp/config-example.ini aura-running:/app/config-example.ini
docker restart aura-running
```

Replace `VPS_IP` with your actual VPS IP address.

## Verify It Works:

```bash
# Check if the change was applied:
docker exec aura-running grep "maps.jass.enabled" /app/config-example.ini

# Should output: maps.jass.enabled = always
```

After running the command, try loading the TWRPG map again with:
```
.load twrpg
```

It should work now! ✓
