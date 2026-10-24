# DPKViewer 工程设计

面向 Windows 的桌面工具，用 C++ / MFC 实现，对《寻仙》DPK 包（`whpackage1.0`）提供**浏览、解包、生成**三项能力。
包的格式定义与读写语义以 `dpk.py`（`docs/DPK_FORMAT.md`）为唯一依据。

---

## 1. 目录与分层

```
DPKViewer/
├─ DPKViewer.sln              Visual Studio 解决方案
├─ DPKViewer.vcxproj          工程文件（Win32 / x64，Unicode，静态 MFC）
├─ DPKViewer.vcxproj.filters
├─ docs/
│  ├─ DPK_FORMAT.md           包格式规范（由 dpk.py 复刻整理）
│  └─ DESIGN.md               本文件
├─ res/
│  ├─ resource.h              资源 ID
│  ├─ DPKViewer.rc            菜单 + 图标 + 版本信息
│  └─ DPKViewer.ico           程序图标
├─ src/DPKViewer/
│  ├─ Core/                   「格式层」：与界面完全解耦，可独立编译测试
│  │  ├─ DpkTypes.h           条目结构、进度/结果回调定义
│  │  ├─ Whsc.h/.cpp          whsc1.0 流编解码（flags、MD5 校验、压缩、加密）
│  │  ├─ Des.h/.cpp           DES 变体（CBC 零 IV + 尾块混淆）
│  │  ├─ Md5.h/.cpp           自包含 MD5
│  │  ├─ Zlib.h/.cpp          自包含 inflate / deflate
│  │  ├─ DpkReader.h/.cpp     包头解析、块链读取、目录树/名表解码、解包
│  │  ├─ DpkWriter.h/.cpp     块分配、目录树/名表构造、打包写出
│  │  ├─ FileIO.h/.cpp        文件读写、递归枚举、相对路径
│  │  └─ StrUtil.h/.cpp       编码转换、路径工具、十六进制预览
│  └─ App/                    「界面层」
│     ├─ DpkViewerApp.h/.cpp  CWinApp：启动、命令行模式分流
│     ├─ MainFrame.h/.cpp     CFrameWnd：主窗口与三条功能链路
│     ├─ Cli.h/.cpp           /list、/extract、/pack 控制台模式
│     └─ AppUtil.h/.cpp       文件夹选择器、控制台附加、消息输出
└─ tests/                     核心验证（控制台，不依赖 MFC）
   ├─ TestCore.cpp            自检 + 往返 + 损坏包边界
   ├─ tools/build_and_test.bat
   ├─ tools/verify.py         与 dpk.py 的双向交叉验证
   ├─ tools/make_test_package.py 按 dpk.py 合成参考包与样本
   └─ tools/reference/dpk.py  参考实现副本
```

分层原则：`Core` 不引用任何 MFC 头，界面层只调用 `Core` 的公开接口；测试与命令行模式复用同一份 `Core`，避免「界面能跑但格式不对」。

---

## 2. 程序类型与资源

- 程序类型：**单文档风格的自由框架窗口**（`CFrameWnd` 派生），控件全部**程序化创建**，不使用对话框模板；`res/DPKViewer.rc` 只提供菜单、图标与版本信息，工程可在任意 VS 2022 环境重新编译。
- 字符集：**Unicode**；MFC 使用方式：**静态链接**（`UseOfMfc=Static`），分发的 exe 不依赖 MFC 运行库 DLL。
- 目标平台：**Win32 + x64**；语言标准 `/std:c++17`；源文件按 `/utf-8` 编译，源码内的中文仅出现在字符串字面量中。

---

## 3. 窗口布局

```
┌──────────────────────────────────────────────────────────────┐
│ 文件(F)  操作(A)  帮助(H)                                     │  菜单
├──────────────────────────────────────────────────────────────┤
│ [打开包…] [解包到目录…] [生成 DPK 包…] [关于]                  │  按钮行
│ 筛选: [____________]  [x]压缩(L9)  [x]加密  [ ]沿用模板包头     │  选项行
├──────────────────────────────────────────────────────────────┤
│ 条目路径            │ 根块 │ 块数 │ 明文 │ 存储 │ 标志 │ 校验  │  条目列表
│ 场景/地表/xx.dds    │  12  │  3   │ 2.1K │ 900B │ 压+密 │ OK   │  (CListCtrl)
│ …                                                             │
├──────────────────────────────────────────────────────────────┤
│ 详情：路径 / 根块 / 块链 / flags 解码 / 明文与存储长度 / MD5     │  详情面板
│ 内容预览（可直接显示的首段文本，否则十六进制）                  │  (CEdit 只读)
├──────────────────────────────────────────────────────────────┤
│ [进度条]                        │ 就绪                       │  进度 + 状态栏
└──────────────────────────────────────────────────────────────┘
```

控件与 ID：

| ID | 类型 | 说明 |
| --- | --- | --- |
| `IDC_BTN_OPEN` | 按钮 | 打开 DPK 包 |
| `IDC_BTN_EXTRACT` | 按钮 | 解包当前包到目录 |
| `IDC_BTN_PACK` | 按钮 | 用文件夹内容生成 DPK 包 |
| `IDC_BTN_ABOUT` | 按钮 | 关于/格式说明摘要 |
| `IDC_EDIT_FILTER` | 单行编辑 | 按路径子串过滤条目列表 |
| `IDC_CHK_COMPRESS` | 复选框 | 生成时压缩（zlib level 9） |
| `IDC_CHK_ENCRYPT` | 复选框 | 生成时加密（PackagePassword） |
| `IDC_CHK_TEMPLATE` | 复选框 | 沿用当前已打开包的 1024 字节包头 |
| `IDC_ENTRY_LIST` | 报表视图列表 | 条目浏览 |
| `IDC_DETAIL` | 只读多行编辑 | 条目详情与内容预览 |
| `IDC_PROGRESS` | 进度条 | 解包/生成进度 |
| `IDC_STATUS` | 状态栏 | 包信息、条目数、结果 |

窗口最小尺寸 720×480，可自由缩放；列表与详情随窗口拉伸。

---

## 4. 三条操作路径

### 4.1 浏览

1. 「打开包…」→ `CFileDialog` 选择 `.dpk` → `Reader::Open`（校验魔数 `whpackage1.0`、长度、`0x20` 口令标记）+ `Reader::LoadEntries`（读目录树与名表虚拟文件，解出全部条目）。
2. 条目填入列表：路径、根块号、块链长度、明文大小、存储大小、标志（压缩/加密/ContentPassword）、MD5 校验结果。包信息（大小、总块数、treeRoot、namesRoot）进入状态栏。
3. 选中条目 → 详情面板显示元数据，并读取该条目的明文首段：可判定为文本则直接显示，否则显示十六进制转储。
4. `IDC_EDIT_FILTER` 输入即过滤（子串匹配，不区分 ASCII 大小写）。

### 4.2 解包

1. 「解包到目录…」→ 选择目标文件夹（要求先打开包）。
2. `Reader::ExtractAll` 按条目写出文件：逐条建目录、解码 whsc 流、校验 MD5 与长度后落盘；单条失败不影响其余条目。
3. 进度条实时更新，可取消；结束弹窗汇总「成功 N / 失败 M」并列出失败条目与原因，同时写状态栏。
4. 路径处理：条目路径经 `IsSafeRelativePath` 过滤，拒绝绝对路径与 `..` 逃逸，避免写出到目标目录之外。

### 4.3 生成

1. 「生成 DPK 包…」→ 选择源文件夹 → 选择输出 `.dpk` 路径（`CFileDialog` 保存）。
2. 选项取自主窗口复选框：压缩（level 9）、加密、沿用模板包头（勾选时要求已打开包，复制其块 0 后仅改写 0x84/0x88）。
3. `Writer::PackFolder` 递归枚举源文件 → 排序 → 逐条编码 whsc 流并分配块链 → 构造目录树、路径名表 → 回填包头 → 补齐到 1024 整数倍。
4. 结束弹窗汇总「写入 N 条、跳过 M 条（路径非法）、明文总量、包大小、总块数」，状态栏显示输出路径。

---

## 5. 命令行模式

同一个 exe 支持无界面批处理，便于脚本化与自动化验证（GUI 子系统的 exe 通过 `AttachConsole(ATTACH_PARENT_PROCESS)` 回接父控制台）：

```
DPKViewer.exe /list    <包>                         列出条目
DPKViewer.exe /extract <包> <目录>                  解包
DPKViewer.exe /pack    <源目录> <输出包> [选项]      生成
选项：/level N  /nocompress  /noencrypt  /template <包>
附加：/log <文件>（同时把输出写入文件）
```

返回值：0 成功，1 失败（含参数错误）；失败原因逐行打印。命令行参数由 `CWinApp::InitInstance` 在创建主窗口前分流处理，处理完毕直接退出，不弹出任何窗口。

---

## 6. 异常与边界

- 损坏包：魔数不符、长度不足 1024、块链成环或 `prev`/`tail` 不一致、目录树出现非法首字节、名表偏移越界、whsc 魔数不符、长度与 `plainSize` 不符、MD5 不匹配，均在读取时以错误文本返回，界面弹窗提示，命令行打印 `FAIL`。
- 大文件：块链按需读取，列表不预读内容；仅选中条目时才读取其内容用于预览。
- 空包：允许生成不含任何条目的包（`Writer::PackEmpty`）。
- 覆盖写：解包前不删除目标目录既有内容，同名文件直接覆盖；生成前若目标已存在，由 `CFileDialog` 的覆盖确认把关。