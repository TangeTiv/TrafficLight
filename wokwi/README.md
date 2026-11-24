# Wokwi 仿真（VS Code 插件路线）

> **环境还没配好（没装扩展 / 没装 `arduino-cli` / 仿真窗口一片空白）？**
> 先看 **[`../SETUP.md`](../SETUP.md)** —— 那是从零开始的配置书，带逐步「通过信号」和验收清单。
> 本文件只讲**已经能跑起来之后**的日常操作、电路说明和报错处理。

> 电路图 `diagram.json` 和配置 `wokwi.toml` 都放在**仓库根目录**（和 `TrafficLight.ino` 同级），
> 因为 Wokwi 扩展要求这两个文件必须在 VS Code 打开的那个文件夹里。
> 本文件只是说明文档，放在这里不影响任何东西。

## 为什么走这条路

| | VS Code 插件 | 网页版 |
| --- | --- | --- |
| 改完代码看效果 | `Ctrl+Shift+B` 再重跑 | 要重新粘文件 |
| 多文件工程 | 直接用本地文件 | 12 个文件手工粘 |
| 断点调试 | 支持（GDB） | 不支持 |

## 一次性配置

**前提**：`arduino-cli` 已装在 `C:/Users/谭/tools/arduino-cli/`，AVR 内核 `arduino:avr@1.8.8` 已就绪。
（已经在你们这台机器上装好并编译通过了，这一步不用重做。）

1. **装扩展**：VS Code 里搜 `Wokwi for VS Code` → Install
2. **激活 License**：按 `F1` → 输入 `Wokwi: Request a new License` → 确认在浏览器打开 →
   点 **GET YOUR LICENSE** → 登录 Wokwi 账号（免费注册）→ 浏览器和 VS Code 各确认一次 →
   VS Code 提示 `License activated for ...`
   > 免费账号够用，这个 license 是扩展的授权，和"项目公开/私有"无关。
3. **打开正确的文件夹**：在 VS Code 里 `文件 → 打开文件夹`，选
   **`D:\我的作业\单片机\TrafficLight`**（选这个目录本身，**不要选它的上一层**）
   —— 选错了会报 `wokwi.toml configuration file not found in workspace`
4. **编译**：`Ctrl+Shift+B`（任务已配好，等价于下面这行）
   ```bash
   arduino-cli compile --fqbn arduino:avr:uno --output-dir build .
   ```
   通过信号：`Sketch uses 7048 bytes (21%) of program storage space`
5. **启动仿真**：`F1` → `Wokwi: Start Simulator`

## 日常工作流（三步，别漏第三步）

```
改代码 → Ctrl+Shift+B（编译） → F1: Wokwi: Start Simulator（或点仿真窗口的重启）
```

> ⚠ **Wokwi 不会编译你的代码。** 它只是读取 `build/TrafficLight.ino.hex` 去跑。
> 所以最常见的困惑是"我明明改了代码，仿真里却没变" —— **90% 是忘了按 `Ctrl+Shift+B`**。
> 养成顺序：改完先看编译有没有过，再启动仿真。

## 编译时那两个警告是正常的

```
phases.cpp:118:33: warning: unused parameter 'now'
pedestrian.cpp:78:43: warning: unused parameter 'now'
```

这不是错误，而是**"这两个 TODO 还没写"的进度指示器**：
- `phasesUpdate(now)` 函数体空的 → 参数没用上
- `handleEmergency(now)` 还没实现 → 参数没用上

**你把任务一和任务二的代码填进去，这两条警告就自动消失。**
哪天编译输出变干净了，说明这两个函数真的动了。这是一个很省事的自检信号。

## 报错对照表

| 报错 | 原因 | 怎么办 |
| --- | --- | --- |
| `wokwi.toml configuration file not found in workspace` | 打开的文件夹不对 | 打开 `TrafficLight` 本身；或 `F1` → `Wokwi: Select Config File` |
| `firmware binary build/... not found in workspace` | 还没编译，或编译失败 | 先 `Ctrl+Shift+B`，确认 `build/` 里有 `.hex` |
| `no diagram.json file in ...` | 文件被移走了 | 确认根目录有 `diagram.json`；没有就从 git 恢复 |
| 仿真窗口一片空白 / 一直转圈 | License 没激活 | `F1` → `Wokwi: Request a new License` |
| 串口没输出 | 仿真标签页被切到后台，Wokwi 会暂停 | 把仿真标签页保持在前台 |

## 仿真里的三个操作

| 要做的事 | 怎么做 |
| --- | --- |
| **改"车流量"** | 点超声波元件 → 会弹出 **distance 滑块**，拖它改距离（2–400cm）。任务三的动态配时就靠它 |
| **按行人键** | 按键盘 **`P`**，或鼠标点绿色按键 |
| **按紧急键** | 按键盘 **`E`**，或鼠标点红色按键 |

> 按键的 `bounce` 属性保留为 `"1"`（**模拟真实抖动**）。
> 这是有意的：任务二要求写消抖，如果仿真里的按键不抖，你就永远测不出消抖有没有写对。
> 想验证效果，把 `diagram.json` 里两个按键的 `"bounce": "1"` 改成 `"0"` 再跑一遍，
> 两次现象一对比，就是报告里一张现成的对照数据。

## 退路：网页版（扩展装不上时用）

1. https://wokwi.com/ → GitHub 账号登录 → **+ New Project** → **Arduino Uno**
2. 点右上角 `<>` 打开文件列表，把根目录 `diagram.json` 的内容**整个覆盖**原文件
3. 新建同名文件，把 12 个文件（`sketch.ino` 贴 `TrafficLight.ino`、`pins.h`、5 组 `.h`/`.cpp`）
   逐个粘进去，文件名必须和本地完全一致，`#include` 一个字都不要改
4. 点 ▶ 运行

网页版**和本地是两份代码**，所以只允许一个方向：
**本地改 → 粘到网页版看效果**，不能反过来。日常只需粘你改动的那一个文件。

## 硬件到货之后

仿真的定位变成"回归测试"：改完代码先在仿真里跑一遍（快、不会烧板子），
确认逻辑没错，再上传到实物验证电气部分。**先仿真后实物，返工量小一个数量级。**

---

*`diagram.json` 是普通文本，改 `connections` 就能改电路。
元件名和引脚名的合法写法见 https://docs.wokwi.com/parts/ ，**拼错一个字母整个图就打不开**。*
