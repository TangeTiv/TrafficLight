#include <Arduino.h>
#include <Wire.h>          // Arduino 内置库，不需要额外安装
#include <string.h>
#include <stdio.h>
#include "pins.h"
#include "display.h"
#include "phases.h"
#include "trafficflow.h"   // 任务四要显示排队长度，需要 trafficflowQueueCm()

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

  // 串口也要去重。LCD 那边已经有"内容没变就不刷"的优化，但串口原来是无条件打印：
  // 倒计时按秒变化，20 秒的绿灯就会刷 20 行（4Hz 刷新则是 80 行），
  // 把相位切换、行人、紧急全埋掉 —— 和刚才修 [flow] 是同一个问题。
  static char last1[LCD_COLS + 1] = {0};
  static char last2[LCD_COLS + 1] = {0};
  if (strncmp(last1, line1, LCD_COLS) == 0 &&
      strncmp(last2, line2, LCD_COLS) == 0) return;

  strncpy(last1, line1, LCD_COLS); last1[LCD_COLS] = '\0';
  strncpy(last2, line2, LCD_COLS); last2[LCD_COLS] = '\0';

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
  lcd.print("  ready");
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
  // 200ms 刷一次。倒计时是按秒变的，200ms 足够及时；
  // 和任务一一样用"记上次时刻"节流，绝不用 delay()。
  static unsigned long lastRefresh = 0;
  if (now - lastRefresh < 200) return;
  lastRefresh = now;

  unsigned long remainMs = phasesRemainMs(now);
  unsigned long remainS  = (remainMs + 999) / 1000;   // 向上取整，否则刚进相位会一直显示 0

  char line1[LCD_COLS + 1];
  char line2[LCD_COLS + 1];

  // 第一行：相位名 + 倒计时。相位名最长 9 字符（EW_YELLOW），左对齐占 9 位，
  // 后面跟 3 位的秒数，合计 12 字符，16 列的屏放得下。
  snprintf(line1, sizeof(line1), "%-9s%3lus", phasesName(phasesCurrent()), remainS);

  // 第二行：排队长度。-1 表示测距无效，要显示 "--" 而不是 0 ——
  // 0 会被误读成"车贴脸堵死"，和"没测到"完全是两回事。
  float cm = trafficflowQueueCm();
  if (cm < 0.0f) {
    snprintf(line2, sizeof(line2), "Q:--");
  } else {
    // ⚠ AVR 的 snprintf 默认不支持 %f（会输出一个问号，因为没链接浮点 printf）。
    //   必须先四舍五入转成整数再打印 —— 这也是骨架里用 Serial.print(cm, 1)
    //   而不是 printf 的原因。
    int cmInt = (int)(cm + 0.5f);
    snprintf(line2, sizeof(line2), "Q:%3dcm", cmInt);
  }

  displayShow(line1, line2);
}
