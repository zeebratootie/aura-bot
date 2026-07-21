# Docker Hub Push Script for Aura Bot (Windows PowerShell)

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "Aura Bot - Docker Hub Push Script" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host ""

# Get Docker Hub username
$DOCKER_USERNAME = Read-Host "Enter your Docker Hub username"

# Verify image exists
Write-Host "[1/5] Checking if image exists..." -ForegroundColor Yellow
$imageExists = docker images | Select-String "aura-bot"
if (-not $imageExists) {
    Write-Host "❌ Image 'aura-bot' not found!" -ForegroundColor Red
    Write-Host "Build it first with: docker build -t aura-bot:latest ." -ForegroundColor Red
    exit 1
}
Write-Host "✓ Image found" -ForegroundColor Green

# Login to Docker Hub
Write-Host ""
Write-Host "[2/5] Logging in to Docker Hub..." -ForegroundColor Yellow
docker login

# Tag the image
Write-Host ""
Write-Host "[3/5] Tagging image..." -ForegroundColor Yellow
docker tag aura-bot:latest "$DOCKER_USERNAME/aura-bot:latest"
docker tag aura-bot:latest "$DOCKER_USERNAME/aura-bot:1.0"
Write-Host "✓ Tagged as:" -ForegroundColor Green
Write-Host "  - $DOCKER_USERNAME/aura-bot:latest" -ForegroundColor Cyan
Write-Host "  - $DOCKER_USERNAME/aura-bot:1.0" -ForegroundColor Cyan

# Push to Docker Hub
Write-Host ""
Write-Host "[4/5] Pushing to Docker Hub..." -ForegroundColor Yellow
Write-Host "(This may take a few minutes...)" -ForegroundColor Gray
docker push "$DOCKER_USERNAME/aura-bot:latest"
docker push "$DOCKER_USERNAME/aura-bot:1.0"

# Verify
Write-Host ""
Write-Host "[5/5] Push complete!" -ForegroundColor Green
Write-Host ""
Write-Host "✓ Image is now available at: https://hub.docker.com/r/$DOCKER_USERNAME/aura-bot" -ForegroundColor Green
Write-Host ""
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "Next Steps:" -ForegroundColor Yellow
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "On your VPS, run:" -ForegroundColor Cyan
Write-Host ""
Write-Host "  docker pull $DOCKER_USERNAME/aura-bot:latest" -ForegroundColor Gray
Write-Host ""
Write-Host "  docker run -d \" -ForegroundColor Gray
Write-Host "    --restart unless-stopped \" -ForegroundColor Gray
Write-Host "    --name aura-running \" -ForegroundColor Gray
Write-Host "    -p 6112:6112 \" -ForegroundColor Gray
Write-Host "    -p 6113:6113/tcp \" -ForegroundColor Gray
Write-Host "    -p 6113:6113/udp \" -ForegroundColor Gray
Write-Host "    -p 6115:6115 \" -ForegroundColor Gray
Write-Host "    $DOCKER_USERNAME/aura-bot:latest" -ForegroundColor Gray
Write-Host ""
Write-Host "=========================================" -ForegroundColor Cyan
