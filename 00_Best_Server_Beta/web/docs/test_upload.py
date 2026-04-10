#!/usr/bin/env python3
import socket
import time

def test_upload():
    # 读取测试文件
    with open('/data/data/com.termux/files/home/Server/web/test_upload_content.txt', 'rb') as f:
        file_data = f.read()
    
    # 创建HTTP POST请求
    request = (
        f"POST /upload HTTP/1.1\r\n"
        f"Host: 127.0.0.1:8080\r\n"
        f"X-Filename: test_upload.txt\r\n"
        f"Content-Length: {len(file_data)}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode()
    
    # 连接到服务器
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect(('127.0.0.1', 8080))
        
        # 发送请求头
        sock.sendall(request)
        time.sleep(0.1)
        
        # 发送文件数据
        sock.sendall(file_data)
        
        # 接收响应
        response = b""
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk
        
        sock.close()
        
        print("服务器响应:")
        print(response.decode('utf-8', errors='ignore'))
        return True
        
    except ConnectionRefusedError:
        print("错误: 连接被拒绝 - 服务器可能未运行")
        return False
    except Exception as e:
        print(f"错误: {e}")
        return False

if __name__ == "__main__":
    print("开始测试文件上传...")
    test_upload()