#!/bin/bash

# HTTPS Server Stop Script
# Supports: Linux, macOS, Android/Termux, Windows (Git Bash/WSL)

# Define colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}         Stop HTTPS Server${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

echo -e "${YELLOW}[Check] Checking for HTTPS server on port 8443...${NC}"
echo ""

# 优先使用pgrep查找node进程，然后检查是否运行server-https.js
PID=""
if command -v pgrep &> /dev/null; then
    PID=$(pgrep -f "node.*server-https.js" 2>/dev/null)
fi

# 如果pgrep未找到，尝试使用ps命令
if [ -z "$PID" ]; then
    PID=$(ps -ef 2>/dev/null | grep "node.*server-https.js" | grep -v grep | awk '{print $2}')
fi

# 如果还是没找到，尝试使用netstat（兼容性）
if [ -z "$PID" ] && command -v netstat &> /dev/null; then
    # Try Windows format first
    PID=$(netstat -ano 2>/dev/null | grep ":8443.*LISTENING" | awk '{print $5}')
    # If empty, try Linux format
    if [ -z "$PID" ]; then
        PID=$(netstat -tuln 2>/dev/null | grep ":8443 " | awk '{print $7}' | cut -d'/' -f1)
    fi
fi

# 最后尝试lsof
if [ -z "$PID" ] && command -v lsof &> /dev/null; then
    PID=$(lsof -ti:8443 2>/dev/null)
fi

if [ -z "$PID" ]; then
    echo -e "${YELLOW}[Info] No HTTPS server found running on port 8443${NC}"
else
    echo -e "${GREEN}[Found] Server process found (PID: $PID)${NC}"
    echo -e "${YELLOW}[Stop] Stopping server...${NC}"

    # Windows: use taskkill
    if command -v taskkill &> /dev/null; then
        taskkill //F //PID $PID > /dev/null 2>&1
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}[Success] HTTPS server stopped successfully (PID: $PID)${NC}"
        else
            echo -e "${RED}[Error] Failed to stop server process (PID: $PID)${NC}"
        fi
    else
        kill $PID 2>/dev/null
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}[Success] HTTPS server stopped successfully (PID: $PID)${NC}"
        else
            # Try force kill
            kill -9 $PID 2>/dev/null
            if [ $? -eq 0 ]; then
                echo -e "${GREEN}[Success] HTTPS server stopped successfully (PID: $PID)${NC}"
            else
                echo -e "${RED}[Error] Failed to stop server process (PID: $PID)${NC}"
            fi
        fi
    fi
fi

echo ""
echo -e "${BLUE}========================================${NC}"
echo ""
read -p "按 Enter 键退出..."