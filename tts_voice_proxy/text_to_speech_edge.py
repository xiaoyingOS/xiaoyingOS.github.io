# -*- coding: utf-8 -*-
"""
文本转语音生成脚本 - 使用 Edge-TTS
支持高质量语音合成，包括多种中文语音
"""

import json
import sys
import os
import asyncio
import edge_tts

def generate_with_edge_tts(config):
    """使用 Edge-TTS 生成语音"""
    text = config.get('text', '')
    voice_model = config.get('voice_model', 'zh-CN-XiaoxiaoNeural')
    pitch = config.get('pitch', 1.0)
    rate = config.get('rate', 1.0)
    volume = config.get('volume', 1.0)
    output_file = config.get('output_file', 'output.wav')
    
    if not text:
        return {"success": False, "error": "文本内容为空"}
    
    print(f"[DEBUG] 使用 Edge-TTS 生成语音")
    print(f"[DEBUG] 文本长度: {len(text)}")
    print(f"[DEBUG] 原始语音模型: {voice_model}")
    
    # 语音模型名称映射
    # 将 Web Speech API 的语音名称映射到 Edge-TTS 的语音 ID
    voice_mapping = {
        # 大陆普通话
        'Microsoft Huihui': 'zh-CN-XiaoxiaoNeural',
        'Microsoft Huihui - Chinese (Simplified, PRC)': 'zh-CN-XiaoxiaoNeural',
        'Microsoft Huihui - Chinese (Simplified, PRC) (zh-CN)': 'zh-CN-XiaoxiaoNeural',
        'Microsoft Kangkang': 'zh-CN-YunxiNeural',
        'Microsoft Kangkang - Chinese (Simplified, PRC)': 'zh-CN-YunxiNeural',
        'Microsoft Kangkang - Chinese (Simplified, PRC) (zh-CN)': 'zh-CN-YunxiNeural',
        'Microsoft Xiaoxiao': 'zh-CN-XiaoxiaoNeural',
        'Microsoft Xiaoyi': 'zh-CN-XiaoyiNeural',
        'Microsoft Yunjian': 'zh-CN-YunjianNeural',
        'Microsoft Yunxi': 'zh-CN-YunxiNeural',
        'Microsoft Yunxia': 'zh-CN-YunxiaNeural',
        'Microsoft Yunyang': 'zh-CN-YunyangNeural',
        
        # 方言
        'Microsoft Xiaobei': 'zh-CN-liaoning-XiaobeiNeural',
        'Microsoft Xiaoni': 'zh-CN-shaanxi-XiaoniNeural',
        
        # 台湾
        'Microsoft HsiaoChen': 'zh-TW-HsiaoChenNeural',
        'Microsoft HsiaoYu': 'zh-TW-HsiaoYuNeural',
        'Microsoft YunJhe': 'zh-TW-YunJheNeural',
        
        # 香港
        'Microsoft HiuGaai': 'zh-HK-HiuGaaiNeural',
        'Microsoft HiuMaan': 'zh-HK-HiuMaanNeural',
        'Microsoft WanLung': 'zh-HK-WanLungNeural',
    }
    
    # 尝试映射语音模型
    edge_voice = voice_model
    
    # 如果已经是 Edge-TTS 格式，直接使用
    if '-' in voice_model and len(voice_model.split('-')) >= 2 and 'Microsoft' not in voice_model:
        edge_voice = voice_model
        print(f"[DEBUG] 使用 Edge-TTS 格式语音模型: {edge_voice}")
    else:
        # 尝试从 Web Speech API 名称映射到 Edge-TTS
        mapped = False
        for web_name, edge_name in voice_mapping.items():
            if web_name in voice_model:
                edge_voice = edge_name
                print(f"[DEBUG] 语音模型已映射: {web_name} -> {edge_voice}")
                mapped = True
                break
        
        # 如果没有找到映射，尝试自动推断
        if not mapped:
            print(f"[DEBUG] 未找到映射，尝试自动推断...")
            
            # 提取语音名称中的关键部分
            voice_name = voice_model.split(' ')[1] if ' ' in voice_model else voice_model
            
            # 如果包含中文，根据性别推断
            if 'Chinese' in voice_model or 'zh-CN' in voice_model:
                # 默认映射到晓晓（女声）
                edge_voice = 'zh-CN-XiaoxiaoNeural'
                print(f"[DEBUG] 自动映射到默认语音: {edge_voice}")
            elif 'Taiwan' in voice_model or 'zh-TW' in voice_model:
                edge_voice = 'zh-TW-HsiaoChenNeural'
                print(f"[DEBUG] 自动映射到台湾语音: {edge_voice}")
            elif 'Hong Kong' in voice_model or 'zh-HK' in voice_model:
                edge_voice = 'zh-HK-HiuGaaiNeural'
                print(f"[DEBUG] 自动映射到香港语音: {edge_voice}")
            else:
                # 最后的备用方案
                edge_voice = 'zh-CN-XiaoxiaoNeural'
                print(f"[DEBUG] 使用备用语音: {edge_voice}")
    
    print(f"[DEBUG] 最终语音模型: {edge_voice}")
    print(f"[DEBUG] 输出文件: {output_file}")
    print(f"[DEBUG] 音调: {pitch}, 语速: {rate}, 音量: {volume}")
    
    try:
        # 创建异步函数来生成语音
        async def generate_audio():
            # 计算语速（Edge-TTS 使用 +-% 格式）
            # 1.0 = +0%, 0.5 = -50%, 2.0 = +100%
            rate_str = f"+{int((rate - 1) * 100)}%"
            
            # 计算音调（Edge-TTS 使用 +Hz 格式）
            # 1.0 = +0Hz, 0.5 = -10Hz, 2.0 = +10Hz
            pitch_str = f"+{int((pitch - 1) * 10)}Hz"
            
            # 计算音量（Edge-TTS 使用 +-% 格式）
            # 1.0 = +0%, 0.5 = -50%, 2.0 = +100%
            volume_str = f"+{int((volume - 1) * 100)}%"
            
            print(f"[DEBUG] 语速参数: {rate_str}")
            print(f"[DEBUG] 音调参数: {pitch_str}")
            print(f"[DEBUG] 音量参数: {volume_str}")
            
            # 创建 TTS 对象
            communicate = edge_tts.Communicate(
                text=text,
                voice=edge_voice,
                rate=rate_str,
                pitch=pitch_str,
                volume=volume_str
            )
            
            # 保存到文件
            await communicate.save(output_file)
            
            return True
        
        # 运行异步函数
        asyncio.run(generate_audio())
        
        # 检查文件是否生成
        if os.path.exists(output_file):
            file_size = os.path.getsize(output_file)
            print(f"[DEBUG] 音频文件已生成，大小: {file_size} 字节")
            
            if file_size > 0:
                return {
                    "success": True,
                    "output_file": output_file,
                    "output_path": os.path.abspath(output_file),
                    "file_exists": True,
                    "file_size": file_size,
                    "format": "mp3" if output_file.endswith('.mp3') else "wav",
                    "method": "edge-tts",
                    "text_length": len(text),
                    "voice_model": voice_model,
                    "pitch": pitch,
                    "rate": rate,
                    "volume": volume
                }
            else:
                return {
                    "success": False,
                    "error": f"音频文件已生成但大小为 0 字节",
                    "output_file": output_file,
                    "file_size": 0
                }
        else:
            return {"success": False, "error": f"音频文件未生成"}
            
    except Exception as e:
        print(f"[ERROR] Edge-TTS 生成失败: {str(e)}")
        import traceback
        traceback.print_exc()
        return {"success": False, "error": f"Edge-TTS 生成失败: {str(e)}"}

def get_chinese_voices():
    """获取可用的中文语音列表"""
    print("正在获取中文语音列表...")
    
    # 创建异步函数来获取语音列表
    async def fetch_voices():
        voices = await edge_tts.list_voices()
        return voices
    
    # 运行异步函数
    voices = asyncio.run(fetch_voices())
    
    # 筛选中文语音
    chinese_voices = []
    for voice in voices:
        if voice['Locale'].startswith('zh-'):
            chinese_voices.append({
                'name': voice['Name'],
                'display_name': voice['FriendlyName'],
                'locale': voice['Locale'],
                'gender': voice['Gender'],
                'categories': voice.get('VoicePersonalities', 'N/A')
            })
    
    return chinese_voices

if __name__ == "__main__":
    # 如果命令行参数是 --list-voices，列出中文语音
    if len(sys.argv) > 1 and sys.argv[1] == '--list-voices':
        voices = get_chinese_voices()
        print(f"\n可用中文语音 ({len(voices)} 个):")
        print("-" * 80)
        for voice in voices:
            print(f"名称: {voice['name']}")
            print(f"显示名称: {voice['display_name']}")
            print(f"地区: {voice['locale']}")
            print(f"性别: {voice['gender']}")
            print(f"特性: {voice['categories']}")
            print("-" * 80)
        sys.exit(0)
    
    # 从命令行参数读取配置文件路径
    config_file = "speech_config.json"
    
    if len(sys.argv) > 1:
        config_file = sys.argv[1]
    
    print(f"读取配置文件: {config_file}")
    
    # 读取配置
    with open(config_file, 'r', encoding='utf-8') as f:
        config = json.load(f)
    
    print(f"配置内容: {json.dumps(config, ensure_ascii=False, indent=2)}")
    
    # 使用 Edge-TTS 生成语音
    print(f"\n使用 Edge-TTS 生成语音...")
    result = generate_with_edge_tts(config)
    
    print(f"\n生成结果:")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    
    if result.get('success'):
        print(f"\n✅ 音频文件已生成: {result.get('output_file')}")
        print(f"📁 文件大小: {result.get('file_size')} 字节")
        print(f"🎵 文件格式: {result.get('format', 'unknown')}")
        
        # 如果文件大小为 0，提示用户
        if result.get('file_size', 0) == 0:
            print(f"\n⚠️ 警告: 文件大小为 0 字节，可能生成失败")
    else:
        print(f"\n❌ 生成失败: {result.get('error')}")
        sys.exit(1)