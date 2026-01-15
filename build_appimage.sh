#!/bin/bash

# Configuration
PROJECT_ROOT=$(pwd)
BUILD_DIR="${PROJECT_ROOT}/desktopApp/build/compose/binaries/main-release/app/LoungeCat"
APP_IMAGE_TOOL="${HOME}/.local/bin/appimagetool"
ICON_SOURCE="${PROJECT_ROOT}/shared/src/commonMain/composeResources/drawable/logo.png"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}Starting manual AppImage build process...${NC}"

# Check for appimagetool
if [ ! -f "$APP_IMAGE_TOOL" ]; then
    echo "appimagetool not found at $APP_IMAGE_TOOL"
    echo "Downloading appimagetool..."
    mkdir -p "${HOME}/.local/bin"
    wget "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" -O "$APP_IMAGE_TOOL"
    chmod +x "$APP_IMAGE_TOOL"
    echo -e "${GREEN}Installed appimagetool.${NC}"
fi

# Run Gradle task
# Run Gradle tasks
echo "Running Gradle packageReleaseAppImage and packageReleaseDeb..."
./gradlew :desktopApp:packageReleaseAppImage :desktopApp:packageReleaseDeb

# Copy DEB file
DEB_DIR="${PROJECT_ROOT}/desktopApp/build/compose/binaries/main-release/deb"
echo "Looking for DEB file in $DEB_DIR..."
if ls "$DEB_DIR"/*.deb 1> /dev/null 2>&1; then
    cp "$DEB_DIR"/*.deb "${PROJECT_ROOT}/"
    echo -e "${GREEN}Copied DEB file to project root.${NC}"
else
    echo -e "${RED}Error: DEB file not found in $DEB_DIR${NC}"
fi

# AppImage Handling
# Gradle might fail at the packing step, or we prefer manual packing. 
# We use the output 'app' directory from packageReleaseAppImage.
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}Error: Build directory not found: $BUILD_DIR${NC}"
    echo "Gradle build failed to produce the output directory."
    exit 1
fi

echo -e "${GREEN}Gradle tasks finished.${NC}"

# Prepare AppDir
echo "Configuring AppDir..."

# Link AppRun
rm -f "$BUILD_DIR/AppRun"
ln -s "bin/LoungeCat" "$BUILD_DIR/AppRun"

# Copy Icon
# Using logo.png as the main icon
echo "Copying icon from $ICON_SOURCE"
if [ -f "$ICON_SOURCE" ]; then
    cp "$ICON_SOURCE" "$BUILD_DIR/LoungeCat.png"
else
    echo -e "${RED}Warning: Icon source not found at $ICON_SOURCE${NC}"
fi

# Link .DirIcon for AppImage
ln -s "LoungeCat.png" "$BUILD_DIR/.DirIcon"

# Create Desktop File
echo "Creating .desktop file..."
cat > "$BUILD_DIR/LoungeCat.desktop" << EOF
[Desktop Entry]
Name=LoungeCat
Exec=LoungeCat
Icon=LoungeCat
Type=Application
Categories=Network;
Comment=Modern IRC Client for Linux
Terminal=false
StartupWMClass=LoungeCat
EOF

# Build AppImage
echo "Packaging AppImage..."
# Run appimagetool in the project root so the output file is saved there
cd "${PROJECT_ROOT}"
"$APP_IMAGE_TOOL" "$BUILD_DIR"

echo -e "${GREEN}Build Complete! AppImage and DEB files should be in the root directory.${NC}"
