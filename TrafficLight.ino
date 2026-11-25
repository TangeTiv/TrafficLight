// ============================================================================
//  TrafficLight.ino  ·  主骨架（冻结层）
// ----------------------------------------------------------------------------
//  《单片机原理及应用项目设计》选题 1：基于单片机的智能交通灯控制系统
//  硬件平台：Arduino Uno（ATmega328P / 16MHz / 2KB SRAM）
//  对应指导文档：实验1（Arduino Uno · 仿真 + 实物）
//
//  ────────────────────────────────────────────────────────────────────────
//  这个文件已经把"除了任务代码以外的所有事情"都做完了：
//      · 引脚与参数集中定义       → pins.h        （冻结层，改动前公告）
//      · 初始化与硬件自检          → phasesInit / phasesSelfTest
//      · 主循环的调用顺序          → 本文件 loop()  （冻结，改动前公告）
//      · 各模块的接口签名          → 各 .h 文件     （冻结，改动前公告）
//
//  你和队友要做的，就是到下面这 5 个地方填 TODO：
//      任务一  甲 → phases.cpp        phasesUpdate()        相位状态机
//      任务二  乙 → pedestrian.cpp    onPed / onEmg / handleEmergency / handlePedestrian
//      任务三  乙 → trafficflow.cpp   readDistanceCM / smoothDistance
//      任务三  甲 → green_time.cpp    calcGreenTime()       分段配时
//      任务四  乙 → display.cpp       displayUpdate()       显示内容
//  ────────────────────────────────────────────────────────────────────────
// ============================================================================

#include "pins.h"
#include "phases.h"
#include "pedestrian.h"
#include "trafficflow.h"
#include "green_time.h"
#include "display.h"

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(50);   // 等串口与外设上电稳定。这是初始化里唯一允许的 delay()

  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F(" Smart Traffic Light   Arduino Uno"));
  Serial.println(F(" Tasks 1-3 done: 4-phase / ped+emg / adaptive"));
  Serial.println(F(" LCD: set USE_LCD1602 in pins.h when ready"));
  Serial.println(F("=================================================="));

  phasesInit();          // 任务一(甲)：六个灯设为输出、进入初始相位、点亮
  phasesSelfTest();      // 环境自检：六个灯依次点亮 0.5 秒，用来验证接线与下载链路

  pedestrianInit();      // 任务二(乙)：两个按键 + 外部中断 + 蜂鸣器
  trafficflowInit();     // 任务三(乙)：超声波引脚
  displayInit();         // 任务四(乙)：I2C 扫描 + （可选）LCD 初始化

  Serial.println(F("[boot] ready. normal cycle running."));
}

void loop() {
  const unsigned long now = millis();

  //  ⚠⚠ 下面五行的调用顺序是冻结的，改动前先在群里说一声 ⚠⚠
  //
  //  顺序不是随便排的，每一行都依赖上一行的结果：
  //    1) 先采样 —— 让后面用到"排队长度"的地方读到的都是本轮的值
  //    2) 状态机推进 —— 这是整条流水线里"时间"的唯一推动力
  //    3) 配时算法紧跟其后 —— 它需要看到"刚刚进入的是哪个绿灯相位"
  //    4) 请求处理放在状态机之后 —— 这样它才能看到"刚进入全红"的那一轮，
  //       从而正确地把行人相位插入进去（这是任务二的关键时机）
  //    5) 最后统一刷新显示，把本轮结果呈现出来
  //
  trafficflowUpdate(now);    // 任务三(乙)：超声波采样
  phasesUpdate(now);         // 任务一(甲)：到点换相位
  greenTimeUpdate();         // 任务三(甲)：刚进绿灯就按排队长度改写时长
  pedestrianUpdate(now);     // 任务二(乙)：行人 / 紧急车辆请求
  displayUpdate(now);        // 任务四(乙)：显示

  //  ★ 本文件里不允许再出现 delay()。
  //    整个循环必须是非阻塞的 —— 一旦阻塞，上面五个模块会互相拖死。
  //    这是本实验的头号禁忌，详见指导文档 1.3.3。
}
