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
//  ★★★ 任务一：相位迁移 ★★★
// ============================================================================
//  答辩会问到的四个点：
//   1) 计时用减法 now - g_start >= g_dur，免疫 millis() 约 49.7 天的溢出。
//   2) 两个方向都必须经过全红清空期（见图 1.3）：
//          NS_GREEN → NS_YELLOW → ALL_RED
//          EW_GREEN → EW_YELLOW → ALL_RED
//      只有一边有全红是不对称的；真实路口也是每个黄灯后都要清空。
//   3) ALL_RED 之后该放行哪个方向？5 个状态本身记不住这件事，
//      所以用 g_nextGreen 在"进入全红"那一刻把答案存下来。
//   4) 迁移时四件事的顺序：g_start → g_phase → g_dur → 刷新灯。
//      写反是经典 bug（表现为灯卡住、或跳变得飞快）。
// ============================================================================
static Phase g_nextGreen = PHASE_EW_GREEN;   // 全红结束后要放行的方向

void phasesUpdate(unsigned long now) {
  if (now - g_start < g_dur) return;         // 没到点就立刻返回，不阻塞任何人

  // --- 第 1 步：决定下一个相位 ---
  Phase next;
  switch (g_phase) {
    case PHASE_NS_GREEN:   next = PHASE_NS_YELLOW; break;
    case PHASE_NS_YELLOW:  next = PHASE_ALL_RED;   g_nextGreen = PHASE_EW_GREEN; break;
    case PHASE_EW_GREEN:   next = PHASE_EW_YELLOW; break;
    case PHASE_EW_YELLOW:  next = PHASE_ALL_RED;   g_nextGreen = PHASE_NS_GREEN; break;
    case PHASE_ALL_RED:    next = g_nextGreen;     break;  // 走"进入全红时"存下的方向
    default:               next = PHASE_NS_GREEN;  break;  // 防御：正常不会到这里
  }

  // --- 第 2 步：迁移，这四行的顺序不能换 ---
  g_start = now;                             // ① 先重置计时基准
  g_phase = next;                            // ② 再换相位

  switch (g_phase) {                         // ③ 设本相位的时长
    case PHASE_NS_GREEN:
    case PHASE_EW_GREEN:   g_dur = GREEN_BASE; break;
    case PHASE_NS_YELLOW:
    case PHASE_EW_YELLOW:  g_dur = T_YELLOW;   break;
    default:               g_dur = T_ALL_RED;  break;
  }

  phasesApplyLights();                       // ④ 立刻刷新灯

  // --- 第 3 步：日志，答辩演示时一眼能看出状态在动 ---
  Serial.print('['); Serial.print(now); Serial.print(F("] "));
  Serial.println(phasesName(g_phase));
}
