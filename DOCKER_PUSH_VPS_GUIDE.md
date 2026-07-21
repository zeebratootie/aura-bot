# Docker Image Push and VPS Deployment Guide

## Option 1: Push to Docker Hub (Easiest)

### Step 1: Create Docker Hub Account
1. Go to https://hub.docker.com/
2. Sign up (free account)
3. Remember your username

### Step 2: Login to Docker Hub

```bash
docker login
# Enter your Docker Hub username and password
```

### Step 3: Tag Your Image

Replace `YOUR_USERNAME` with your actual Docker Hub username:

```bash
docker tag aura-bot:latest YOUR_USERNAME/aura-bot:latest
docker tag aura-bot:latest YOUR_USERNAME/aura-bot:1.0
```

### Step 4: Push to Docker Hub

```bash
docker push YOUR_USERNAME/aura-bot:latest
docker push YOUR_USERNAME/aura-bot:1.0
```

Wait for upload to complete (may take a few minutes depending on internet speed)

### Step 5: Verify on Docker Hub

Visit: https://hub.docker.com/r/YOUR_USERNAME/aura-bot

You should see your image listed there!

---

## Option 2: Push to GitHub Container Registry (GitHub Free)

### Step 1: Create GitHub Personal Access Token

1. Go to https://github.com/settings/tokens
2. Click "Generate new token" → "Generate new token (classic)"
3. Give it these scopes:
   - ✓ `write:packages`
   - ✓ `read:packages`
   - ✓ `delete:packages`
4. Generate and copy the token (you'll need it once)

### Step 2: Login to GitHub Container Registry

```bash
echo YOUR_TOKEN | docker login ghcr.io -u YOUR_GITHUB_USERNAME --password-stdin
```

Replace:
- `YOUR_TOKEN` with your personal access token
- `YOUR_GITHUB_USERNAME` with your GitHub username

### Step 3: Tag Your Image

```bash
docker tag aura-bot:latest ghcr.io/YOUR_GITHUB_USERNAME/aura-bot:latest
docker tag aura-bot:latest ghcr.io/YOUR_GITHUB_USERNAME/aura-bot:1.0
```

### Step 4: Push to GitHub Container Registry

```bash
docker push ghcr.io/YOUR_GITHUB_USERNAME/aura-bot:latest
docker push ghcr.io/YOUR_GITHUB_USERNAME/aura-bot:1.0
```

### Step 5: Verify

Visit: https://github.com/YOUR_GITHUB_USERNAME?tab=packages

---

## Deploy on VPS (using Docker Hub or GHCR)

### SSH into Your VPS

```bash
ssh root@YOUR_VPS_IP
# or
ssh user@YOUR_VPS_IP
```

### Option A: If Using Docker Hub

```bash
# Pull the image
docker pull YOUR_USERNAME/aura-bot:latest

# Run it
docker run -d \
  --name aura-running \
  -p 6112:6112 \
  -p 6113:6113/tcp \
  -p 6113:6113/udp \
  -p 6115:6115 \
  YOUR_USERNAME/aura-bot:latest

# Check if it's running
docker ps
docker logs aura-running
```

### Option B: If Using GitHub Container Registry

```bash
# Login (if private repo)
echo YOUR_TOKEN | docker login ghcr.io -u YOUR_GITHUB_USERNAME --password-stdin

# Pull the image
docker pull ghcr.io/YOUR_GITHUB_USERNAME/aura-bot:latest

# Run it
docker run -d \
  --name aura-running \
  -p 6112:6112 \
  -p 6113:6113/tcp \
  -p 6113:6113/udp \
  -p 6115:6115 \
  ghcr.io/YOUR_GITHUB_USERNAME/aura-bot:latest

# Check if it's running
docker ps
docker logs aura-running
```

### Copy Config to VPS (if needed)

```bash
# From your local machine:
scp /c/Users/Admin/Desktop/Git/aura-bot/config-example.ini root@YOUR_VPS_IP:/tmp/

# Then on VPS:
ssh root@YOUR_VPS_IP
docker cp /tmp/config-example.ini aura-running:/app/config-example.ini
docker restart aura-running
```

---

## Quick Commands

### Docker Hub Quick Push
```bash
# Login
docker login

# Tag and push (replace YOUR_USERNAME)
docker tag aura-bot:latest YOUR_USERNAME/aura-bot:latest
docker push YOUR_USERNAME/aura-bot:latest

# On VPS:
docker pull YOUR_USERNAME/aura-bot:latest
docker run -d --name aura-running -p 6112:6112 -p 6113:6113/tcp -p 6113:6113/udp -p 6115:6115 YOUR_USERNAME/aura-bot:latest
```

---

## Troubleshooting

### "denied: requested access is denied"
- Make sure you're logged in: `docker login`
- Make sure image tag matches your username exactly

### "Failed to fetch image"
- Check image was pushed: `docker images`
- Verify tag spelling
- Check internet connection on VPS

### "Container won't start"
- Check logs: `docker logs aura-running`
- Make sure ports are available: `sudo netstat -tlnp | grep 611`
- Ensure config file is present

### VPS Has No Docker
```bash
# Install Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
```

---

## Check VPS Public IP

On VPS:
```bash
curl ifconfig.me
# or
hostname -I
```

Players can join using: `VPS_IP_ADDRESS` in Warcraft III

---

## Keep Container Running on VPS Restart

Add restart policy:

```bash
docker run -d \
  --restart unless-stopped \
  --name aura-running \
  -p 6112:6112 \
  -p 6113:6113/tcp \
  -p 6113:6113/udp \
  -p 6115:6115 \
  YOUR_USERNAME/aura-bot:latest
```

The `--restart unless-stopped` flag ensures it auto-starts after VPS reboots.

---

## Questions?

Tell me:
1. Which registry do you prefer? (Docker Hub or GitHub?)
2. Do you have a VPS ready? (IP address and SSH access?)
3. What OS is your VPS? (Ubuntu, CentOS, etc.)
