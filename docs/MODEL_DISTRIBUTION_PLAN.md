# 模型分发方案规划

## 问题

当前 QwenEcho 需要两个大模型文件：
- **ASR 模型** (SenseVoice-Small): ~240MB
- **LLM 模型** (Qwen3.5-0.8B GGUF): ~500MB
- **总计**: ~740MB

当前部署方式：
- iOS: 可以打包进 app（测试用）或手动放置
- Android: 需要手动放到 `/sdcard/Android/data/com.example.qwen_echo/files/models/`

这对用户来说完全不可接受。

## 方案对比

### 方案 1: 全部打包进 App
**优点**: 开箱即用
**缺点**: 
- App 体积 ~740MB+，用户下载意愿极低
- App Store 可能拒绝（超过 200MB 限制需要特殊说明）
- 每次更新都要重新下载全部模型

**结论**: ❌ 不可行

### 方案 2: 应用内下载（推荐）⭐
**优点**:
- App 初始体积小（~50MB）
- 首次启动时下载模型，显示进度条
- 可以断点续传、重试
- 支持模型版本更新

**实现**:
```
用户首次启动
    ↓
显示"下载模型"引导页
    ↓
用户点击"下载" (WiFi 提示)
    ↓
下载 ASR 模型 (240MB) → 进度 0-50%
    ↓
下载 LLM 模型 (500MB) → 进度 50-100%
    ↓
解压/验证 → 存储到 app documents
    ↓
进入主界面
```

**技术细节**:
- 使用 `dio` 或 `http` 包下载
- 从 HuggingFace / GitHub Releases 下载
- 存储路径:
  - iOS: `Documents/models/`
  - Android: `filesDir/models/` (内部存储) 或 `externalFilesDir/models/` (外部存储)
- 下载失败自动重试（3 次）
- 支持后台下载（iOS: `background URLSession`）
- MD5 校验完整性

**结论**: ✅ **最佳方案**

### 方案 3: 按需下载
**优点**: 用户只下载需要的功能
**缺点**: 
- 实现复杂（多次下载）
- 用户体验碎片化

**实现**:
- 首次开启"语音识别"时下载 ASR 模型
- 首次开启"翻译"时下载 LLM 模型

**结论**: ⚠️ 可行但复杂，不推荐

### 方案 4: 混合方案
**优点**: 平衡体积和体验
**缺点**: 仍有部分下载

**实现**:
- 打包小模型（VAD、小 ASR）~50MB
- 下载大模型（LLM）~500MB

**结论**: ⚠️ 折中方案，不如方案 2 简洁

## 推荐方案：应用内下载系统

### 架构设计

```
lib/src/model/
├── model_repository.dart          # 现有：模型路径查找
├── model_downloader.dart          # 新增：下载管理器
├── model_storage.dart             # 新增：存储管理
└── model_update_checker.dart      # 新增：版本检查

lib/src/ui/
└── model_download_screen.dart     # 新增：下载引导页
```

### 核心组件

#### 1. ModelDownloader
```dart
class ModelDownloader {
  // 下载模型，返回进度流
  Stream<DownloadProgress> downloadModel(ModelSpec spec);
  
  // 暂停/恢复下载
  void pauseDownload(String modelId);
  void resumeDownload(String modelId);
  
  // 取消下载
  void cancelDownload(String modelId);
  
  // 检查下载状态
  DownloadStatus getStatus(String modelId);
}
```

#### 2. ModelStorage
```dart
class ModelStorage {
  // 获取模型存储路径
  Future<String> getModelPath(ModelSpec spec);
  
  // 检查模型是否已下载
  Future<bool> isModelDownloaded(ModelSpec spec);
  
  // 删除模型（释放空间）
  Future<void> deleteModel(ModelSpec spec);
  
  // 获取已用空间
  Future<int> getUsedSpace();
}
```

#### 3. ModelUpdateChecker
```dart
class ModelUpdateChecker {
  // 检查模型更新
  Future<UpdateInfo?> checkForUpdates();
  
  // 获取最新版本信息
  Future<ModelVersionInfo> getLatestVersion();
}
```

### 下载源

**主源**: HuggingFace
```
https://huggingface.co/mythchow/qwen-echo-models/resolve/main/
├── asr/sensevoice-small.tgz
└── llm/qwen3.5-0.8b-q4.gguf
```

**备源**: GitHub Releases
```
https://github.com/iMyth/QwenEcho/releases/download/v1.0.0/
├── asr-models.tgz
└── llm-models.tgz
```

### UI 流程

#### 首次启动流程
```
┌─────────────────────────────────┐
│  欢迎使用 QwenEcho              │
│                                 │
│  同声传译需要下载 AI 模型       │
│  总大小: 740 MB                 │
│                                 │
│  [下载模型]                     │
│                                 │
│  💡 建议在 WiFi 环境下下载      │
└─────────────────────────────────┘
```

#### 下载进度页
```
┌─────────────────────────────────┐
│  正在下载模型...                │
│                                 │
│  ASR 模型 (语音识别)            │
│  ████████████░░░░░░  60%        │
│  144 MB / 240 MB                │
│                                 │
│  LLM 模型 (翻译)                │
│  ░░░░░░░░░░░░░░░░░░  等待中     │
│                                 │
│  [暂停]  [取消]                 │
└─────────────────────────────────┘
```

#### 下载完成
```
┌─────────────────────────────────┐
│  ✓ 模型下载完成！               │
│                                 │
│  已下载: 740 MB                 │
│                                 │
│  [开始使用]                     │
└─────────────────────────────────┘
```

### 关键特性

1. **WiFi 检测与提示**
   - 非 WiFi 环境弹窗警告
   - 允许用户选择继续或等待

2. **断点续传**
   - 下载中断后自动恢复
   - 支持手动暂停/恢复

3. **后台下载**
   - iOS: 使用 `background URLSession`
   - Android: 使用 `WorkManager`
   - 用户可以切换到其他 app

4. **完整性校验**
   - MD5/SHA256 校验
   - 下载失败自动重试（3 次）

5. **存储空间管理**
   - 下载前检查可用空间
   - 空间不足时提示
   - 支持删除模型释放空间

6. **版本更新**
   - 启动时检查模型更新
   - 提示用户下载新版本
   - 支持增量更新（未来优化）

### 存储路径

**iOS**:
```
Documents/
└── models/
    ├── asr/
    │   └── sensevoice-small/
    │       ├── model.int8.onnx
    │       ├── tokens.txt
    │       └── metadata.json
    └── llm/
        └── qwen3.5-0.8b/
            └── model.gguf
```

**Android**:
```
Android/data/com.example.qwen_echo/files/
└── models/
    ├── asr/
    │   └── sensevoice-small/
    └── llm/
        └── qwen3.5-0.8b/
```

### 修改现有代码

#### model_repository.dart
```dart
// 移除手动放置逻辑，改为：
if (Platform.isIOS || Platform.isAndroid) {
  final docsDir = await getApplicationDocumentsDirectory();
  final modelPath = '${docsDir.path}/models/${spec.dirName}';
  final status = await _checkModel(spec, modelPath);
  if (status != null) return status;
}
```

#### 新增 ModelDownloadScreen
```dart
class ModelDownloadScreen extends StatefulWidget {
  @override
  State<ModelDownloadScreen> createState() => _ModelDownloadScreenState();
}

class _ModelDownloadScreenState extends State<ModelDownloadScreen> {
  final _downloader = ModelDownloader();
  double _progress = 0;
  String _status = '准备下载';
  
  @override
  void initState() {
    super.initState();
    _startDownload();
  }
  
  Future<void> _startDownload() async {
    // 下载 ASR + LLM 模型
    // 更新进度条
  }
  
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Column(
        children: [
          LinearProgressIndicator(value: _progress),
          Text(_status),
          // ... UI
        ],
      ),
    );
  }
}
```

### 实施计划

**Phase 1: 基础设施** (2-3 天)
1. 实现 `ModelDownloader` 类
2. 实现 `ModelStorage` 类
3. 修改 `model_repository.dart` 支持新路径

**Phase 2: UI 实现** (1-2 天)
4. 创建 `ModelDownloadScreen`
5. 实现进度条、暂停/恢复按钮
6. 集成到 app 启动流程

**Phase 3: 高级特性** (1-2 天)
7. 后台下载支持
8. 断点续传
9. WiFi 检测与提示

**Phase 4: 测试与优化** (1 天)
10. 真机测试下载流程
11. 优化下载速度
12. 错误处理与重试逻辑

**总计**: 5-8 天

### 风险与挑战

1. **下载稳定性**
   - 网络中断、超时
   - 解决：断点续传 + 重试机制

2. **存储空间**
   - 740MB 对部分用户可能太大
   - 解决：下载前检查空间，提供清理功能

3. **下载源可用性**
   - HuggingFace 在中国可能被墙
   - 解决：提供多个下载源（GitHub、国内镜像）

4. **模型更新**
   - 新版本模型如何分发
   - 解决：版本号管理 + 增量更新（未来）

## 替代方案：预装模型的分发渠道

如果不想实现应用内下载，可以考虑：

1. **Google Play / App Store 作为 OBB  Expansion File**
   - Android: 使用 Expansion Files（最大 2GB）
   - iOS: 不支持

2. **第三方应用市场**
   - 中国：应用宝、华为应用市场
   - 提供"完整包"下载（包含模型）

3. **官网下载**
   - 提供 APK/IPA 下载
   - 包含模型的大包

但这些方案都不如应用内下载灵活。

## 结论

**推荐方案**: 应用内下载系统

**理由**:
1. ✅ 用户体验最佳（小 app + 按需下载）
2. ✅ 灵活性高（支持更新、回滚）
3. ✅ 符合行业标准（ChatGPT、Gemini 等都这样做）
4. ✅ 长期维护成本低

**下一步**:
1. 开始实现 `ModelDownloader` 基础设施
2. 创建下载引导页 UI
3. 集成到 app 启动流程
4. 真机测试验证

这是 QwenEcho 从"demo"走向"产品"的关键一步。
