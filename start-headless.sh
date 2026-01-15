#!/usr/bin/env bash

# Function to check if a command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check for Docker
if ! command_exists docker; then
    echo "Error: 'docker' command not found. Please install Docker."
    exit 1
fi

# Determine Compose command (plugin vs standalone)
if docker compose version >/dev/null 2>&1; then
    COMPOSE_CMD="docker compose"
elif command_exists docker-compose; then
    COMPOSE_CMD="docker-compose"
else
    echo "Error: Neither 'docker compose' nor 'docker-compose' found."
    exit 1
fi

# Stop any existing container
echo "Stopping previous instances..."
$COMPOSE_CMD -f docker/docker-compose.headless.yml down

echo "Building and starting Headless LoungeCat..."
echo "This may take a while as it builds the application and installs headless dependencies..."

$COMPOSE_CMD -f docker/docker-compose.headless.yml up --build -d

echo ""
echo "========================================================"
echo "LoungeCat Headless is running!"
echo "Access the desktop interface at: http://localhost:6080/vnc.html"
echo "========================================================"
echo ""
echo "To view logs: $COMPOSE_CMD -f docker/docker-compose.headless.yml logs -f"
echo "To stop: $COMPOSE_CMD -f docker/docker-compose.headless.yml down"
