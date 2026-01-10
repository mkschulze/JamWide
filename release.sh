#!/bin/bash
#
# NINJAM CLAP Release Script
# Builds, installs, commits, tags, and pushes to GitHub
#

set -e  # Exit on error

cd "$(dirname "$0")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get current build number
BUILD_NUM=$(grep -o 'NINJAM_BUILD_NUMBER [0-9]*' src/build_number.h | awk '{print $2}')

echo -e "${YELLOW}=== NINJAM CLAP Release Script ===${NC}"
echo ""

# Check for uncommitted changes
if [[ -n $(git status --porcelain) ]]; then
    echo -e "${YELLOW}Uncommitted changes detected:${NC}"
    git status --short
    echo ""
    read -p "Enter commit message (or Ctrl+C to cancel): " COMMIT_MSG
    if [[ -z "$COMMIT_MSG" ]]; then
        echo -e "${RED}Error: Commit message required${NC}"
        exit 1
    fi
    git add -A
    git commit -m "$COMMIT_MSG"
    echo -e "${GREEN}✓ Changes committed${NC}"
else
    echo -e "${GREEN}✓ Working directory clean${NC}"
fi

# Build and install
echo ""
echo -e "${YELLOW}Building...${NC}"
./install.sh

# Get new build number after install
NEW_BUILD_NUM=$(grep -o 'NINJAM_BUILD_NUMBER [0-9]*' src/build_number.h | awk '{print $2}')
TAG_NAME="v0.${NEW_BUILD_NUM}"

echo ""
echo -e "${GREEN}✓ Built and installed r${NEW_BUILD_NUM}${NC}"

# Check if build_number.h changed
if [[ -n $(git status --porcelain src/build_number.h) ]]; then
    git add src/build_number.h
    git commit -m "Bump build number to r${NEW_BUILD_NUM}"
    echo -e "${GREEN}✓ Build number committed${NC}"
fi

# Check if tag already exists
if git rev-parse "$TAG_NAME" >/dev/null 2>&1; then
    echo -e "${YELLOW}Tag $TAG_NAME already exists, skipping tag creation${NC}"
else
    read -p "Create release tag $TAG_NAME? (y/n): " CREATE_TAG
    if [[ "$CREATE_TAG" == "y" || "$CREATE_TAG" == "Y" ]]; then
        read -p "Enter release notes (optional): " RELEASE_NOTES
        if [[ -z "$RELEASE_NOTES" ]]; then
            RELEASE_NOTES="Release $TAG_NAME"
        fi
        git tag -a "$TAG_NAME" -m "$RELEASE_NOTES"
        echo -e "${GREEN}✓ Tagged $TAG_NAME${NC}"
    fi
fi

# Push to GitHub
echo ""
read -p "Push to GitHub? (y/n): " PUSH_CONFIRM
if [[ "$PUSH_CONFIRM" == "y" || "$PUSH_CONFIRM" == "Y" ]]; then
    git push origin main
    echo -e "${GREEN}✓ Pushed to origin/main${NC}"
    
    # Push tags if any new ones exist
    if git tag --points-at HEAD | grep -q .; then
        git push origin --tags
        echo -e "${GREEN}✓ Pushed tags${NC}"
    fi
fi

echo ""
echo -e "${GREEN}=== Release Complete ===${NC}"
echo -e "Build: r${NEW_BUILD_NUM}"
echo -e "Installed to: ~/Library/Audio/Plug-Ins/CLAP/NINJAM.clap"
