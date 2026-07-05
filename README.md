# 幻想乡聊天室 · C++ 词典机器人

这是将 `ggg0057/chat_room` 与正确的词典项目 `ggg0057/dictionary` 整合后的 Windows 小程序。聊天室仍是主体，词典既可作为常驻机器人响应消息，也有独立查词页面。后端完全使用 C++17 与 WinSock，不依赖 Python。

## 主要功能

- 游客模式，以及 SQLite 持久账户注册/登录；密码使用 PBKDF2-SHA256（120,000 次）加盐哈希。
- 公共聊天室、用户创建房间、私聊、未读数量、在线成员和词典机器人。
- 独立查词页面，支持英译中和中文反向检索。
- 聊天记录与个人查词记录，可按关键词查询。
- 在线音乐播放器：浏览曲库、选择音乐、开始、暂停、拖动进度和调节音量。
- 管理员音乐接口：上传及删除 `mp3 / wav / ogg / m4a / aac`，单文件最大 25 MB。
- 独立响应式网页版，与小程序共享在线成员、聊天室消息和词典机器人。
- 小程序内置“单词雨挑战”：60 秒词义答题、生命值、连击加分、暂停和在线排行榜。
- 可选玻璃透明窗口与 Canvas 粒子烟花效果，开关状态自动保存在浏览器。
- 10 套用户提供的场景图片，可在界面左下角随时切换并自动记忆。

## Windows 运行

1. 完整解压 ZIP，不能直接在压缩包预览窗口内运行。
2. 双击 `start.bat`。
3. 保持服务器窗口开启；关闭窗口会停止聊天室。
4. 浏览器会自动打开。若未打开，请访问服务器窗口显示的地址，通常是 <http://127.0.0.1:8000>。

交付包已包含静态链接的 `chat_server.exe`，普通 Windows 电脑不需要安装编译器或 Python。启动器发现 8000 被占用时会自动尝试 8001-8010。

## 网页版与小程序互通

本机访问：

```text
小程序界面：http://127.0.0.1:8000/
独立网页版：http://127.0.0.1:8000/web
```

需要让同一 Wi-Fi 下的手机或其他电脑访问时，双击：

```text
start_website.bat
```

启动窗口会显示 `LAN website` 地址，例如 `http://192.168.1.20:8000/web`。其他设备打开这个地址即可进入。首次启动时若 Windows 防火墙询问网络访问权限，请仅允许“专用网络”。

小程序和网页版直接连接同一个 C++ 消息状态，因此可以互相看到加入提示、在线成员、普通消息和词典机器人回复。

如果需要真正的公网网站，应把程序部署到有公网 IP 的服务器，并在前面配置域名、HTTPS 反向代理和防火墙；不要直接把管理员接口暴露在明文公网。

## 音乐播放器与管理员接口

普通用户登录后打开“在线音乐”，即可选择管理员已经上传的音乐，并进行开始、暂停、进度和音量控制。音乐保存在 `data/music`，重启服务后仍会保留。

音乐页面下方的“管理员音乐接口”可上传和删除音乐。默认管理员口令是：

```text
gensokyo-admin
```

正式使用前建议修改口令：

```powershell
powershell -ExecutionPolicy Bypass -File .\start.ps1 -AdminPassword "你的新口令"
```

接口如下：

```text
GET    /api/music
GET    /api/music/file?name=文件名
POST   /api/admin/music?name=文件名
DELETE /api/admin/music?name=文件名
```

上传与删除请求需携带 `X-Admin-Password` 请求头。当前程序面向本机或可信局域网；若部署到公网，应在前面增加 HTTPS 反向代理，不要通过明文 HTTP 发送管理员口令。

## 在线小游戏

登录小程序后选择左侧“游戏”，即可运行“单词雨挑战”：

- 每局 60 秒，拥有 3 点生命值。
- 选择英文单词的正确中文释义；连续答对有连击加分。
- 支持开始、重新开始、暂停和继续。
- 游戏结束后自动把成绩提交到 C++ 服务，并显示所有在线用户共享的前 10 名排行榜。

排行榜保存在 SQLite 数据库中，服务重启后仍会保留。

## 账户、房间和私聊

- **游客**：无需密码，可以进入公共房间和发起私聊；不能冒用已注册用户名，也不能创建房间。
- **注册账户**：可创建聊天室，持久保存房间、聊天记录、查词记录、排行榜和未读数量。
- **私聊**：点击右侧联系人或在线成员即可进入；其他用户无法通过历史查询看到双方私聊内容。
- **默认房间**：公共大厅、英语角、音乐茶室。

数据库文件会在首次运行时自动创建为 `data/chat.db`，并启用 WAL、外键和并发等待设置。

## 界面效果

登录后点击左下角“场景”，可以在“界面效果”中启用：

- **窗口透明化**：降低主窗口、侧栏和卡片的不透明度，让当前场景透过界面显示。
- **粒子烟花**：在界面上层播放轻量 Canvas 烟花动画，不影响按钮和输入操作。

两个开关均保存在当前浏览器的本地设置中。烟花关闭后会立即清空动画、粒子和定时器。

## 词典机器人

在公共大厅发送：

```text
@词典 network
/dict hello
查词 ability
查询 网络
```

机器人使用 `ggg0057/dictionary` 的原始 `dict.txt`，共读取 7,987 行，去重后加载 7,975 个词条；支持英译中和中文反向查找。

## 源码与构建

`src/main.cpp` 负责 WinSock HTTP、会话、房间与消息接口；`src/persistence.cpp` 负责 SQLite、账户密码、消息和未读状态。SQLite 3.53.3 官方 amalgamation 已放在 `src/vendor/sqlite`，构建时直接编译进 EXE，不要求用户安装数据库 DLL。

在安装 MinGW-w64 或 TDM-GCC 后运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

GitHub 仓库中的 SQLite 官方源码采用无损 gzip 分片保存，以适应上传接口限制；`build.ps1` 和 CMake 会在首次编译前自动、离线还原，无需额外下载。

也可使用 CMake 3.15+：

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --config Release
```

## 验证

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\smoke_test.ps1
```

测试会启动真实 C++ 进程，验证健康接口、游客、注册登录、PBKDF2 账户、多房间、私聊、未读清零、服务重启后的 SQLite 恢复、两个前端双向消息、游戏排行榜，以及音乐上传、Range 播放和删除。

## 结构

```text
chat_dictionary_app/
├─ chat_server.exe       # 已编译 C++ Windows 后端
├─ src/main.cpp          # C++17 / WinSock 服务端源码
├─ src/persistence.*     # SQLite 持久化与密码哈希
├─ src/vendor/sqlite/    # 官方 SQLite amalgamation
├─ data/dict.txt         # 正确词典仓库的原始词库
├─ data/music/           # 管理员上传的音乐文件
├─ static/               # HTML / CSS / JavaScript 窗口与 10 套场景图片
├─ tests/smoke_test.ps1  # 端到端冒烟测试
├─ build.ps1             # MinGW 编译脚本
├─ CMakeLists.txt
├─ start.ps1             # 端口检测与启动
├─ start.bat             # Windows 双击入口
└─ start_website.bat     # 局域网页版双击入口
```

账户、房间、消息、查词记录、排行榜和未读数量保存在 `data/chat.db`；上传音乐保存在 `data/music`。只有在线连接状态在服务重启后清空。
