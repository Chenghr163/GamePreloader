# GamePreloader V2.0 正式版 - 游戏加载预处理工具（手机级 / 多引擎）

Windows 游戏加载预处理工具（C++/Win32）。减少磁盘 IO、着色器加载与调度造成的卡顿停顿；支持 UE5（含《黑神话：悟空》）、Unity、ForzaTech《极限竞速：地平线》系列。

## 快速开始
- **只想直接用（无需编译）**：下载 [`GamePreloader_V2.0_Release.zip`](GamePreloader_V2.0_Release.zip)，解压后保持 `park` 文件夹与 `GamePreloader.exe` 同一层级即可运行。
- **想自己编译 / 查看源码**：本仓库根目录即为完整源代码（`src/`、`build.bat`、`CMakeLists.txt`），编译流程见文末。

基于 V1.8，在**不更改任何旧设置**的前提下：新增**引擎无关的通用大文件预读**，兼容 Unity《午夜轮班》、ForzaTech《极限竞速：地平线6》等**非 UE 游戏**，并进一步优化处理效果。

## V2.0 新增

### 1. 引擎无关的"通用大文件预读"（多引擎兼容）
此前深度解析只认 UE5。本版新增一条**与引擎无关的预读通道**，对任何监控到的游戏都生效：
- 自动定位常见数据目录：
  - **Unity**（《午夜轮班 Shift At Midnight》）：`*_Data` 目录下的 `sharedassets*.assets`、
    `level*`、`globalgamemanagers`、`resources.assets`、`*.bundle`、`*.resS`（含**无扩展名**的序列化大文件）；
  - **ForzaTech**（《极限竞速：地平线6》）：`media` / `mediaPC` 目录下的大体积资源（`.bin/.model/...`，含子目录）；
  - 其它引擎：按"大文件"通用规则兜底扫描。
- 把这些大资源**整体流式读入缓存并预热 Windows 文件缓存**，运行期命中文件缓存、减少随机 IO 卡顿。
- 受控 BFS（限深度 4、限文件数、跳过符号链接与可执行文件），不会暴力遍历整个 167GB 目录。

### 2. 处理效果优化：真实资源优先于零填充
- 预读顺序调整为：UE5 核心资源 → IoStore `.ucas` → UE 其它资源 → 整包兜底 →
  **通用大文件（真实资源）** → 最后才零填充。
- 即缓存尽量由**真实游戏资源**构成，零填充只在真实资源确实不足时兜底，提升缓存的实际价值。
- UE5（含《黑神话：悟空》）的 Pak / IoStore 深度解析路径完全保留、行为不变。

### 3. 不更改任何旧设置
- 悬浮窗、右键菜单、配置项、监控逻辑、`park` 目录结构、独显检测与缓存上限（独显 20GB / 核显 40GB）、
  缓存 ≥5GB、退出自动清缓存、系统级加速等**全部保持原样**。
- 通用预读能力内置在主程序，**无需新增 DLL**；`park/UE5PakParser.dll` 仍只负责 UE5 解析。

## 已验证（端到端）
用模拟游戏（含 Unity `*_Data` 与 Forza `media` 目录 + 常驻游戏进程）实测：
- 程序在**监测到游戏进程后**才开始处理（游戏未启动不处理）；
- 正确发现并预读了无扩展名的 `level0/globalgamemanagers`、`sharedassets0.assets`、
  `media/stables.bin`、子目录 `cars/testcar.model`，随后才补足到 5GB；
- 缓存条目类型与大小正确，文件 ≥5GB。

## 能力边界（务必阅读）
本工具减少**磁盘随机 IO、着色器加载、系统调度、资源流送**造成的卡顿与掉帧停顿，让加载更快、停顿更少。

**它不能提升 GPU 算力 / 帧率上限。** 大型复杂场景的原生帧率瓶颈在 GPU 渲染，预处理无法把核显变成独显。
无独显机器要真正流畅跑 3A，还需在游戏内开启 **FSR / XeSS 超分**、降低画质/渲染分辨率——本工具与之互补。

对 Unity / ForzaTech，本工具**不逆向其专有资源格式、不破解加密、不注入、不 Hook**，只做引擎无关的
"整文件只读预读 + 文件缓存预热"，因此不触碰反作弊红线。

## 目录结构（本仓库 / 发布包）
```
├── src/                     # 完整 C++ 源代码（15 个文件）
├── park/
│   └── UE5PakParser.dll     # （发布包内）UE5(Pak/IoStore) 解析库；park 专放各引擎解析库
├── app.ico / app.rc         # 程序图标与资源
├── build.bat                # 一键编译脚本
├── CMakeLists.txt           # CMake 构建
└── GamePreloader_V2.0_Release.zip   # 预编译程序包（exe + park 解析库 + 运行时）
```

## 使用
1. 解压发布包后**保持 `park` 文件夹与 `GamePreloader.exe` 同一层级**，不要只单独拎出 exe。
2. 右键悬浮窗 → "添加游戏（浏览文件）..." → 选中游戏 exe（UE5 / Unity / ForzaTech 均可）。
3. 启动游戏进入加载阶段后自动处理；UE5 走 Pak/IoStore 深度解析，Unity/ForzaTech 走通用大文件预读。
4. 游戏退出自动恢复电源计划；右键"退出程序"自动清除缓存。

## 从源码编译（完整流程）
### 方式一：一键编译（推荐）
1. 安装 **Visual Studio 2022（或 Build Tools）**，勾选"使用 C++ 的桌面开发"与对应 **Windows SDK**。
2. 确认 `build.bat` 中 `vcvarsall.bat` 路径与本机一致。
3. 双击 `build.bat`，依次：
   1. `/LD /DGAMEPAK_EXPORTS` 把 `src/PakParser.cpp + src/Common.cpp` 编成 `park\UE5PakParser.dll`，生成导入库 `bin\UE5PakParser.lib`；
   2. 编译主程序（不含 PakParser.cpp；通用预读在 `src/Preprocessor.cpp` 内，自动编入）；
   3. `rc.exe` 编译 `app.rc` 图标；
   4. 链接 `GamePreloader.exe`，`/DELAYLOAD:UE5PakParser.dll + delayimp.lib`，并链接 `dxgi.lib`。
4. 发布时把 exe、`park\UE5PakParser.dll`、VC 运行时 DLL、cache/logs/config 目录放在一起。

### 方式二：CMake
```cmd
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

### 关键选项
- C++17、UNICODE、`/utf-8`、`WIN32_LEAN_AND_MEAN`、`NOMINMAX`；解析 DLL 额外定义 `GAMEPAK_EXPORTS`。
- 链接库：`psapi shell32 user32 gdi32 advapi32 ole32 comdlg32 powrprof dxgi delayimp`，
  加 `/DELAYLOAD:UE5PakParser.dll`。
- 要求 VS2019/2022 + Windows SDK，**无第三方依赖**。
