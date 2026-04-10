#!/usr/bin/env python3
import socket
import time
import os

def generate_multipart_boundary():
    """生成一个随机的multipart boundary"""
    import random
    import string
    chars = string.ascii_letters + string.digits
    return "----WebKitFormBoundary" + ''.join(random.choice(chars) for _ in range(16))

def test_multipart_upload():
    # 读取测试文件
    test_file_path = '/data/data/com.termux/files/home/Server/web/test_upload_content.txt'
    with open(test_file_path, 'rb') as f:
        file_data = f.read()
    
    # 生成boundary
    boundary = generate_multipart_boundary()
    filename = os.path.basename(test_file_path)
    
    # 构建multipart/form-data请求体
    body = []
    body.append(f"--{boundary}\r\n")
    body.append(f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n')
    body.append("Content-Type: text/plain\r\n")
    body.append("\r\n")
    body.append(file_data)
    body.append(f"\r\n--{boundary}--\r\n")
    
    body_bytes = b''.join(part.encode() if isinstance(part, str) else part for part in body)
    
    # 构建HTTP请求
    request = (
        f"POST /upload HTTP/1.1\r\n"
        f"Host: 127.0.0.1:8080\r\n"
        f"Content-Type: multipart/form-data; boundary={boundary}\r\n"
        f"Content-Length: {len(body_bytes)}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode()
    
    # 连接到服务器
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        sock.connect(('127.0.0.1', 8080))
        
        # 发送请求
        sock.sendall(request + body_bytes)
        
        # 接收响应
        response = b""
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk
        
        sock.close()
        
        print("服务器响应:")
        print("=" * 60)
        print(response.decode('utf-8', errors='ignore'))
        print("=" * 60)
        return True
        
    except ConnectionRefusedError:
        print("错误: 连接被拒绝 - 服务器可能未运行")
        return False
    except Exception as e:
        print(f"错误: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    print("开始测试multipart文件上传...")
    print(f"测试文件: /data/data/com.termux/files/home/Server/web/test_upload_content.txt")
    success = test_multipart_upload()
    if success:
        print("\n测试完成！")
    else:
        print("\n测试失败！")