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

# Check for xhost (required for GUI access)
if command_exists xhost; then
    echo "Granting X11 access to local docker..."
    xhost +local:docker
else
    echo "Warning: 'xhost' not found. GUI might not work if running on X11."
fi

# Stop any existing container to avoid name conflicts
echo "Stopping previous instances..."
$COMPOSE_CMD -f docker/docker-compose.yml down

# Start Docker Compose using the file in the docker directory
echo "Starting LoungeCat..."
$COMPOSE_CMD -f docker/docker-compose.yml up --build

# Cleanup (optional, runs when you exit with Ctrl+C)
if command_exists xhost; then
    echo "Cleaning up X11 access..."
    xhost -local:docker
fi
