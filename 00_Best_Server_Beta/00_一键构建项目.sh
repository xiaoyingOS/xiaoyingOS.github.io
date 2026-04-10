#!/bin/bash

#########################################
# Best Server 一键构建脚本
#########################################

# 设置颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  Best Server 一键构建脚本${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  项目目录: $SCRIPT_DIR${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# 切换到脚本所在目录
cd "$SCRIPT_DIR" || exit 1

#########################################
# 步骤1：构建 Best_Server 框架
#########################################
echo -e "${GREEN}步骤1：构建 Best_Server 框架${NC}"
echo "----------------------------------------"

# 清理旧的构建文件
echo -e "${YELLOW}清理旧的构建文件...${NC}"
rm -rf build CMakeCache.txt CMakeFiles Makefile cmake_install.cmake CTestTestfile.cmake

# 创建构建目录并配置
echo -e "${YELLOW}配置项目...${NC}"
mkdir -p build
cd build
cmake ..

if [ $? -ne 0 ]; then
    echo -e "${RED}错误：CMake 配置失败！${NC}"
    exit 1
fi

# 编译框架
echo -e "${YELLOW}编译框架（使用4个并行任务）...${NC}"
cmake --build . -j4

if [ $? -ne 0 ]; then
    echo -e "${RED}错误：框架编译失败！${NC}"
    exit 1
fi

echo -e "${GREEN}框架构建成功！${NC}"
echo ""
echo -e "${BLUE}生成的文件：${NC}"
echo "----------------------------------------"

# 列出生成的文件
if [ -f "libbest_server.a" ]; then
    SIZE=$(ls -lh libbest_server.a | awk '{print $5}')
    echo -e "${GREEN}✓${NC} 框架静态库: build/libbest_server.a (${SIZE})"
fi

if [ -f "test_upload_fix" ]; then
    SIZE=$(ls -lh test_upload_fix | awk '{print $5}')
    echo -e "${GREEN}✓${NC} 上传测试程序: build/test_upload_fix (${SIZE})"
fi

if [ -f "tests/unit_tests" ]; then
    SIZE=$(ls -lh tests/unit_tests | awk '{print $5}')
    echo -e "${GREEN}✓${NC} 单元测试程序: build/tests/unit_tests (${SIZE})"
fi

if [ -f "examples/simple_test" ]; then
    SIZE=$(ls -lh examples/simple_test | awk '{print $5}')
    echo -e "${GREEN}✓${NC} 示例程序: build/examples/simple_test (${SIZE})"
fi

echo ""

# 返回脚本所在目录
cd "$SCRIPT_DIR"

#########################################
# 步骤2：构建 Web 服务器
#########################################
echo -e "${GREEN}步骤2：构建 Web 服务器${NC}"
echo "----------------------------------------"

# 切换到 Web 服务器目录
cd web

# 清理旧的构建文件
echo -e "${YELLOW}清理旧的构建文件...${NC}"
rm -rf build CMakeCache.txt CMakeFiles Makefile cmake_install.cmake CTestTestfile.cmake

# 创建构建目录并配置
echo -e "${YELLOW}配置项目...${NC}"
mkdir -p build
cd build
cmake ..

if [ $? -ne 0 ]; then
    echo -e "${RED}错误：CMake 配置失败！${NC}"
    exit 1
fi

# 编译 Web 服务器
echo -e "${YELLOW}编译 Web 服务器...${NC}"
cmake --build . -j4

if [ $? -ne 0 ]; then
    echo -e "${RED}错误：Web 服务器编译失败！${NC}"
    exit 1
fi

echo -e "${GREEN}Web 服务器构建成功！${NC}"
echo ""
echo -e "${BLUE}生成的文件：${NC}"
echo "----------------------------------------"

# 列出生成的文件
if [ -f "web_server" ]; then
    SIZE=$(ls -lh web_server | awk '{print $5}')
    echo -e "${GREEN}✓${NC} 标准 Web 服务器: web/build/web_server (${SIZE})"
fi

if [ -f "web_server_daemon" ]; then
    SIZE=$(ls -lh web_server_daemon | awk '{print $5}')
    echo -e "${GREEN}✓${NC} 守护进程版本: web/build/web_server_daemon (${SIZE})"
fi

echo ""

#########################################
# 构建完成总结
#########################################
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}构建完成！${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""
echo -e "${BLUE}所有生成文件的位置：${NC}"
echo "----------------------------------------"
echo ""
echo "【框架相关文件】"
echo "  框架静态库:     $SCRIPT_DIR/build/libbest_server.a"
echo "  测试程序:       $SCRIPT_DIR/build/test_upload_fix"
echo "  单元测试:       $SCRIPT_DIR/build/tests/unit_tests"
echo "  示例程序:       $SCRIPT_DIR/build/examples/simple_test"
echo ""
echo "【Web 服务器相关文件】"
echo "  Web 服务器:     $SCRIPT_DIR/web/build/web_server"
echo "  守护进程:       $SCRIPT_DIR/web/build/web_server_daemon"
echo "  服务器日志:     $SCRIPT_DIR/web/server.log"
echo "  静态文件目录:   $SCRIPT_DIR/web/static/"
echo "  上传文件目录:   $SCRIPT_DIR/web/uploads/"
echo ""
echo -e "${BLUE}启动 Web 服务器的方法：${NC}"
echo "----------------------------------------"
echo ""
echo "【前台运行（测试用）】"
echo "  ./web/build/web_server"
echo ""
echo "【后台运行（生产环境）】"
echo "  重定向到空设备"
echo "  nohup ./web/build/web_server > /dev/null 2>&1 &"
echo ""
echo "【测试服务器】"
echo "  curl http://localhost:8080/api/status"
echo ""
echo -e "${BLUE}停止服务器的方法：${NC}"
echo "----------------------------------------"
echo "按 Ctrl + C 即可停止服务器"
echo "  也可以查找端口线程 ps aux | grep -E 'web_server|8080' | grep -v grep"
echo "  终止服务器进程 kill -9 进程号"
echo ""
echo -e "${GREEN}构建脚本执行完毕！${NC}"