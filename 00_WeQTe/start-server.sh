#!/bin/bash

echo "=========================================="
echo "   聊天应用服务器启动选择器"
echo "=========================================="
echo ""
echo "请选择启动方式："
echo "1. HTTP服务器 (端口8084) - 视频语音功能受限"
echo "2. HTTPS服务器 (端口8443) - 视频语音功能完全可用"
echo "3. 同时启动HTTP和HTTPS服务器"
echo "4. 停止HTTP服务器"
echo "5. 停止HTTPS服务器"
echo "6. 停止所有服务器 (HTTP + HTTPS)"
echo "7. 停止并重启HTTPS服务器"
echo "8. 退出"
echo ""
read -p "请输入选项 (1-8): " choice

case $choice in
    1)
        echo ""
        echo "正在启动HTTP服务器..."

        # 检查并停止端口8084上的旧服务器
        # 使用更精确的匹配模式避免误匹配脚本自身
        OLD_PID=""
        if command -v pgrep &> /dev/null; then
            # 使用精确匹配：查找以server.js结尾的node进程
            # 排除包含grep、bash、start-server等关键词的进程
            OLD_PID=$(pgrep -af "node.*server\.js$" 2>/dev/null | grep -v -E "(grep|bash|start-server)" | awk '{print $1}' | head -1)
        fi

        # 如果pgrep未找到，尝试使用ps命令
        if [ -z "$OLD_PID" ]; then
            OLD_PID=$(ps aux 2>/dev/null | grep "node.*server\.js$" | grep -v -E "(grep|bash|start-server)" | awk '{print $2}' | head -1)
        fi

        # 兼容Windows netstat格式
        if [ -z "$OLD_PID" ] && command -v netstat &> /dev/null; then
            OLD_PID=$(netstat -ano 2>/dev/null | grep ":8084.*LISTENING" | awk '{print $5}')
        fi

        if [ -n "$OLD_PID" ]; then
            echo "找到旧HTTP服务器进程 (PID: $OLD_PID)"
            # 验证进程确实是node server.js
            if ps -p $OLD_PID -o comm= 2>/dev/null | grep -q "node"; then
                echo "停止旧HTTP服务器 (PID: $OLD_PID)..."
                kill $OLD_PID 2>/dev/null
                sleep 1
                # 如果进程仍在运行，强制终止
                if ps -p $OLD_PID > /dev/null 2>&1; then
                    kill -9 $OLD_PID 2>/dev/null
                fi
                echo "旧HTTP服务器已停止"
            else
                echo "警告：PID $OLD_PID 不是node进程，跳过终止"
                OLD_PID=""
            fi
        else
            echo "未找到运行中的HTTP服务器"
        fi

        echo "访问地址: http://localhost:8084"
        echo "手机访问: http://手机IP:8084"
        echo ""
        echo "注意：HTTP访问时视频语音功能可能受限"
        echo "建议使用HTTPS版本或直接打开HTML文件"
        echo ""
        node server.js
        ;;
    2)
        echo ""
        echo "正在启动HTTPS服务器..."
        echo ""

        # 检查并停止端口8443上的旧服务器
        # 优先使用pgrep查找node进程
        OLD_PID=""
        if command -v pgrep &> /dev/null; then
            OLD_PID=$(pgrep -f "node.*server-https.js" 2>/dev/null)
        fi

        # 如果pgrep未找到，尝试使用ps aux命令（跨平台兼容）
        if [ -z "$OLD_PID" ]; then
            OLD_PID=$(ps aux 2>/dev/null | grep "node.*server-https.js" | grep -v grep | awk '{print $2}')
        fi

        # 如果ps aux未找到，尝试使用ps -ef命令（部分系统支持）
        if [ -z "$OLD_PID" ]; then
            OLD_PID=$(ps -ef 2>/dev/null | grep "node.*server-https.js" | grep -v grep | awk '{print $2}')
        fi

        # 兼容Windows netstat格式
        if [ -z "$OLD_PID" ] && command -v netstat &> /dev/null; then
            OLD_PID=$(netstat -ano 2>/dev/null | grep ":8443.*LISTENING" | awk '{print $5}')
        fi

        # Linux ss命令支持（netstat的现代替代）
        if [ -z "$OLD_PID" ] && command -v ss &> /dev/null; then
            OLD_PID=$(ss -tlnp 2>/dev/null | grep ":8443" | grep node | awk '{print $7}' | sed 's/.*pid=\([0-9]*\).*/\1/')
        fi

        if [ -n "$OLD_PID" ]; then
            echo "停止旧服务器 (PID: $OLD_PID)..."
            # Windows: 使用 taskkill
            if command -v taskkill &> /dev/null; then
                taskkill //F //PID $OLD_PID > /dev/null 2>&1
            else
                kill $OLD_PID 2>/dev/null
                sleep 1
                # 如果进程仍在运行，强制终止
                if ps -p $OLD_PID > /dev/null 2>&1; then
                    kill -9 $OLD_PID 2>/dev/null
                fi
            fi
            echo "旧服务器已停止"
        fi

        # 后台启动服务器并记录日志
        nohup node server-https.js > https-server.log 2>&1 &
        SERVER_PID=$!

        # 等待服务器启动
        sleep 2

        echo "=========================================="
        echo "HTTPS服务器启动成功！"
        echo "=========================================="
        echo ""
        echo "服务器信息："
        echo "  进程ID: $SERVER_PID"
        echo "  日志文件: $(pwd)/https-server.log"
        echo ""
        echo "服务器输出："
        echo "----------------------------------------"
        cat https-server.log
        echo "----------------------------------------"
        echo ""
        echo "提示："
        echo "  • 浏览器会提示证书不安全，请点击'高级'然后'继续访问'"
        echo "  • 查看日志: tail -f https-server.log"
        echo "  • 停止服务器: kill $SERVER_PID"
        echo ""
        echo "视频语音功能完全可用"
        echo ""

        printf "服务器已后台启动，按任意键继续..."
        read -n1 -s -r
        echo ""
        echo "继续执行脚本！"
        echo ""
        ;;
    3)
        echo ""
        echo "正在同时启动HTTP和HTTPS服务器..."
        echo ""

        # 检查并停止端口8084上的旧HTTP服务器
        OLD_HTTP_PID=""
        if command -v pgrep &> /dev/null; then
            OLD_HTTP_PID=$(pgrep -af "node.*server\.js$" 2>/dev/null | grep -v -E "(grep|bash|start-server)" | awk '{print $1}' | head -1)
        fi

        if [ -z "$OLD_HTTP_PID" ]; then
            OLD_HTTP_PID=$(ps aux 2>/dev/null | grep "node.*server\.js$" | grep -v -E "(grep|bash|start-server)" | awk '{print $2}' | head -1)
        fi

        if [ -z "$OLD_HTTP_PID" ]; then
            OLD_HTTP_PID=$(ps -ef 2>/dev/null | grep "node.*server\.js$" | grep -v -E "(grep|bash|start-server)" | awk '{print $2}' | head -1)
        fi

        if [ -z "$OLD_HTTP_PID" ] && command -v netstat &> /dev/null; then
            OLD_HTTP_PID=$(netstat -ano 2>/dev/null | grep ":8084.*LISTENING" | awk '{print $5}')
        fi

        if [ -z "$OLD_HTTP_PID" ] && command -v ss &> /dev/null; then
            OLD_HTTP_PID=$(ss -tlnp 2>/dev/null | grep ":8084" | grep node | awk '{print $7}' | sed 's/.*pid=\([0-9]*\).*/\1/')
        fi

        if [ -n "$OLD_HTTP_PID" ]; then
            echo "找到旧HTTP服务器进程 (PID: $OLD_HTTP_PID)"
            if ps -p $OLD_HTTP_PID -o comm= 2>/dev/null | grep -q "node"; then
                echo "停止旧HTTP服务器 (PID: $OLD_HTTP_PID)..."
                kill $OLD_HTTP_PID 2>/dev/null
                sleep 1
                if ps -p $OLD_HTTP_PID > /dev/null 2>&1; then
                    kill -9 $OLD_HTTP_PID 2>/dev/null
                fi
                echo "旧HTTP服务器已停止"
            else
                echo "警告：PID $OLD_HTTP_PID 不是node进程，跳过终止"
                OLD_HTTP_PID=""
            fi
        fi

        # 检查并停止端口8443上的旧HTTPS服务器
        OLD_HTTPS_PID=""
        if command -v pgrep &> /dev/null; then
            OLD_HTTPS_PID=$(pgrep -f "node.*server-https.js" 2>/dev/null)
        fi

        if [ -z "$OLD_HTTPS_PID" ]; then
            OLD_HTTPS_PID=$(ps aux 2>/dev/null | grep "node.*server-https.js" | grep -v grep | awk '{print $2}')
        fi

        if [ -z "$OLD_HTTPS_PID" ]; then
            OLD_HTTPS_PID=$(ps -ef 2>/dev/null | grep "node.*server-https.js" | grep -v grep | awk '{print $2}')
        fi

        if [ -z "$OLD_HTTPS_PID" ] && command -v netstat &> /dev/null; then
            OLD_HTTPS_PID=$(netstat -ano 2>/dev/null | grep ":8443.*LISTENING" | awk '{print $5}')
        fi

        if [ -z "$OLD_HTTPS_PID" ] && command -v ss &> /dev/null; then
            OLD_HTTPS_PID=$(ss -tlnp 2>/dev/null | grep ":8443" | grep node | awk '{print $7}' | sed 's/.*pid=\([0-9]*\).*/\1/')
        fi

        if [ -n "$OLD_HTTPS_PID" ]; then
            echo "找到旧HTTPS服务器进程 (PID: $OLD_HTTPS_PID)"
            echo "停止旧HTTPS服务器 (PID: $OLD_HTTPS_PID)..."
            if command -v taskkill &> /dev/null; then
                taskkill //F //PID $OLD_HTTPS_PID > /dev/null 2>&1
            else
                kill $OLD_HTTPS_PID 2>/dev/null
                sleep 1
                if ps -p $OLD_HTTPS_PID > /dev/null 2>&1; then
                    kill -9 $OLD_HTTPS_PID 2>/dev/null
                fi
            fi
            echo "旧HTTPS服务器已停止"
        fi

        echo ""
        echo "HTTP服务器:"
        echo "  访问地址: http://localhost:8084"
        echo "  手机访问: http://手机IP:8084"
        echo ""
        echo "HTTPS服务器:"
        echo "  访问地址: https://localhost:8443"
        echo "  手机访问: https://手机IP:8443"
        echo ""
        echo "建议使用HTTPS版本以获得完整功能"
        echo ""
        node server.js &
        HTTP_PID=$!
        node server-https.js &
        HTTPS_PID=$!

        echo ""
        echo "HTTP服务器进程ID: $HTTP_PID"
        echo "HTTPS服务器进程ID: $HTTPS_PID"
        echo ""
        echo "按 Ctrl+C 停止所有服务器"

        # 等待用户中断
        trap 'echo "正在停止服务器..."; kill $HTTP_PID 2>/dev/null; sleep 0.5; kill $HTTPS_PID 2>/dev/null; exit' INT
        wait
        ;;
    4)
        echo ""
        echo "正在停止HTTP服务器..."

        # 查找server.js进程（使用与选项1相同的精确匹配）
        OLD_PID=""
        if command -v pgrep &> /dev/null; then
            # 使用精确匹配：查找以server.js结尾的node进程
            # 排除包含grep、bash、start-server等关键词的进程
            OLD_PID=$(pgrep -af "node.*server\.js$" 2>/dev/null | grep -v -E "(grep|bash|start-server)" | awk '{print $1}' | head -1)
        fi

        if [ -z "$OLD_PID" ]; then
            OLD_PID=$(ps aux 2>/dev/null | grep "node.*server\.js$" | grep -v -E "(grep|bash|start-server)" | awk '{print $2}' | head -1)
        fi

        if [ -z "$OLD_PID" ]; then
            OLD_PID=$(ps -ef 2>/dev/null | grep "node.*server\.js$" | grep -v -E "(grep|bash|start-server)" | awk '{print $2}' | head -1)
        fi

        # 兼容Windows netstat格式
        if [ -z "$OLD_PID" ] && command -v netstat &> /dev/null; then
            OLD_PID=$(netstat -ano 2>/dev/null | grep ":8084.*LISTENING" | awk '{print $5}')
        fi

        if [ -z "$OLD_PID" ] && command -v ss &> /dev/null; then
            OLD_PID=$(ss -tlnp 2>/dev/null | grep ":8084" | grep node | awk '{print $7}' | sed 's/.*pid=\([0-9]*\).*/\1/')
        fi

        if [ -n "$OLD_PID" ]; then
            echo "找到HTTP服务器 (PID: $OLD_PID)"
            # 验证进程确实是node server.js
            if ps -p $OLD_PID -o comm= 2>/dev/null | grep -q "node"; then
                if command -v taskkill &> /dev/null; then
                    taskkill //F //PID $OLD_PID > /dev/null 2>&1
                else
                    kill $OLD_PID 2>/dev/null
                    sleep 1
                    if ps -p $OLD_PID > /dev/null 2>&1; then
                        kill -9 $OLD_PID 2>/dev/null
                    fi
                fi
                echo "HTTP服务器已停止"
            else
                echo "警告：PID $OLD_PID 不是node进程，跳过终止"
            fi
        else
            echo "未找到运行中的HTTP服务器"
        fi
        echo ""
        ;;
    5)
        echo ""
        echo "正在停止HTTPS服务器..."

        # 查找server-https.js进程
        OLD_PID=""
        if command -v pgrep &> /dev/null; then
            OLD_PID=$(pgrep -f "node.*server-https.js" 2>/dev/null)
        fi

        if [ -z "$OLD_PID" ]; then
            OLD_PID=$(ps aux 2>/dev/null | grep "node.*server-https.js" | grep -v grep | awk '{print $2}')
        fi

        if [ -z "$OLD_PID" ]; then
            OLD_PID=$(ps -ef 2>/dev/null | grep "node.*server-https.js" | grep -v grep | awk '{print $2}')
        fi

        # 兼容Windows netstat格式
        if [ -z "$OLD_PID" ] && command -v netstat &> /dev/null; then
            OLD_PID=$(netstat -ano 2>/dev/null | grep ":8443.*LISTENING" | awk '{print $5}')
        fi

        if [ -z "$OLD_PID" ] && command -v ss &> /dev/null; then
            OLD_PID=$(ss -tlnp 2>/dev/null | grep ":8443" | grep node | awk '{print $7}' | sed 's/.*pid=\([0-9]*\).*/\1/')
        fi

        if [ -n "$OLD_PID" ]; then
            echo "找到HTTPS服务器 (PID: $OLD_PID)"
            if command -v taskkill &> /dev/null; then
                taskkill //F //PID $OLD_PID > /dev/null 2>&1
            else
                kill $OLD_PID 2>/dev/null
                sleep 1
                if ps -p $OLD_PID > /dev/null 2>&1; then
                    kill -9 $OLD_PID 2>/dev/null
                fi
            fi
            echo "HTTPS服务器已停止"
        else
            echo "未找到运行中的HTTPS服务器"
        fi
        echo ""
        ;;
    6)
        echo ""
        echo "正在停止所有服务器 (HTTP + HTTPS)..."

        # 停止HTTP服务器（使用精确匹配）
        HTTP_PID=""
        if command -v pgrep &> /dev/null; then
            HTTP_PID=$(pgrep -af "node.*server\.js$" 2>/dev/null | grep -v -E "(grep|bash|start-server)" | awk '{print $1}' | head -1)
        fi

        if [ -z "$HTTP_PID" ]; then
            HTTP_PID=$(ps aux 2>/dev/null | grep "node.*server\.js$" | grep -v -E "(grep|bash|start-server)" | awk '{print $2}' | head -1)
        fi

        if [ -z "$HTTP_PID" ]; then
            HTTP_PID=$(ps -ef 2>/dev/null | grep "node.*server\.js$" | grep -v -E "(grep|bash|start-server)" | awk '{print $2}' | head -1)
        fi

        if [ -z "$HTTP_PID" ] && command -v netstat &> /dev/null; then
            HTTP_PID=$(netstat -ano 2>/dev/null | grep ":8084.*LISTENING" | awk '{print $5}')
        fi

        if [ -z "$HTTP_PID" ] && command -v ss &> /dev/null; then
            HTTP_PID=$(ss -tlnp 2>/dev/null | grep ":8084" | grep node | awk '{print $7}' | sed 's/.*pid=\([0-9]*\).*/\1/')
        fi

        if [ -n "$HTTP_PID" ]; then
            echo "找到HTTP服务器 (PID: $HTTP_PID)"
            # 验证进程确实是node server.js
            if ps -p $HTTP_PID -o comm= 2>/dev/null | grep -q "node"; then
                if command -v taskkill &> /dev/null; then
                    taskkill //F //PID $HTTP_PID > /dev/null 2>&1
                else
                    kill $HTTP_PID 2>/dev/null
                    sleep 1
                    if ps -p $HTTP_PID > /dev/null 2>&1; then
                        kill -9 $HTTP_PID 2>/dev/null
                    fi
                fi
                echo "HTTP服务器已停止"
            else
                echo "警告：PID $HTTP_PID 不是node进程，跳过终止"
            fi
        else
            echo "未找到运行中的HTTP服务器"
        fi

        # 停止HTTPS服务器
        HTTPS_PID=""
        if command -v pgrep &> /dev/null; then
            HTTPS_PID=$(pgrep -f "node.*server-https.js" 2>/dev/null)
        fi

        if [ -z "$HTTPS_PID" ]; then
            HTTPS_PID=$(ps aux 2>/dev/null | grep "node.*server-https.js" | grep -v grep | awk '{print $2}')
        fi

        if [ -z "$HTTPS_PID" ]; then
            HTTPS_PID=$(ps -ef 2>/dev/null | grep "node.*server-https.js" | grep -v grep | awk '{print $2}')
        fi

        if [ -z "$HTTPS_PID" ] && command -v netstat &> /dev/null; then
            HTTPS_PID=$(netstat -ano 2>/dev/null | grep ":8443.*LISTENING" | awk '{print $5}')
        fi

        if [ -z "$HTTPS_PID" ] && command -v ss &> /dev/null; then
            HTTPS_PID=$(ss -tlnp 2>/dev/null | grep ":8443" | grep node | awk '{print $7}' | sed 's/.*pid=\([0-9]*\).*/\1/')
        fi

        if [ -n "$HTTPS_PID" ]; then
            echo "找到HTTPS服务器 (PID: $HTTPS_PID)"
            if command -v taskkill &> /dev/null; then
                taskkill //F //PID $HTTPS_PID > /dev/null 2>&1
            else
                kill $HTTPS_PID 2>/dev/null
                sleep 1
                if ps -p $HTTPS_PID > /dev/null 2>&1; then
                    kill -9 $HTTPS_PID 2>/dev/null
                fi
            fi
            echo "HTTPS服务器已停止"
        else
            echo "未找到运行中的HTTPS服务器"
        fi

        echo ""
        echo "所有服务器已停止"
        echo ""
        ;;
    7)
        echo ""
        echo "正在停止并重启HTTPS服务器..."

        # 停止旧服务器（查找server-https.js进程）
        OLD_PID=""
        if command -v pgrep &> /dev/null; then
            OLD_PID=$(pgrep -f "node.*server-https.js" 2>/dev/null)
        fi

        if [ -z "$OLD_PID" ]; then
            OLD_PID=$(ps aux 2>/dev/null | grep "node.*server-https.js" | grep -v grep | awk '{print $2}')
        fi

        if [ -z "$OLD_PID" ]; then
            OLD_PID=$(ps -ef 2>/dev/null | grep "node.*server-https.js" | grep -v grep | awk '{print $2}')
        fi

        # 兼容Windows netstat格式
        if [ -z "$OLD_PID" ] && command -v netstat &> /dev/null; then
            OLD_PID=$(netstat -ano 2>/dev/null | grep ":8443.*LISTENING" | awk '{print $5}')
        fi

        if [ -z "$OLD_PID" ] && command -v ss &> /dev/null; then
            OLD_PID=$(ss -tlnp 2>/dev/null | grep ":8443" | grep node | awk '{print $7}' | sed 's/.*pid=\([0-9]*\).*/\1/')
        fi

        if [ -n "$OLD_PID" ]; then
            echo "停止旧服务器 (PID: $OLD_PID)..."
            if command -v taskkill &> /dev/null; then
                taskkill //F //PID $OLD_PID > /dev/null 2>&1
            else
                kill $OLD_PID 2>/dev/null
                sleep 1
                if ps -p $OLD_PID > /dev/null 2>&1; then
                    kill -9 $OLD_PID 2>/dev/null
                fi
            fi
            echo "旧服务器已停止"
        else
            echo "未找到运行中的服务器，将启动新服务器"
        fi

        echo ""
        echo "正在启动HTTPS服务器..."
        echo ""

        # 后台启动服务器并记录日志
        nohup node server-https.js > https-server.log 2>&1 &
        SERVER_PID=$!

        # 等待服务器启动
        sleep 2

        echo "=========================================="
        echo "HTTPS服务器启动成功！"
        echo "=========================================="
        echo ""
        echo "服务器信息："
        echo "  进程ID: $SERVER_PID"
        echo "  日志文件: $(pwd)/https-server.log"
        echo ""
        echo "服务器输出："
        echo "----------------------------------------"
        cat https-server.log
        echo "----------------------------------------"
        echo ""
        echo "提示："
        echo "  • 浏览器会提示证书不安全，请点击'高级'然后'继续访问'"
        echo "  • 查看日志: tail -f https-server.log"
        echo "  • 停止服务器: kill $SERVER_PID"
        echo ""
        echo "视频语音功能完全可用"
        echo ""

        printf "服务器已后台启动，按任意键继续..."
        read -n1 -s -r
        echo ""
        echo "继续执行脚本！"
        echo ""
        ;;
    8)
        echo "退出"
        exit 0
        ;;
    *)
        echo "无效选项，请重新运行脚本"
        exit 1
        ;;
esac