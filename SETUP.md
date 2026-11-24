# 环境配置书 · 给乙（Windows / VS Code + Wokwi）

> 目标：从一台干净的电脑开始，做到「改代码 → 按 `Ctrl+Shift+B` → 启动仿真 → 看见灯在动」。
> **全程不需要买任何硬件。**
>
> 预计耗时 **40~60 分钟**，大头是下载（约 100 MB）。
> 每一步都给了「通过信号」，没看到就别往下走。

---

## 0 · 先确认你有这些东西

| 检查项 | 怎么查 | 没有的话 |
| --- | --- | --- |
| Windows 10 / 11 | 设置 → 系统 → 关于 | 换台电脑，或问甲要 Mac 版的差异 |
| **Git** | 终端敲 `git --version` | https://git-scm.com/download/win ，装完重启终端 |
| **VS Code** | 开始菜单搜 code | https://code.visualstudio.com/ |
| **GitHub 账号** | 能登录 github.com | 注册一个，用户名告诉甲 |
| 约 100 MB 空闲磁盘 | — | — |

> **最重要的一件事：让甲把你加成 Collaborator。**
> 仓库 · Settings → Collaborators → Add people → 填你的 GitHub 用户名。
> 不加的话你能 clone 但**推不上去**，后面所有提交都会失败。
> （仓库是公开的，所以只读克隆不用授权；要写就必须加。）

---

## 1 · 克隆仓库

选一个你放作业的目录，比如 `D:\我的作业\`：

```bash
cd "D:/我的作业"
git clone https://github.com/TangeTiv/TrafficLight.git
cd TrafficLight
```

**通过信号**：目录里能看到 `TrafficLight.ino`、`pins.h`、`diagram.json`、`wokwi.toml`。

**接着配提交身份**（不配的话，你的提交会显示成别人的名字，或者报错）：

```bash
git config user.name  "你的名字"
git config user.email "你的GitHub隐私邮箱"
```

> 隐私邮箱在 GitHub → Settings → Emails → *Keep my email addresses private*，
> 形如 `12345678+你的用户名@users.noreply.github.com`。
> **公开仓库里用真实邮箱会被爬虫采集**，用这个最省心。

---

## 2 · 装 arduino-cli

Wokwi 扩展**不编译代码**，它只读取编译好的 `.hex`。所以必须先有编译器。

1. 下载：https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip
2. 解压，把 `arduino-cli.exe` 放到：

```
%USERPROFILE%\tools\arduino-cli\arduino-cli.exe
```

> ⚠ **必须是这个路径。** 仓库里的 `.vscode/tasks.json` 用 `${userHome}/tools/arduino-cli/`
> 去找它，放到别处 `Ctrl+Shift+B` 会报 `spawn ... ENOENT`。
>
> Windows 上 `%USERPROFILE%` 一般就是 `C:\Users\你的用户名`。

**通过信号**：在终端里敲

```bash
"%USERPROFILE%/tools/arduino-cli/arduino-cli.exe" version
```

看到 `arduino-cli Version: 1.6.0` 之类就对了。

### （可选）把它加进 PATH，以后能直接敲 `arduino-cli`

**用 PowerShell**（不要用 `setx`，它有 1024 字符截断的老问题，PATH 长了会丢内容）：

```powershell
$dir = "$env:USERPROFILE\tools\arduino-cli"
$p = [Environment]::GetEnvironmentVariable('PATH','User')
if (($p -split ';') -notcontains $dir) {
  [Environment]::SetEnvironmentVariable('PATH', $p.TrimEnd(';') + ';' + $dir, 'User')
}
```

改完**重开终端和 VS Code** 才生效。不加也能用——`Ctrl+Shift+B` 走的是全路径。

---

## 3 · 装 Arduino AVR 内核

```bash
"%USERPROFILE%/tools/arduino-cli/arduino-cli.exe" core install arduino:avr
```

约 50 MB，看网速可能要几分钟。

**通过信号**：

```bash
"%USERPROFILE%/tools/arduino-cli/arduino-cli.exe" core list
```

输出里要有这一行：

```
arduino:avr 1.8.8  ...
```

> **如果你之前装过 Arduino IDE**，这一步大概率会直接告诉你 `already installed`——
> 内核在 `%LOCALAPPDATA%\Arduino15` 里，共用，不用重装。
> （注意：**不是** `~/.arduino15`，Windows 上别找错地方。）

---

## 4 · 装 VS Code 扩展并激活 License

1. VS Code 左侧「扩展」→ 搜 **`Wokwi for VS Code`** → Install
2. 按 `F1` → 输入 **`Wokwi: Request a new License`** → 确认在浏览器打开
3. 点 **GET YOUR LICENSE** → 登录或注册 Wokwi 账号（免费）
4. 浏览器确认一次，**VS Code 里再确认一次**

**通过信号**：VS Code 右下角弹出

```
License activated for 你的名字
```

> **没激活会怎样？** 仿真窗口一片空白或一直转圈，且不报错。
> 这是最容易被误判成"装坏了"的一步。

---

## 5 · 打开正确的文件夹（这一步选错，后面全卡）

`文件 → 打开文件夹` → 选 **`<你的作业目录>\TrafficLight`**（例如 `D:\我的作业\TrafficLight`）

> ⚠ **选这个目录本身，不要选它的上一层 `我的作业`。**
> `wokwi.toml` 和 `diagram.json` 必须正好在工作区根目录，
> 选上一层会报 `wokwi.toml configuration file not found in workspace`。

---

## 6 · 第一次编译 + 启动仿真

**编译**：`Ctrl+Shift+B`

任务已经配好了，等价于：

```bash
arduino-cli compile --fqbn arduino:avr:uno --output-dir build .
```

**通过信号**：

```
Sketch uses 7388 bytes (22%) of program storage space. Maximum is 32256 bytes.
Global variables use 547 bytes (26%) of dynamic memory, leaving 1501 bytes for local variables.
```

产物在 `build/TrafficLight.ino.hex`（这个目录不进 git，会自动生成）。

**启动仿真**：`F1` → `Wokwi: Start Simulator`

**通过信号**：出现电路图，六个灯依次各亮半秒，串口打印：

```
==================================================
 Smart Traffic Light   skeleton vs0.0
 Hardware: Arduino Uno   guide: Lab 1
==================================================
 All tasks are TODO. See the module files.
[selftest] lighting 6 LEDs in order: NS R/Y/G -> EW R/Y/G
[selftest] done. LED dark or wrong order? check wiring first
[i2c] scanning bus ...
[i2c] found device 0x27
[i2c] total 1 device(s)
[display] LCD disabled (USE_LCD1602 = 0 in pins.h), serial only
[boot] ready. Start with the TODO in phases.cpp
```

然后灯会按相位循环（任务一已经合并进 main 了）：

```
[ 11000] NS_YELLOW
[ 14000] ALL_RED
[ 15500] EW_GREEN
[ 23500] EW_YELLOW
[ 26500] ALL_RED
[ 28000] NS_GREEN
```

---

## 7 · 验收清单（8 条全过才算装好）

- [ ] `arduino-cli version` 有输出
- [ ] `core list` 里有 `arduino:avr 1.8.8`
- [ ] `Ctrl+Shift+B` 编译成功，无 error
- [ ] `build/TrafficLight.ino.hex` 存在
- [ ] 仿真窗口出现电路图（不是空白）
- [ ] 串口出现 `[boot] ready`
- [ ] 六个灯依次亮过一遍
- [ ] 灯开始**自动循环**（约 25 秒一轮）

只要有一条没过，就去下一节的排错表。

---

## 8 · 排错表（只列环境问题，代码问题看指导文档附录 B）

| 症状 | 原因 | 处理 |
| --- | --- | --- |
| `Ctrl+Shift+B` 报 `spawn ... ENOENT` | `arduino-cli.exe` 不在 `%USERPROFILE%\tools\arduino-cli\` | 按第 2 步放到这个路径，一个字符都别改 |
| 终端里 `arduino-cli` 不是内部或外部命令 | PATH 没加，或加了没重启 | 重开 VS Code；或改用全路径 |
| `wokwi.toml configuration file not found` | 打开的文件夹层级不对 | 打开 `TrafficLight` 本身 |
| `firmware binary build/... not found` | 没编译，或编译失败 | 先 `Ctrl+Shift+B`，确认 `build/` 里有 `.hex` |
| 仿真窗口空白 / 一直转圈 | License 没激活 | `F1` → `Wokwi: Request a new License` |
| 串口没有输出 | 仿真标签页切到后台了，Wokwi 会暂停 | 把仿真标签页保持在前台 |
| `core install` 卡住 / 超时 | 网络到 downloads.arduino.cc 慢 | 重试；或挂代理后重试 |
| `Permission denied (publickey)` | 用了 SSH 地址但没配 SSH key | 用 HTTPS 地址克隆（本书给的命令就是 HTTPS） |
| 推送时报 `403` / `Permission denied` | 甲没加你为 Collaborator | 让甲在 Settings → Collaborators 里加你 |

---

## 9 · 你每天的工作流

**第一天（现在就做）**：

```bash
git switch main && git pull
git switch -c feat/task2-pedestrian
```

**日常**：

```
改代码 → Ctrl+Shift+B → 重启仿真看效果 → 满意了再提交
```

> **改代码和看效果之间的那个 `Ctrl+Shift+B` 不能省。**
> Wokwi 只读 `.hex`，不编译。"我改了代码仿真没变"90% 是忘了这一步。

**每天开工前先同步一次**（冲突面积和同步频率成反比）：

```bash
git switch main && git pull
git switch feat/task2-pedestrian
git merge main
```

**做完了**：

```bash
git add -A
git commit -m "feat(pedestrian): 实现按键消抖与紧急车辆抢占"
git push -u origin feat/task2-pedestrian
```

然后去 GitHub 开 PR（base 选 `main`），PR 模板会把三条硬门槛列成勾选项。

---

## 10 · 你和甲的文件边界

| 你负责 | 甲负责 | **谁都不许随便动** |
| --- | --- | --- |
| `pedestrian.cpp`（任务二） | `phases.cpp`（任务一 ✅ 已完成） | `pins.h` |
| `trafficflow.cpp`（任务三测距） | `green_time.cpp`（任务三配时） | `TrafficLight.ino` |
| `display.cpp`（任务四） | | |

**要改 `pins.h` 或 `TrafficLight.ino`，先在群里说一句。**
两个人同时改同一份文件，最后一定会出现"谁覆盖了谁"，查这种事故比写代码还费时间。

---

## 11 · 三个只有踩过才知道的点

**① 仿真里的按键有快捷键。** 按 **`P`** 是行人请求，**`E`** 是紧急车辆。不用鼠标去点元件。

**② 按键的抖动是模拟出来的**（`diagram.json` 里 `bounce` 默认为开）。
这是好事——**你的消抖代码在仿真里就能验证出有没有写对**。
想对比效果：把两个按键的 `"bounce": "1"` 改成 `"0"` 再跑，两次现象一对比就是报告里的数据。

**③ 超声波元件点一下有距离滑块。** 拖它就是"车靠近/驶离"，任务三的动态配时靠它测，不用拿手挡。

---

## 12 · 编译时那两条警告，别去修

```
pedestrian.cpp:78:43: warning: unused parameter 'now'
```

**它不是错误，是"这个函数还没写"的进度指示器**——参数声明了但函数体是空的，所以没用上。

- 你把任务二的代码填进去，**这条警告就自动消失**
- 编译输出变干净 = 那个函数真的被动了

（另外几条 `new.cpp` / `hardware/avr` 的警告来自 Arduino 内核本身，跟你们无关，永远都在。）

---

## 附：如果你用的是 Mac

只有三处不同，其余完全一样：

1. `%USERPROFILE%\tools\arduino-cli\` → `~/tools/arduino-cli/`
2. 下载那个 zip 换成 `arduino-cli_latest_macOS_64bit.tar.gz`，解压后需要 `chmod +x`
   并处理一下 Gatekeeper（`xattr -d com.apple.quarantine arduino-cli`）
3. `tasks.json` 里的 `${userHome}` 会自动变成 `/Users/你的用户名`，不用改

---

*配置完成后，从 `pedestrian.cpp` 第 37 行那个 TODO 开始。卡在哪一步，把报错原文发群里，不要只说"不行了"。*
