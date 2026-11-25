#include <Arduino.h>
#include "pins.h"
#include "trafficflow.h"

// ============================================================================
//  trafficflow.cpp  ·  任务三：超声波测距与排队长度采集    【乙 负责】
// ----------------------------------------------------------------------------
//  本文件里有 3 处 TODO，是任务三里"感知"的那一半。
//  （另一半"配时算法"在甲的 green_time.cpp 里）
// ============================================================================

static float         g_queueCm    = -1.0f;   // 最近一次滤波后的有效距离；-1 = 无效
static unsigned long g_lastSample = 0;       // 上次采样的时刻（节流用）

// ============================================================================
//  ★★★ TODO（任务三 · 第 1 处）：一次测距 ★★★
// ============================================================================
//  目标：按 HC-SR04 的时序测出距离，单位厘米。
//
//  时序（四步）：
//      1) TRIG 拉低
//      2) TRIG 拉高，保持 10 微秒  → 传感器发出 8 个 40kHz 的超声脉冲
//      3) TRIG 拉低
//      4) 用 pulseIn 读 ECHO 高电平持续了多久（单位微秒）
//
//  换算：距离(cm) = 时长(us) / 58
//        （推导：声速约 343m/s ≈ 0.0343cm/us，声音跑了一去一回两趟，
//          所以 时长 × 0.0343 ÷ 2 = 时长 / 58）
//
//  三条硬要求：
//    ⚠ 1. pulseIn 的第三个参数（超时）必须传 ULTRASONIC_TIMEOUT_US。
//          不传的话它的默认超时是整整 1 秒 —— 传感器一坏，你的程序就呆立 1 秒，
//          精心写的非阻塞架构全白费。这是本实验的第二个大坑。
//    ⚠ 2. 若超时（pulseIn 返回 0），本函数要返回 -1 表示"本次无效"，
//          由上层沿用上一次的有效值。
//    ⚠ 3. 不要把"无效"当成 0 厘米返回 —— 0 厘米意味着严重拥堵，
//          会让这个方向永远拿到最长绿灯，另一个方向直接饿死。
//
//  提示：delayMicroseconds() 是用来凑那 10 微秒脉冲的，它和 delay() 不同，
//        只等微秒级，属于正常的硬件时序操作，不违反"禁止阻塞"的原则。
static float readDistanceCM() {
  // ① 先拉低再拉高，保证这次上升沿是干净的
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);        // 保持 10µs，传感器才会发那 8 个 40kHz 脉冲
  digitalWrite(PIN_TRIG, LOW);

  // ② 等 ECHO 的高电平，第三个参数是超时上限
  unsigned long dur = pulseIn(PIN_ECHO, HIGH, ULTRASONIC_TIMEOUT_US);
  if (dur == 0) return -1.0f;   // 超时 = 本次没测到

  // ③ 换算
  return dur / 58.0f;
}

// ============================================================================
//  ★★★ TODO（任务三 · 第 2 处）：滑动窗口滤波 ★★★
// ============================================================================
//  目标：把相邻几次的读数做一次滤波，压掉抖动。
//
//  为什么需要：超声波单次读数会因车体形状、地面反光、水面/车顶角度而跳变，
//              不滤波的话，串口上会看到数字疯狂乱蹦。
//
//  建议用「中位数」而不是平均值：
//      中位数能直接把"偶发的一次离谱读数"整个丢掉，
//      而平均值会被那一次离谱读数拖偏。
//
//  做法提示：
//      准备一个长度 SMOOTH_SAMPLES 的数组当环形缓冲，每次把新读数塞进去，
//      再取这 SMOOTH_SAMPLES 个数的中位数返回。
//      可以先用 static 局部变量或文件级静态数组保存历史值。
static float smoothDistance(float raw) {
  static float   buf[SMOOTH_SAMPLES];
  static uint8_t filled = 0;                       // 已采集的样本数（≤ SMOOTH_SAMPLES）
  static uint8_t head   = 0;                       // 下一个写入位置

  // 环形缓冲：新读数覆盖最旧的
  buf[head] = raw;
  head = (uint8_t)((head + 1) % SMOOTH_SAMPLES);
  if (filled < SMOOTH_SAMPLES) filled++;

  // 复制出来排序。只有 5 个元素，插入排序比调 qsort 划算：
  // 不占调用栈、不引入库、而且这个规模的插入排序本来就更快。
  float tmp[SMOOTH_SAMPLES];
  for (uint8_t i = 0; i < filled; i++) tmp[i] = buf[i];

  for (uint8_t i = 1; i < filled; i++) {
    float  key = tmp[i];
    int8_t j   = (int8_t)(i - 1);
    while (j >= 0 && tmp[j] > key) {
      tmp[j + 1] = tmp[j];
      j--;
    }
    tmp[j + 1] = key;
  }

  // 取中位数：奇数个取中间那个，偶数个取中间两个的平均。
  // 中位数能整条扔掉"偶发的一次离谱读数"，而平均值会被它拖偏 ——
  // 这正是选它而不选平均的原因。
  return (filled & 1) ? tmp[filled / 2]
                      : (tmp[filled / 2 - 1] + tmp[filled / 2]) / 2.0f;
}

// ============================================================================
//  已实现：初始化与采样调度
// ============================================================================
void trafficflowInit() {
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);
  g_lastSample = millis();
  g_queueCm    = -1.0f;
}

void trafficflowUpdate(unsigned long now) {
  // 节流：HC-SR04 要求两次测量至少间隔 60ms，测太快会收到上一次的回波（串扰）。
  if (now - g_lastSample < ULTRASONIC_INTERVAL_MS) return;
  g_lastSample = now;

  float raw = readDistanceCM();
  if (raw < 0.0f) return;                  // 本次无效 —— 沿用上一次的有效值

  float cm = smoothDistance(raw);
  if (cm > 0.0f) g_queueCm = cm;           // 只在拿到有效值时才更新

  // 只在"读数有明显变化（≥1cm）"或"每 2 秒"时打印。
  // 原来固定 500ms 一条，20 秒的绿灯里就刷 40 行，
  // 把相位切换、行人、紧急这些关键事件全淹没了 —— 长时间运行时根本没法读。
  // 改成事件驱动之后：静止时安静，拖滑块时立刻能看到变化。
  static unsigned long lastLogMs = 0;
  static float         lastLogCm = -1000.0f;
  float diff = (cm > lastLogCm) ? (cm - lastLogCm) : (lastLogCm - cm);
  if (diff >= 1.0f || now - lastLogMs >= 2000) {
    lastLogMs = now;
    lastLogCm = cm;

    Serial.print(F("[flow] raw="));
    Serial.print(raw, 1);
    Serial.print(F(" smooth="));
    Serial.print(cm, 1);
    Serial.println(F("cm"));
  }
}

float trafficflowQueueCm() {
  return g_queueCm;
}
