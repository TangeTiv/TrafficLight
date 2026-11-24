#include <Arduino.h>
#include "pins.h"
#include "phases.h"

// ============================================================================
//  phases.cpp  ·  任务一：四相位状态机与灯输出        【甲 负责】
// ----------------------------------------------------------------------------
//  除 phasesUpdate() 外，本文件其余部分都已写好（属于基础设施，不用改）。
// ============================================================================

// --- 硬件映射 --------------------------------------------------------------
// 下面这张表的下标顺序，和这里 6 个引脚的顺序必须一一对应：
//   0=南北红  1=南北黄  2=南北绿  3=东西红  4=东西黄  5=东西绿
static const uint8_t kLights[6] = {
  PIN_NS_R, PIN_NS_Y, PIN_NS_G,
  PIN_EW_R, PIN_EW_Y, PIN_EW_G
};

// --- 查表法：每个相位下六个灯的亮灭 -----------------------------------------
//  行 = 相位（顺序与 enum Phase 完全一致）
//  列 = 上面 kLights 的 6 个灯，1=亮 0=灭
//
//  用查表法而不是手写六行 digitalWrite 的原因：手写一定会漏掉某一颗灯，
//  而"漏掉一颗红灯"的直接后果就是两个方向同时绿灯 —— 那是本题最严重的逻辑错误。
//  查表法从根上杜绝了这件事：每一行都必须写满 6 个数。
static const uint8_t kTable[5][6] = {
  /* PHASE_NS_GREEN  */ { 0, 0, 1,   1, 0, 0 },   // 南北绿，东西红
  /* PHASE_NS_YELLOW */ { 0, 1, 0,   1, 0, 0 },   // 南北黄，东西红
  /* PHASE_EW_GREEN  */ { 1, 0, 0,   0, 0, 1 },   // 南北红，东西绿
  /* PHASE_EW_YELLOW */ { 1, 0, 0,   0, 1, 0 },   // 南北红，东西黄
  /* PHASE_ALL_RED   */ { 1, 0, 0,   1, 0, 0 }    // 全红（清空期 / 行人过街插入点）
};

static const char* const kNames[5] = {
  "NS_GREEN", "NS_YELLOW", "EW_GREEN", "EW_YELLOW", "ALL_RED"
};

// --- 内部状态 --------------------------------------------------------------
static Phase         g_phase = PHASE_NS_GREEN;
static unsigned long g_start = 0;              // 本相位开始的时刻
static unsigned long g_dur   = GREEN_BASE;     // 本相位的总时长

// ============================================================================
//  已实现：生命周期与输出
// ============================================================================

void phasesInit() {
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(kLights[i], OUTPUT);
    digitalWrite(kLights[i], LOW);
  }
  g_phase = PHASE_NS_GREEN;
  g_dur   = GREEN_BASE;
  g_start = millis();
  phasesApplyLights();
}

void phasesApplyLights() {
  const uint8_t* row = kTable[(uint8_t)g_phase];
  for (uint8_t i = 0; i < 6; i++) {
    digitalWrite(kLights[i], row[i] ? HIGH : LOW);
  }
}

void phasesSelfTest() {
  Serial.println(F("[selftest] lighting 6 LEDs in order: NS R/Y/G -> EW R/Y/G"));
  for (uint8_t i = 0; i < 6; i++) {
    digitalWrite(kLights[i], HIGH);
    // 这里用 delay() 是允许的：它只在开机时跑一次，不参与任何循环计时。
    // ★ 但在 loop() 里、以及任何任务的计时逻辑里，delay() 都是禁止的。★
    delay(500);
    digitalWrite(kLights[i], LOW);
  }
  Serial.println(F("[selftest] done. LED dark or wrong order? check wiring first"));
}

// ============================================================================
//  已实现：状态查询与控制
// ============================================================================

Phase phasesCurrent() {
  return g_phase;
}

unsigned long phasesStartMillis() {
  return g_start;
}

unsigned long phasesDuration() {
  return g_dur;
}

unsigned long phasesRemainMs(unsigned long now) {
  unsigned long elapsed = now - g_start;         // 减法写法，免疫 millis() 溢出
  return (elapsed >= g_dur) ? 0UL : (g_dur - elapsed);
}

void phasesSetDuration(unsigned long ms) {
  if (ms < 100UL) ms = 100UL;                    // 防御：别让时长被算成 0
  g_dur = ms;
}

void phasesGoto(Phase p, unsigned long durMs, unsigned long now) {
  g_phase = p;
  g_dur   = (durMs < 100UL) ? 100UL : durMs;
  g_start = now;
  phasesApplyLights();
}

const char* phasesName(Phase p) {
  uint8_t i = (uint8_t)p;
  return (i < 5) ? kNames[i] : "?";
}

// ============================================================================
//  ★★★ 任务一：要你实现的就是下面这一个函数 ★★★
// ============================================================================
void phasesUpdate(unsigned long now) {
  // --------------------------------------------------------------------------
  //  TODO（任务一）：实现相位迁移
  //
  //  目标：到点就切换到下一个相位；没到点就立刻返回，什么都不做。
  //
  //  第 1 步 · 判断到点了没
  //      用下面这种"减法"写法，它天生免疫 millis() 约 49.7 天的溢出：
  //              if (now - g_start >= g_dur) { ... }
  //      务必不要写成  if (now > g_start + g_dur)  —— 那种写法在溢出那一刻会失效。
  //
  //  第 2 步 · 迁移顺序（一条直线，别绕圈）
  //          NS_GREEN  → NS_YELLOW → ALL_RED
  //          EW_GREEN  → EW_YELLOW → ALL_RED
  //          到这里再回到 NS_GREEN / EW_GREEN 交替。
  //      也就是说：南北绿之后不能直接切东西绿，中间必须经过「黄灯 → 全红」。
  //
  //  第 3 步 · 每次迁移都要按这个顺序做四件事（顺序写反是个经典 bug）
  //          g_start = now;                     // 先重置计时基准
  //          g_phase = 下一个相位;               // 再换相位
  //          g_dur   = 该相位的默认时长;         // 绿灯先给 GREEN_BASE，配时会改写
  //          phasesApplyLights();               // 立刻刷新灯
  //      时长取值：黄灯 → T_YELLOW，全红 → T_ALL_RED，绿灯 → GREEN_BASE
  //
  //  第 4 步 ·（强烈建议）迁移时打一行串口日志，答辩演示非常有用
  //          Serial.print('['); Serial.print(now); Serial.print(F("] "));
  //          Serial.println(phasesName(g_phase));
  //
  //  自查：写完之后，任意时刻都不应出现"南北绿 + 东西绿"同时亮。
  //        如果出现了，检查 g_phase 是否被赋成了越界的值（比如算错下标）。
  // --------------------------------------------------------------------------
}
