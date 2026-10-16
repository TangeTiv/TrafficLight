#include <Arduino.h>
#include <Wire.h>          // Arduino 内置库，不需要额外安装
#include <string.h>
#include <stdio.h>
#include "pins.h"
#include "display.h"
#include "phases.h"

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
  Serial.println(F("[i2c] 扫描总线中 ..."));
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("[i2c] 发现设备 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (found == 0) {
    Serial.println(F("[i2c] 没有找到任何设备 —— 检查 SDA->A4 / SCL->A5 / VCC->5V / GND"));
  } else {
    Serial.print(F("[i2c] 共 "));
    Serial.print(found);
    Serial.println(F(" 个设备"));
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
  Serial.println(F("[display] LCD 已初始化，显示开机横幅"));
  Serial.println(F("[display] 屏幕背光亮但没字 → 改 pins.h 里的 LCD_I2C_ADDR，或用螺丝刀拧转接板上的蓝色电位器调对比度"));
#else
  Serial.println(F("[display] LCD 未启用（pins.h 里 USE_LCD1602 = 0），当前只用串口输出"));
  Serial.println(F("[display] 任务四第 2 步：装好 LiquidCrystal I2C 库后把那个开关改成 1"));
#endif
}

// ============================================================================
//  ★★★ 任务四：要你实现的就是下面这一个函数 ★★★
// ============================================================================
void displayUpdate(unsigned long now) {
  // --------------------------------------------------------------------------
  //  TODO（任务四 · 第 1 处）：决定屏幕上显示什么
  //
  //  现在这个函数是空的，所以除了开机横幅以外，屏幕和串口什么都没有。
  //
  //  最低要求：显示「当前相位名」+「距下次变灯的秒数」。
  //  有了这两项，答辩演示时评委一眼就能看懂你的系统在干什么。
  //  有余力再加：排队长度（advance → 直接体现你的动态配时在工作）。
  //
  //  可用：
  //    phasesCurrent()        → 当前相位（Phase 类型）
  //    phasesName(p)          → 把相位变成字符串，例如 "NS_GREEN"
  //    phasesRemainMs(now)    → 剩余毫秒；除以 1000 就是秒
  //    trafficflowQueueCm()   → 当前排队长度（-1 表示无效）
  //    displayShow(第一行, 第二行)  → 已经写好的输出函数，LCD 和串口一起打
  //    snprintf(缓冲, 长度, 格式, ...) → 拼字符串。建议用 char 缓冲，
  //                                      不要用 String 类（会碎片化那 2KB 内存）
  //
  //  两个必须注意的点：
  //    1) 不要用 delay() 控制刷新节奏！用一个 static 变量记"上次刷新的时刻"，
  //       间隔到了（建议 200~500ms）才刷新一次。这和任务一是同一个道理。
  //    2) 一开始就节流。否则每轮 loop 都打串口，你会看到刷屏，
  //       而且 9600 波特率根本来不及发 —— 看起来像"程序卡住"，其实是串口堵了。
  //
  //  提示：秒数最好向上取整（(ms + 999) / 1000），否则刚进入相位时会一直显示 0 秒。
  // --------------------------------------------------------------------------
  (void)now;
}
