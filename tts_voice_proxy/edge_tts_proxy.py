#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Edge-TTS WebSocket 代理服务器
使用 WebSocket 实时发送音频数据和进度
"""

import asyncio
import json
import edge_tts
import websockets
from websockets.server import WebSocketServerProtocol

class EdgeTTSServer:
    def __init__(self):
        self.active_connections = set()
        self.cancel_flags = {}  # 取消标志
    
    # async def handle_client(self, websocket: WebSocketServerProtocol, path: str):老版本11需要传递 path参数
    async def handle_client(self, websocket: WebSocketServerProtocol):
        """处理客户端连接"""
        print(f'[服务器] 新客户端连接: {websocket.remote_address}')
        self.active_connections.add(websocket)
        client_id = id(websocket)
        self.cancel_flags[client_id] = False
        
        try:
            # 接收配置
            config = await websocket.recv()
            config_data = json.loads(config)
            
            # 检查是否是取消消息
            if config_data.get('type') == 'cancel':
                print(f'[服务器] 收到取消请求')
                return
            
            text = config_data.get('text', '')
            voice = config_data.get('voice_model', 'zh-CN-XiaoxiaoNeural')
            pitch = config_data.get('pitch', 1)
            rate = config_data.get('rate', 1)
            volume = config_data.get('volume', 1)
            is_preview = config_data.get('preview', False)  # 是否是试听模式
            
            print(f'[服务器] 收到请求: 语音={voice}, 文本长度={len(text)}')
            
            # 转换参数
            if rate == 1:
                rate_str = "+0%"
            else:
                rate_change = int((rate - 1) * 100)
                rate_change = max(-100, min(200, rate_change))
                rate_str = f"{rate_change:+d}%"
            
            if pitch == 1:
                pitch_str = "+0Hz"
            else:
                pitch_change = int((pitch - 1) * 100)
                pitch_change = max(-100, min(100, pitch_change))
                pitch_str = f"{pitch_change:+d}Hz"
            
            if volume == 1:
                volume_str = "+0%"
            else:
                volume_change = int((volume - 1) * 100)
                volume_change = max(-100, min(100, volume_change))
                volume_str = f"{volume_change:+d}%"
            
            print(f'[服务器] 参数: pitch={pitch_str}, rate={rate_str}, volume={volume_str}')
            
            # 创建通信对象
            communicate = edge_tts.Communicate(
                text,
                voice,
                rate=rate_str,
                pitch=pitch_str,
                volume=volume_str
            )
            
            # 先遍历一次计算总大小
            total_chunks = 0
            total_bytes = 0
            async for chunk in communicate.stream():
                if chunk["type"] == "audio":
                    total_chunks += 1
                    total_bytes += len(chunk["data"])
            
            print(f'[服务器] 总块数: {total_chunks}, 总大小: {total_bytes} 字节')
            
            # 发送总大小信息
            await websocket.send(json.dumps({
                'type': 'init',
                'total_chunks': total_chunks,
                'total_bytes': total_bytes
            }))
            
            # 第二次遍历，边生成边发送
            communicate = edge_tts.Communicate(
                text,
                voice,
                rate=rate_str,
                pitch=pitch_str,
                volume=volume_str
            )
            
            chunk_count = 0
            bytes_sent = 0
            audio_data = b''
            
            async for chunk in communicate.stream():
                # 检查是否取消
                if self.cancel_flags.get(client_id, False):
                    print(f'[服务器] 生成已取消')
                    await websocket.send(json.dumps({
                        'type': 'cancelled'
                    }))
                    return
                
                if chunk["type"] == "audio":
                    audio_data += chunk["data"]
                    chunk_count += 1
                    bytes_sent += len(chunk["data"])
                    
                    # 计算进度
                    progress = int((bytes_sent / total_bytes) * 100)
                    
                    # 试听模式不发送进度
                    if not is_preview and chunk_count % 10 == 0:
                        await websocket.send(json.dumps({
                            'type': 'progress',
                            'progress': progress,
                            'chunks': chunk_count,
                            'bytes': bytes_sent
                        }))
                        print(f'[服务器] 进度: {progress}% ({chunk_count}/{total_chunks}, {bytes_sent}/{total_bytes} 字节)')
            
            # 发送完成进度
            if not is_preview:
                await websocket.send(json.dumps({
                    'type': 'progress',
                    'progress': 100,
                    'chunks': chunk_count,
                    'bytes': bytes_sent
                }))
            
            # 发送音频数据（Base64编码）
            audio_base64 = audio_data.hex()  # 使用十六进制编码更高效
            await websocket.send(json.dumps({
                'type': 'audio',
                'data': audio_base64,
                'size': len(audio_data)
            }))
            
            # 发送完成消息
            await websocket.send(json.dumps({
                'type': 'complete',
                'size': len(audio_data)
            }))
            
            print(f'[服务器] 音频生成完成，大小: {len(audio_data)} 字节')
            
        except Exception as e:
            print(f'[服务器] 错误: {str(e)}')
            import traceback
            traceback.print_exc()
            await websocket.send(json.dumps({
                'type': 'error',
                'message': str(e)
            }))
        
        finally:
            self.active_connections.remove(websocket)
            if client_id in self.cancel_flags:
                del self.cancel_flags[client_id]
            print(f'[服务器] 客户端断开: {websocket.remote_address}')


async def run_server(port=8000):
    """启动 WebSocket 服务器"""
    server = EdgeTTSServer()
    
    print(f'=' * 60)
    print(f'Edge-TTS WebSocket 代理服务器')
    print(f'=' * 60)
    print(f'服务器地址: ws://localhost:{port}')
    print(f'状态: 运行中...')
    print(f'按 Ctrl+C 停止服务器')
    print(f'=' * 60)
    
    async with websockets.serve(server.handle_client, '0.0.0.0', port):
        await asyncio.Future()  # 永久运行


if __name__ == '__main__':
    asyncio.run(run_server(9191))
