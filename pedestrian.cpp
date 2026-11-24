#include <Arduino.h>
#include "pins.h"
#include "pedestrian.h"
#include "phases.h"

// ============================================================================
//  pedestrian.cpp  ·  任务二：行人过街 + 紧急车辆优先      【乙 负责】
// ----------------------------------------------------------------------------
//  本文件里有 4 处 TODO，就是任务二的全部内容。
// ============================================================================

// --- 中断与主循环之间共享的变量 ---------------------------------------------
//  ⚠ 这几行必须保留 volatile：中断里改、loop() 里读的变量，
//    不加 volatile 会出现"按了没反应，但随便加一句 Serial.print 就又好了"
//    这种薛定谔的 bug。
static volatile bool          g_pedRequest   = false;
static volatile bool          g_emgRequest   = false;
static volatile unsigned long g_pedLastIsrMs = 0;    // 消抖用：上次行人中断的时刻
static volatile unsigned long g_emgLastIsrMs = 0;    // 消抖用：上次紧急中断的时刻

// 已受理、正在排队等待"全红插入点"的行人请求。
// 行人可以等，但不能丢 —— 所以它和 g_pedRequest 是两件事。
static bool g_pedPending = false;

// 紧急模式：记"开始时刻 + 是否进行中"。
// ⚠ 这里故意不写成"结束时刻 = now + T_EMG_HOLD"，因为用无符号数判断
//   "现在有没有超过结束时刻"会牵扯到回绕问题。记开始时刻、每轮比一次
//   "过去了多久"，才是既安全又好读的写法 —— 和 phases 里的计时方式一致。
static unsigned long g_emgStartMs = 0;
static bool          g_emgActive  = false;

// ============================================================================
//  中断服务程序（ISR）：只置标志，尽快返回
// ============================================================================
static void onPed() {
  // --------------------------------------------------------------------------
  //  TODO（任务二 · 第 1 处）：给这个 ISR 加消抖
  //
  //  为什么必须做：机械按键在按下的一瞬间会抖动几毫秒到几十毫秒，
  //  中断会因此连着触发好几次。后果是"行人请求被重复插入"。
  //
  //  做法提示：在 ISR 里记一个"上次触发时刻"，如果距现在不足 DEBOUNCE_MS
  //            就直接 return，什么都不置。
  //            （g_pedLastIsrMs 已经帮你声明好了）
  //
  //  ⚠ ISR 里不能用 delay() —— 只能比时间戳。
  //  ⚠ ISR 要短，不能在里面做串口打印、不能算配时、不能刷屏。
  // --------------------------------------------------------------------------
  unsigned long now = millis();
  if(now - g_pedLastIsrMs < DEBOUNCE_MS)
    return;
  g_pedLastIsrMs = now;
  g_pedRequest = true;
}

static void onEmg() {
  // TODO（任务二 · 第 2 处）：和上面一样，给紧急按键加消抖
  unsigned long now = millis();
  if(now - g_emgLastIsrMs < DEBOUNCE_MS)
    return;
  g_emgLastIsrMs = now;
  g_emgRequest = true;
}

// ============================================================================
//  已实现：初始化
// ============================================================================
void pedestrianInit() {
  pinMode(PIN_PED, INPUT_PULLUP);      // 内部上拉，按键另一端直接接 GND
  pinMode(PIN_EMG, INPUT_PULLUP);

  // ⚠ Uno 上真正支持外部中断的只有 D2 / D3。
  //   把按键接到别的脚再写 attachInterrupt 是不会报错的，只会静默失灵。
  attachInterrupt(digitalPinToInterrupt(PIN_PED), onPed, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_EMG), onEmg, FALLING);

  pinMode(PIN_BUZZ, OUTPUT);
  digitalWrite(PIN_BUZZ, LOW);
}

// ============================================================================
//  任务二：请求处理
// ============================================================================

// 紧急车辆优先：可以打断，必须立刻
static void handleEmergency(unsigned long now) {
  if (!g_emgRequest) return;
  g_emgRequest = false;

  // 已经在紧急模式里：忽略重复请求，不重置计时。
  // 没有这一句的话，一旦按键重复或被抖动漏过，phasesGoto 会把 5 秒重新计时，
  // 紧急模式就永远结束不了 —— 这比"多打一条日志"严重得多。
  // 有了它，消抖就算漏了一次，系统行为也是对的（双保险）。
  if (g_emgActive) return;

  // --------------------------------------------------------------------------
  //  TODO（任务二 · 第 3 处）：实现"紧急车辆立即抢占"
  //
  //  要求：不管当前在哪个相位、还剩多久，马上把所有方向都转红，
  //        并保持 T_EMG_HOLD 毫秒，同时蜂鸣器给出提示音；
  //        这段时间结束后，系统要能自己回到正常的自动循环。
  //
  //  可用：
  //    phasesGoto(PHASE_ALL_RED, T_EMG_HOLD, now);   // 强制进入全红并重置计时
  //    digitalWrite(PIN_BUZZ, HIGH);                 // 蜂鸣器响
  //    g_emgStartMs = now; g_emgActive = true;       // 记住"从什么时候开始响"
  //    Serial.println(F("[emg] emergency vehicle priority, all red")); // 建议打日志
  //
  //  ⚠ 关键：不要用 delay(T_EMG_HOLD) 去"等 5 秒"。
  //    那会让整个系统停摆 5 秒，行人按键、测距、显示全都不响应 ——
  //    这正是本实验最大的坑（见指导文档 1.3.3）。
  //    正确做法是只记一个"结束时刻"，然后在 pedestrianUpdate 里检查它。
  // --------------------------------------------------------------------------
  phasesGoto(PHASE_ALL_RED, T_EMG_HOLD, now);
  digitalWrite(PIN_BUZZ, HIGH);
  g_emgStartMs = now; g_emgActive = true;
  Serial.print(F("[emg] emergency vehicle priority, all red @"));
  Serial.print(now);
  Serial.println(F("ms"));
}

// 行人过街：可以等，但不能丢
static void handlePedestrian(unsigned long now) {
  if (g_pedRequest) {
    g_pedRequest = false;

    // 已经有一个请求在排队时，重复的请求直接合并掉（不打印、不改状态）。
    // 为什么需要：键盘自动重复约 30Hz（间隔 33ms），能穿过 30ms 消抖窗，
    //            按住不放会产生好几条请求。和紧急模式同理，需要一层状态守卫。
    //            注意这里不能 return —— 下面的"插入全红"逻辑还要继续走。
    if (!g_pedPending) {
      g_pedPending = true;    // 先锁存下来，等状态机走到全红再放行
      // 第 1 条日志：请求"排队成功"。注意此时还没放行，行人还得等。
      Serial.print(F("[ped] request queued @"));
      Serial.print(now);
      Serial.println(F("ms"));
    }
  }

  // --------------------------------------------------------------------------
  //  TODO（任务二 · 第 4 处 b）：把行人相位插入到"全红"里
  //
  //  要求：行人请求受理后，必须等当前绿灯跑完（也就是状态机自然进入
  //        PHASE_ALL_RED），再把这一段全红的时长延长到 T_PED_CROSS 让行人通过。
  //
  //  做法提示：
  //        if (g_pedPending && phasesCurrent() == PHASE_ALL_RED) {
  //            phasesSetDuration(T_PED_CROSS);   // 延长本段全红
  //            g_pedPending = false;             // 请求已消费
  //        }
  //
  //  为什么这样写就够了：pedestrianUpdate() 在 loop() 里排在 phasesUpdate()
  //  之后，所以状态机"刚进入全红"的那一轮，你立刻就看到了，
  //  此时已过去的时间约为 0，延长时长是安全且正确的。
  //
  //  想一想：如果不用"等全红"，而是收到请求就立刻切灯，会出现什么问题？
  //          （提示：此刻另一个方向的绿灯还没走完黄灯。）
  //          —— 这个问题老师很可能在答辩时问你，建议写进报告。
  // --------------------------------------------------------------------------
  if (g_pedPending && phasesCurrent() == PHASE_ALL_RED)
  {
    phasesSetDuration(T_PED_CROSS);
    g_pedPending = false;
    // 第 2 条日志：行人相位真正开始。它一定发生在状态机进入全红之后。
    Serial.print(F("[ped] cross start @"));
    Serial.print(now);
    Serial.println(F("ms"));
  }
}

void pedestrianUpdate(unsigned long now) {
  // 紧急模式到点就收尾。
  // 这一段已经写好，用来示范"非阻塞地等一段时间"的标准写法：
  //     记开始时刻 → 每轮比一次"过去了多久" → 到点了才做事。
  // 你只要在 handleEmergency 里把 g_emgStartMs / g_emgActive 填好，这里就自动生效。
  if (g_emgActive && (now - g_emgStartMs >= T_EMG_HOLD)) {
    digitalWrite(PIN_BUZZ, LOW);
    g_emgActive = false;
    Serial.println(F("[emg] emergency over, back to normal cycle"));
  }

  handleEmergency(now);
  handlePedestrian(now);
}
