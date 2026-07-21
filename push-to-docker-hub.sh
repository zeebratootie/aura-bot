#!/bin/bash
# Docker Hub Push Script for Aura Bot

echo "=========================================="
echo "Aura Bot - Docker Hub Push Script"
echo "=========================================="
echo ""

# Get Docker Hub username
read -p "Enter your Docker Hub username: " DOCKER_USERNAME

# Verify image exists
echo "[1/5] Checking if image exists..."
if ! docker images | grep -q "aura-bot"; then
    echo "❌ Image 'aura-bot' not found!"
    echo "Build it first with: docker build -t aura-bot:latest ."
    exit 1
fi
echo "✓ Image found"

# Login to Docker Hub
echo ""
echo "[2/5] Logging in to Docker Hub..."
docker login

# Tag the image
echo ""
echo "[3/5] Tagging image..."
docker tag aura-bot:latest $DOCKER_USERNAME/aura-bot:latest
docker tag aura-bot:latest $DOCKER_USERNAME/aura-bot:1.0
echo "✓ Tagged as:"
echo "  - $DOCKER_USERNAME/aura-bot:latest"
echo "  - $DOCKER_USERNAME/aura-bot:1.0"

# Push to Docker Hub
echo ""
echo "[4/5] Pushing to Docker Hub..."
echo "(This may take a few minutes...)"
docker push $DOCKER_USERNAME/aura-bot:latest
docker push $DOCKER_USERNAME/aura-bot:1.0

# Verify
echo ""
echo "[5/5] Push complete!"
echo ""
echo "✓ Image is now available at: https://hub.docker.com/r/$DOCKER_USERNAME/aura-bot"
echo ""
echo "=========================================="
echo "Next Steps:"
echo "=========================================="
echo "On your VPS, run:"
echo ""
echo "  docker pull $DOCKER_USERNAME/aura-bot:latest"
echo ""
echo "  docker run -d \\"
echo "    --restart unless-stopped \\"
echo "    --name aura-running \\"
echo "    -p 6112:6112 \\"
echo "    -p 6113:6113/tcp \\"
echo "    -p 6113:6113/udp \\"
echo "    -p 6115:6115 \\"
echo "    $DOCKER_USERNAME/aura-bot:latest"
echo ""
echo "=========================================="
