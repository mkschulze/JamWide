#!/bin/bash
#
# NINJAM CLAP Release Script
# Automatically generates commit summary, commits, builds, tags, and pushes to GitHub
#

set -e  # Exit on error

cd "$(dirname "$0")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Get current build number
BUILD_NUM=$(grep -o 'NINJAM_BUILD_NUMBER [0-9]*' src/build_number.h | awk '{print $2}')

echo -e "${YELLOW}=== NINJAM CLAP Release Script ===${NC}"
echo ""

# Function to generate commit message from changes
generate_commit_message() {
    local added_files=$(git diff --cached --name-only --diff-filter=A 2>/dev/null | wc -l | tr -d ' ')
    local modified_files=$(git diff --cached --name-only --diff-filter=M 2>/dev/null | wc -l | tr -d ' ')
    local deleted_files=$(git diff --cached --name-only --diff-filter=D 2>/dev/null | wc -l | tr -d ' ')
    
    # Get the list of changed files for context
    local changed_files=$(git diff --cached --name-only 2>/dev/null)
    
    # Analyze changes to generate meaningful summary
    local features=""
    local fixes=""
    local other=""
    
    # Check for new feature files
    if echo "$changed_files" | grep -q "ui_chat"; then
        features="${features}- Add chat functionality\n"
    fi
    if echo "$changed_files" | grep -q "ui_latency_guide\|timing"; then
        features="${features}- Timing guide improvements\n"
    fi
    if echo "$changed_files" | grep -q "server_browser\|server_list"; then
        features="${features}- Server browser updates\n"
    fi
    
    # Check for fixes
    if git diff --cached -U0 2>/dev/null | grep -qi "fix\|bug\|crash\|error"; then
        fixes="${fixes}- Bug fixes\n"
    fi
    if git diff --cached -U0 2>/dev/null | grep -qi "PushID\|PopID\|##"; then
        fixes="${fixes}- ImGui ID collision fixes\n"
    fi
    
    # Build the commit message
    local msg=""
    
    if [[ -n "$features" ]]; then
        msg="feat: "
    elif [[ -n "$fixes" ]]; then
        msg="fix: "
    else
        msg="chore: "
    fi
    
    # Create summary from file changes
    local summary=""
    for file in $changed_files; do
        case "$file" in
            *ui_chat*) summary="${summary}chat, " ;;
            *ui_latency*|*timing*) summary="${summary}timing guide, " ;;
            *run_thread*) summary="${summary}threading, " ;;
            *login*|*auth*|*anonymous*) summary="${summary}auth, " ;;
            *README*) summary="${summary}docs, " ;;
            *progress*|*activeContext*) summary="${summary}memory-bank, " ;;
        esac
    done
    
    # Remove trailing comma and space
    summary=$(echo "$summary" | sed 's/, $//')
    
    if [[ -z "$summary" ]]; then
        summary="update (${modified_files} modified, ${added_files} added)"
    fi
    
    echo "${msg}${summary}"
}

# Check for uncommitted changes
if [[ -n $(git status --porcelain) ]]; then
    echo -e "${YELLOW}Uncommitted changes detected:${NC}"
    git status --short
    echo ""
    
    # Stage all changes first for analysis
    git add -A
    
    # Generate automatic commit message
    AUTO_MSG=$(generate_commit_message)
    
    echo -e "${CYAN}Generated commit message:${NC}"
    echo -e "  ${AUTO_MSG}"
    echo ""
    read -p "Use this message? (y/n/edit): " USE_AUTO
    
    if [[ "$USE_AUTO" == "y" || "$USE_AUTO" == "Y" ]]; then
        COMMIT_MSG="$AUTO_MSG"
    elif [[ "$USE_AUTO" == "e" || "$USE_AUTO" == "edit" ]]; then
        read -p "Enter commit message: " COMMIT_MSG
        if [[ -z "$COMMIT_MSG" ]]; then
            COMMIT_MSG="$AUTO_MSG"
        fi
    else
        read -p "Enter commit message (or Ctrl+C to cancel): " COMMIT_MSG
        if [[ -z "$COMMIT_MSG" ]]; then
            echo -e "${RED}Error: Commit message required${NC}"
            exit 1
        fi
    fi
    
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
    git commit -m "build: bump to r${NEW_BUILD_NUM}"
    echo -e "${GREEN}✓ Build number committed${NC}"
fi

# Push to GitHub (automatic)
echo ""
echo -e "${YELLOW}Pushing to GitHub...${NC}"
git push origin main
echo -e "${GREEN}✓ Pushed to origin/main${NC}"

# Check if tag already exists
if git rev-parse "$TAG_NAME" >/dev/null 2>&1; then
    echo -e "${YELLOW}Tag $TAG_NAME already exists, skipping tag creation${NC}"
else
    read -p "Create release tag $TAG_NAME? (y/n): " CREATE_TAG
    if [[ "$CREATE_TAG" == "y" || "$CREATE_TAG" == "Y" ]]; then
        read -p "Enter tag suffix (e.g., 'chat', 'timing', or leave empty): " TAG_SUFFIX
        if [[ -n "$TAG_SUFFIX" ]]; then
            TAG_NAME="${TAG_NAME}-${TAG_SUFFIX}"
        fi
        git tag "$TAG_NAME"
        git push origin "$TAG_NAME"
        echo -e "${GREEN}✓ Tagged and pushed $TAG_NAME${NC}"
    fi
fi

echo ""
echo -e "${GREEN}=== Release Complete ===${NC}"
echo -e "Build: r${NEW_BUILD_NUM}"
echo -e "Installed to: ~/Library/Audio/Plug-Ins/CLAP/NINJAM.clap"
