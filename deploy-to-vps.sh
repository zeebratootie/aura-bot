#!/bin/bash
# VPS Deployment Script for Aura Bot
# Run this on your VPS after pushing to Docker Hub

echo "=========================================="
echo "Aura Bot VPS Deployment"
echo "=========================================="
echo ""

# Step 1: Pull the image
echo "[1/3] Pulling image from Docker Hub..."
docker pull hayahaydev/aura-bot:latest

if [ $? -ne 0 ]; then
    echo "❌ Failed to pull image"
    exit 1
fi
echo "✓ Image pulled successfully"

# Step 2: Stop old container if running
echo ""
echo "[2/3] Stopping old container (if exists)..."
docker rm -f aura-running 2>/dev/null
echo "✓ Ready for new container"

# Step 3: Run new container
echo ""
echo "[3/3] Starting Aura Bot container..."
docker run -d \
  --restart unless-stopped \
  --name aura-running \
  -p 6112:6112 \
  -p 6113:6113/tcp \
  -p 6113:6113/udp \
  -p 6115:6115 \
  hayahaydev/aura-bot:latest

if [ $? -eq 0 ]; then
    echo "✓ Container started successfully!"
    echo ""
    sleep 3
    echo "Container Status:"
    docker ps | grep aura-running
    echo ""
    echo "Latest Logs:"
    docker logs aura-running | tail -10
else
    echo "❌ Failed to start container"
    exit 1
fi

echo ""
echo "=========================================="
echo "✓ Deployment Complete!"
echo "=========================================="
echo ""
echo "Your Aura Bot is now running on this VPS!"
echo ""
echo "Next Steps:"
echo "1. Copy your config file to the container:"
echo "   docker cp config-example.ini aura-running:/app/config-example.ini"
echo ""
echo "2. Restart the container:"
echo "   docker restart aura-running"
echo ""
echo "3. Check logs:"
echo "   docker logs -f aura-running"
echo ""
echo "4. Get your VPS public IP:"
echo "   curl ifconfig.me"
echo ""
