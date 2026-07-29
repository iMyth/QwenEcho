# 模型下载系统实现总结

## ✅ 已完成

### 1. 核心基础设施

#### ModelStorage (`lib/src/model/model_storage.dart`)
- ✅ 跨平台存储路径管理
  - iOS: `Documents/models/`
  - Android: 外部存储优先，回退到内部存储
- ✅ 模型存在性检查
- ✅ 磁盘空间统计
- ✅ 模型删除功能

#### ModelDownloader (`lib/src/model/model_downloader.dart`)
- ✅ 下载进度追踪
- ✅ 流式进度更新
- ✅ 下载状态管理
- ✅ 取消下载支持
- ✅ 顺序下载多个模型
- ⚠️ TODO: 配置实际下载 URL
- ⚠️ TODO: 实现 ASR 模型压缩包解压

#### ModelDownloadScreen (`lib/src/ui/model_download_screen.dart`)
- ✅ 首次启动引导页
- ✅ WiFi 检测与警告
- ✅ 实时进度显示（每个模型）
- ✅ 下载/取消按钮
- ✅ 错误处理与重试
- ✅ "稍后下载"选项

### 2. 应用集成

#### main.dart 修改
- ✅ 启动时检查模型状态
- ✅ 模型未就绪时显示下载页
- ✅ 下载完成后自动跳转到主页

#### model_repository.dart
- ✅ 无需修改（已支持多路径查找）
- ✅ 自动查找下载的模型

### 3. 依赖更新

#### pubspec.yaml
- ✅ 添加 `http: ^1.2.0`（HTTP 下载）
- ✅ 添加 `connectivity_plus: ^6.0.3`（网络状态检测）

## 📋 工作流程

```
用户首次启动 App
    ↓
ModelCheckScreen 检查模型
    ↓
模型未找到 → 显示 ModelDownloadScreen
    ↓
用户点击"下载模型"
    ↓
WiFi 检测（非 WiFi 显示警告）
    ↓
下载 ASR 模型 (240MB) → 进度 0-50%
    ↓
下载 LLM 模型 (500MB) → 进度 50-100%
    ↓
下载完成 → 自动跳转到 HomeScreen
```

## ⚠️ 待完成事项

### 高优先级

1. **配置实际模型下载 URL**
   - 当前使用占位 URL
   - 需要上传模型到 HuggingFace 或 GitHub Releases
   - 修改 `model_downloader.dart` 中的 `_getDownloadUrl()` 方法

2. **实现 ASR 模型压缩包解压**
   - ASR 模型下载后是 `.tgz` 格式
   - 需要解压到 `SenseVoiceSmall-onnx/` 目录
   - 推荐使用 `archive` 包

3. **MD5/SHA256 完整性校验**
   - 下载完成后校验文件完整性
   - 防止损坏的模型文件

### 中优先级

4. **断点续传**
   - 当前下载中断需要重新开始
   - 实现 HTTP Range 请求支持

5. **后台下载**
   - iOS: `background URLSession`
   - Android: `WorkManager`
   - 用户可以切换到其他 app

6. **多下载源支持**
   - 主源：HuggingFace
   - 备源：GitHub Releases、国内镜像
   - 自动切换失败的源

### 低优先级

7. **增量更新**
   - 只下载变化的部分
   - 减少下载流量

8. **下载历史记录**
   - 记录下载时间、版本
   - 支持回滚

## 🧪 测试计划

### 单元测试
```bash
# 测试 ModelStorage
flutter test test/model_storage_test.dart

# 测试 ModelDownloader
flutter test test/model_downloader_test.dart
```

### 集成测试
1. 清除应用数据
2. 启动 app → 应显示下载页
3. 点击"下载模型" → 应显示进度
4. 下载完成 → 应跳转到主页
5. 重启 app → 应直接进入主页

### 真机测试
- iOS: 下载 → 解压 → 加载模型 → 开始翻译
- Android: 同上

## 📦 模型发布流程

### 方案 1: HuggingFace（推荐）
```bash
# 上传模型到 HuggingFace
huggingface-cli upload mythchow/qwen-echo-models \
  ./models/SenseVoiceSmall-onnx \
  asr/sensevoice-small.tgz

huggingface-cli upload mythchow/qwen-echo-models \
  ./models/Qwen3.5-0.8B-Q4_K_M.gguf \
  llm/qwen3.5-0.8b-q4.gguf
```

### 方案 2: GitHub Releases
```bash
# 打包模型
tar -czf sensevoice-small.tgz -C models SenseVoiceSmall-onnx
cp models/Qwen3.5-0.8B-Q4_K_M.gguf ./

# 创建 Release
gh release create v1.0.0 \
  sensevoice-small.tgz \
  Qwen3.5-0.8B-Q4_K_M.gguf \
  --title "QwenEcho Models v1.0.0" \
  --notes "Initial model release"
```

## 🔧 下一步实施

### Phase 1: 配置下载 URL（30 分钟）
1. 上传模型到 HuggingFace/GitHub
2. 更新 `_getDownloadUrl()` 方法
3. 测试下载流程

### Phase 2: 实现压缩包解压（1-2 小时）
1. 添加 `archive` 依赖
2. 实现 `.tgz` 解压逻辑
3. 测试 ASR 模型加载

### Phase 3: 完整性校验（1 小时）
1. 计算模型文件 MD5
2. 下载后校验
3. 失败时重新下载

### Phase 4: 真机测试（2-3 小时）
1. iOS 真机测试
2. Android 真机测试
3. 修复发现的问题

## 📊 预估工作量

| 任务 | 时间 |
|------|------|
| 已完成的基础设施 | 3 小时 |
| 配置下载 URL | 0.5 小时 |
| 实现压缩包解压 | 2 小时 |
| 完整性校验 | 1 小时 |
| 真机测试 | 3 小时 |
| **总计** | **~10 小时** |

## 🎯 关键文件清单

### 新增文件
- `lib/src/model/model_storage.dart` - 存储管理
- `lib/src/model/model_downloader.dart` - 下载管理
- `lib/src/ui/model_download_screen.dart` - 下载 UI

### 修改文件
- `pubspec.yaml` - 添加依赖
- `lib/main.dart` - 集成下载检查

### 待创建
- 模型下载 URL 配置
- 压缩包解压逻辑
- 完整性校验代码

## 💡 设计亮点

1. **渐进式下载** - 用户可以看到每个模型的进度
2. **WiFi 检测** - 避免用户在移动网络下意外下载大文件
3. **错误恢复** - 下载失败可以重试
4. **灵活跳过** - 用户可以选择"稍后下载"（功能受限）
5. **跨平台一致** - iOS 和 Android 使用相同的 UI 和逻辑

## 🚀 上线检查清单

- [ ] 配置实际模型下载 URL
- [ ] 实现 ASR 模型解压
- [ ] 添加 MD5 校验
- [ ] iOS 真机测试通过
- [ ] Android 真机测试通过
- [ ] 更新 README.md 说明
- [ ] 准备模型发布脚本
- [ ] 测试弱网环境
- [ ] 测试下载中断恢复
- [ ] 测试存储空间不足情况
