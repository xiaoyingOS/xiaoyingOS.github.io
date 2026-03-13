#!/bin/bash

# Stop HTTP/HTTPS Servers Script
# Supports: Linux, macOS, Android/Termux, Windows (Git Bash/WSL)

# Define colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}     Stop HTTP/HTTPS Servers${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Function to stop server on a specific port
stop_server() {
    local PORT=$1
    local SERVER_NAME=$2
    
    echo -e "${YELLOW}[Check] Checking for ${SERVER_NAME} server on port ${PORT}...${NC}"
    echo ""
    
    local PID=""
    
    # Try multiple methods to find the process
    if [ -z "$PID" ] && command -v pgrep &> /dev/null; then
        PID=$(pgrep -f "node.*server.js" 2>/dev/null | head -1)
    fi
    
    if [ -z "$PID" ]; then
        PID=$(pgrep -f "node.*server-https.js" 2>/dev/null | head -1)
    fi
    
    if [ -z "$PID" ] && command -v netstat &> /dev/null; then
        PID=$(netstat -ano 2>/dev/null | grep ":${PORT}.*LISTENING" | awk '{print $5}')
    fi
    
    if [ -z "$PID" ] && command -v ss &> /dev/null; then
        PID=$(ss -tlnp 2>/dev/null | grep ":${PORT}" | grep node | awk '{print $7}' | sed 's/.*pid=\([0-9]*\).*/\1/')
    fi
    
    if [ -z "$PID" ] && command -v lsof &> /dev/null; then
        PID=$(lsof -ti:${PORT} 2>/dev/null)
    fi
    
    if [ -z "$PID" ]; then
        echo -e "${YELLOW}[Info] No ${SERVER_NAME} server found running on port ${PORT}${NC}"
        echo ""
        return 1
    fi
    
    echo -e "${GREEN}[Found] ${SERVER_NAME} server found (PID: $PID)${NC}"
    echo -e "${YELLOW}[Stop] Stopping ${SERVER_NAME} server...${NC}"
    
    # Stop the process
    if command -v taskkill &> /dev/null; then
        # Windows: use taskkill
        taskkill //F //PID $PID > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}[Success] ${SERVER_NAME} server stopped successfully (PID: $PID)${NC}"
        else
            echo -e "${RED}[Error] Failed to stop ${SERVER_NAME} server (PID: $PID)${NC}"
        fi
    else
        # Unix: use kill
        kill $PID 2>/dev/null
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}[Success] ${SERVER_NAME} server stopped successfully (PID: $PID)${NC}"
        else
            # Try force kill
            kill -9 $PID 2>/dev/null
            if [ $? -eq 0 ]; then
                echo -e "${GREEN}[Success] ${SERVER_NAME} server stopped successfully (PID: $PID)${NC}"
            else
                echo -e "${RED}[Error] Failed to stop ${SERVER_NAME} server (PID: $PID)${NC}"
            fi
        fi
    fi
    
    echo ""
    return 0
}

# Stop HTTP server (port 8084)
HTTP_STOPPED=0
stop_server 8084 "HTTP"
if [ $? -eq 0 ]; then
    HTTP_STOPPED=1
fi

# Stop HTTPS server (port 8443)
HTTPS_STOPPED=0
stop_server 8443 "HTTPS"
if [ $? -eq 0 ]; then
    HTTPS_STOPPED=1
fi

# Summary
echo -e "${BLUE}========================================${NC}"
if [ $HTTP_STOPPED -eq 0 ] && [ $HTTPS_STOPPED -eq 0 ]; then
    echo -e "${YELLOW}[Info] No servers were running${NC}"
else
    echo -e "${GREEN}[Summary] Server stop process completed${NC}"
fi
echo -e "${BLUE}========================================${NC}"
echo ""
read -p "按 Enter 键退出..."