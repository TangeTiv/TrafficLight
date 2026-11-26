#include <Arduino.h>
#include <Wire.h>          // Arduino 内置库，不需要额外安装
#include <string.h>
#include <stdio.h>
#include "pins.h"
#include "display.h"
#include "phases.h"
#include "trafficflow.h"   // trafficflowQueueCm()：第二行显示排队长度

// ============================================================================
//  display.cpp  ·  任务四：LCD 与串口显示                 【乙 负责】
// ----------------------------------------------------------------------------
//  LCD 依赖第三方库 LiquidCrystal I2C，所以用 pins.h 里的 USE_LCD1602 开关做了隔离：
//    USE_LCD1602 = 0  →  完全不依赖第三方库，开箱即可编译，只用串口输出
//    USE_LCD1602 = 1  →  装上库之后打开，LCD 才真正工作
//  这样你第一步先跑串口、第二步再接 LCD，出问题时能立刻分清是代码还是屏幕。
// ============================================================================

#if USE_LCD1602
  #include <LiquidCrystal_I2C.h>
  static LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
#endif

// ============================================================================
//  已实现：I2C 扫描
// ============================================================================
void i2cScan() {
  Serial.println(F("[i2c] scanning bus ..."));
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("[i2c] found device 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) {
    Serial.println(F("[i2c] no device found -- check SDA->A4 / SCL->A5 / VCC->5V / GND"));
  } else {
    Serial.print(F("[i2c] total "));
    Serial.print(found);
    Serial.println(F(" device(s)"));
  }
}

// ============================================================================
//  已实现：LCD 单行写入（带"内容没变就不刷"的优化，避免闪屏）
// ============================================================================
static void lcdWriteLine(uint8_t row, const char* text) {
#if USE_LCD1602
  if (row > 1) return;
  static char cache[2][LCD_COLS + 1] = { {0}, {0} };

  if (strncmp(cache[row], text, LCD_COLS) == 0) return;   // 内容相同 → 不刷，防闪屏

  char buf[LCD_COLS + 1];
  // 右侧补空格到满行，把上一次留下的残字盖掉
  snprintf(buf, sizeof(buf), "%-*s", LCD_COLS, text);

  lcd.setCursor(0, row);
  lcd.print(buf);

  strncpy(cache[row], text, LCD_COLS);
  cache[row][LCD_COLS] = '\0';
#else
  (void)row;
  (void)text;
#endif
}

// ============================================================================
//  已实现：同时输出到 LCD 与串口
// ============================================================================
void displayShow(const char* line1, const char* line2) {
  lcdWriteLine(0, line1);
  lcdWriteLine(1, line2);

  Serial.print(line1);
  Serial.print(F("  |  "));
  Serial.println(line2);
}

// ============================================================================
//  已实现：初始化
// ============================================================================
void displayInit() {
  Wire.begin();
  i2cScan();

#if USE_LCD1602
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("TrafficLight");
  lcd.setCursor(0, 1);
  lcd.print("vs0.0 ready");
  Serial.println(F("[display] LCD initialized, showing boot banner"));
  Serial.println(F("[display] backlight on but no text -> fix LCD_I2C_ADDR in pins.h, or turn the blue trimpot"));
#else
  Serial.println(F("[display] LCD disabled (USE_LCD1602 = 0 in pins.h), serial only"));
  Serial.println(F("[display] Task 4 step 2: install LiquidCrystal I2C, then set that switch to 1"));
#endif
}

// ============================================================================
//  ★★★ 任务四：要你实现的就是下面这一个函数 ★★★
// ============================================================================
void displayUpdate(unsigned long now) {
  // --------------------------------------------------------------------------
  //  TODO（任务四 · 第 1 处）：决定屏幕上显示什么
  //
  //  最低要求：显示「当前相位名」+「距下次变灯的秒数」。
  //  有余力再加：排队长度（直接体现动态配时在工作）。
  //
  //  三个必须注意的点：
  //
  //  1) 必须节流，而且不能只靠时间节流。
  //     displayShow() 的串口侧是无条件打印（LCD 侧才有去重），
  //     若每 250ms 调一次，20 秒的绿灯就是 80 行，会把
  //     [ped] / [emg] / [green] 这些关键事件全淹掉。
  //     所以采用"内容没变就不调"：只有拼出来的两行与上次不同才输出。
  //     倒计时按秒变化 → 每秒最多一行，简洁且够看。
  //
  //  2) 秒数向上取整 (ms + 999) / 1000。
  //     直接 ms / 1000 的话，刚进入相位的第一秒内一直显示 "0s"，
  //     看起来像倒计时卡住了。
  //
  //  3) 测距无效（-1）不能显示成 0cm。
  //     0cm 会被读成"严重拥堵"，本设计里代表"没有回波"。
  //     文案要短：LCD 只有 16 列，"queue: -- (no echo)" 会被截断成
  //     "queue: -- (no ec"，实测过。
  // --------------------------------------------------------------------------
  // 先按时间节流：多快也没必要超过 4Hz，省下循环开销给测距和状态机。
  static unsigned long lastCheck = 0;
  if (lastCheck != 0 && (now - lastCheck < 100)) return;   // 减法免疫 millis() 回绕
  lastCheck = now;

  // --- 拼两行 ---
  char line1[LCD_COLS + 1];
  char line2[LCD_COLS + 1];

  // 第一行：相位名 + 倒计时。相位名最长 9 字符（"EW_YELLOW"），
  // 倒计时最多 "20s"，"-10s" 左对齐后整体不超过 16 列。
  unsigned long remainMs  = phasesRemainMs(now);
  unsigned long remainSec = (remainMs + 999UL) / 1000UL;
  snprintf(line1, sizeof(line1), "%-10s %lus", phasesName(phasesCurrent()), remainSec);

  // 第二行：排队长度
  // ⚠ 不能用 snprintf 的 %f！avr-libc 默认不链接浮点版 vfprintf，
  //    %f 会直接输出一个 '?'，屏幕上就成了 "queue: ? cm"。
  //    实测 avr-nm 只能看到 vfprintf，没有 printf_flt。
  //    两个解法：(a) 手工拆整数/小数，(b) 编译加 -Wl,-u,vfprintf -lprintf_flt。
  //    这里选 (a)：不增加固件体积，也不用改 tasks.json 这个共享的构建配置。
  //    （Serial.print(cm, 1) 不受影响，Print 类走的是自己的 dtostrf，不经 snprintf。）
  float cm = trafficflowQueueCm();
  if (cm < 0.0f) {
    snprintf(line2, sizeof(line2), "queue: no echo");
  } else {
    // 四舍五入到 1 位小数，并处理 9.96 -> 10.0 这种进位
    unsigned int whole = (unsigned int)cm;
    unsigned int frac  = (unsigned int)((cm - (float)whole) * 10.0f + 0.5f);
    if (frac >= 10U) { frac = 0U; whole++; }
    snprintf(line2, sizeof(line2), "queue: %u.%u cm", whole, frac);
  }

  // --- 内容去重：两行都没变就不输出 ---
  // 这是本函数最关键的优化。displayShow() 会同时写 LCD 和串口，
  // 串口侧无去重，所以必须在这里把关。
  static char last1[LCD_COLS + 1] = {0};
  static char last2[LCD_COLS + 1] = {0};
  if (strcmp(line1, last1) == 0 && strcmp(line2, last2) == 0) return;

  strncpy(last1, line1, LCD_COLS); last1[LCD_COLS] = '\0';
  strncpy(last2, line2, LCD_COLS); last2[LCD_COLS] = '\0';

  displayShow(line1, line2);
}
