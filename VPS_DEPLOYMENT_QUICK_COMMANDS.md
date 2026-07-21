# Quick VPS Deployment Commands

## One-Liner Deploy (Run this on your VPS)

```bash
docker pull hayahaydev/aura-bot:latest && docker rm -f aura-running && docker run -d --restart unless-stopped --name aura-running -p 6112:6112 -p 6113:6113/tcp -p 6113:6113/udp -p 6115:6115 hayahaydev/aura-bot:latest && sleep 3 && docker ps && echo "Deployed!" && docker logs aura-running | tail -20
```

## Step-by-Step (Run these commands one by one)

```bash
# 1. Pull the image
docker pull hayahaydev/aura-bot:latest

# 2. Remove old container
docker rm -f aura-running

# 3. Run new container
docker run -d \
  --restart unless-stopped \
  --name aura-running \
  -p 6112:6112 \
  -p 6113:6113/tcp \
  -p 6113:6113/udp \
  -p 6115:6115 \
  hayahaydev/aura-bot:latest

# 4. Check if it's running
docker ps

# 5. View logs
docker logs -f aura-running
```

## If Docker is not installed on VPS

```bash
# Install Docker on Ubuntu/Debian
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh

# Add your user to docker group (optional, for sudo-less commands)
sudo usermod -aG docker $USER
```

## Copy Config to VPS (from your local machine)

```bash
# Replace VPS_IP with your actual VPS IP
scp config-example.ini root@VPS_IP:/tmp/

# Then SSH into VPS and run:
docker cp /tmp/config-example.ini aura-running:/app/config-example.ini
docker restart aura-running
```

## Get VPS Public IP

```bash
# Run on your VPS:
curl ifconfig.me
# or
hostname -I
```

## Check Container Logs

```bash
# View last 50 lines
docker logs aura-running | tail -50

# Follow logs in real-time
docker logs -f aura-running

# Search for errors
docker logs aura-running | grep -i error
```

## Stop/Start Container

```bash
# Stop
docker stop aura-running

# Start
docker start aura-running

# Restart
docker restart aura-running

# Remove
docker rm -f aura-running
```

## Your Image Details

- **Repository**: `hayahaydev/aura-bot`
- **Tags**: `latest`, `1.0`
- **Size**: 5.08GB
- **URL**: https://hub.docker.com/r/hayahaydev/aura-bot

View on Docker Hub: https://hub.docker.com/r/hayahaydev/aura-bot
