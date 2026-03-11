✅ 音频修复完成！

主要修复内容：
1. ✅ 在createOffer和createAnswer时添加offerToReceiveAudio和offerToReceiveVideo选项
2. ✅ 改进getUserMedia音频配置，添加echoCancellation、noiseSuppression、autoGainControl
3. ✅ 修复ontrack处理，确保远程音频流正确设置到video元素
4. ✅ 修复语音通话发起方在接听后未获取音频流的问题
5. ✅ 修复视频通话发起方在接听后未将本地流添加到WebRTC连接的问题
6. ✅ 添加详细的音频轨道日志，便于调试
7. ✅ 确保所有音频轨道在添加时都被启用

预期效果：
- 语音通话：双方都能听到对方声音
- 视频通话：双方都能看到画面和听到声音
- 消除刺耳的噪音
- 改善音频质量和清晰度

请刷新页面后重新测试通话功能。
