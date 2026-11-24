#include <Arduino.h>
#include "pins.h"
#include "pedestrian.h"
#include "phases.h"

// ============================================================================
//  pedestrian.cpp  ·  任务二：行人过街 + 紧急车辆优先      【乙 负责】
// ----------------------------------------------------------------------------
//  本文件里有 4 处 TODO，就是任务二的全部内容。
//
//  实现策略（与骨架注释里的"排队等全红"不同，见下面第 4 处的说明）：
//    行人按钮 = 按下即切（抢占），但优先级低于紧急车辆。
//    紧急车辆 = 无条件抢占，全红 + 蜂鸣器 T_EMG_HOLD，到点自动恢复。
//  两者都不使用 delay()，全部是"记时间戳 + 每轮比一次"的非阻塞写法。
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

// 行人相位进行中：从"按下 D2 那一刻"开始计时。
// 和紧急模式一样，只记开始时刻、每轮比"过去了多久"，不用 delay() 等。
//  ⚠ 记的是"开始时刻"而不是"结束时刻"：无符号数比较结束时刻会在 millis()
//    回绕（约 49.7 天）时出错，减法 now - start 才是安全写法。
static unsigned long g_pedStartMs = 0;
static bool          g_pedActive  = false;

// 紧急模式：记"开始时刻 + 是否进行中"。
// ⚠ 这里故意不写成"结束时刻 = now + T_EMG_HOLD"，因为用无符号数判断
//   "现在有没有超过结束时刻"会牵扯到回绕问题。记开始时刻、每轮比一次
//   "过去了多久"，才是既安全又好读的写法 —— 和 phases 里的计时方式一致。
static unsigned long g_emgStartMs = 0;
static bool          g_emgActive  = false;

// 紧急模式补发过离场黄灯时置位：黄灯跑完进全红的那一刻要重新开始计时。
// 额外再记一个 g_emgRedSince（"全红真正开始"的时刻）：
// 补过黄灯时全红比按键晚 T_YELLOW 才开始，收尾必须用这个时刻，
// 否则蜂鸣器和红灯会提前 3 秒结束。
static bool          g_emgRedPending = false;
static unsigned long g_emgRedSince   = 0;

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
  //  做法：记"上次触发时刻"，距现在不足 DEBOUNCE_MS 就直接 return。
  //
  //  ⚠ ISR 里不能用 delay() —— 只能比时间戳。
  //  ⚠ ISR 要短，不能在里面做串口打印、不能算配时、不能刷屏。
  //
  //  为什么用 now - last 而不是 now > last + DEBOUNCE_MS：
  //     无符号减法在 millis() 回绕时依然得到正确的"过去了多久"；
  //     加法比较会在回绕瞬间误判，吞掉一次按键。
  // --------------------------------------------------------------------------
  unsigned long now = millis();
  if (now - g_pedLastIsrMs < DEBOUNCE_MS) return;   // 同一次按下的抖动回声，丢掉
  g_pedLastIsrMs = now;
  g_pedRequest = true;
}

static void onEmg() {
  // TODO（任务二 · 第 2 处）：和上面一样，给紧急按键加消抖
  unsigned long now = millis();
  if (now - g_emgLastIsrMs < DEBOUNCE_MS) return;
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

  // --------------------------------------------------------------------------
  //  TODO（任务二 · 第 3 处）：实现"紧急车辆立即抢占"
  //
  //  要求：不管当前在哪个相位、还剩多久，马上把所有方向都转红，
  //        并保持 T_EMG_HOLD 毫秒，同时蜂鸣器给出提示音；
  //        这段时间结束后，系统要能自己回到正常的自动循环。
  //
  //  实现要点（四件事，顺序不能乱）：
  //    1) 清掉行人相位标志 —— 紧急优先于行人
  //    2) 补上"离场黄灯" —— 绿灯直接跳红是真实路口不允许的，见下
  //    3) 全红并重置计时（phasesGoto 一次做完"换相位 + 设时长 + 刷灯"）
  //    4) 蜂鸣器响 + 记下开始时刻，交给 pedestrianUpdate 里的收尾逻辑
  //
  //  ⚠ 关键：不要用 delay(T_EMG_HOLD) 去"等 5 秒"。
  //    那会让整个系统停摆 5 秒，行人按键、测距、显示全都不响应 ——
  //    这正是本实验最大的坑（见指导文档 1.3.3）。
  //    正确做法是只记一个时刻，然后在 pedestrianUpdate 里检查它。
  // --------------------------------------------------------------------------
  g_pedPending = false;    // ① 路口已清空，行人的排队请求作废（可以再按）
  g_pedActive  = false;

  // ② 补离场黄灯：如果当前正亮着某个绿灯，先给 T_YELLOW 让它清空路口。
  //    否则"绿灯直接跳红"违反真实路口规则，也让状态机"绿→黄→全红"的
  //    迁移次序失去意义。
  Phase cur = phasesCurrent();
  if (cur == PHASE_NS_GREEN) {
    phasesGoto(PHASE_NS_YELLOW, T_YELLOW, now);
  } else if (cur == PHASE_EW_GREEN) {
    phasesGoto(PHASE_EW_YELLOW, T_YELLOW, now);
  }

  // ③ 全红 + 计时。若刚才补了黄灯，先只置 pending，等黄灯跑完再进全红
  //    （这一步由 pedestrianUpdate 接上）。
  if (phasesCurrent() == PHASE_NS_YELLOW || phasesCurrent() == PHASE_EW_YELLOW) {
    g_emgRedPending = true;
  } else {
    phasesGoto(PHASE_ALL_RED, T_EMG_HOLD, now);
    g_emgRedPending = false;
    g_emgRedSince   = now;         // 全红从这里开始计时
  }

  digitalWrite(PIN_BUZZ, HIGH);                 // ④ 蜂鸣器响
  g_emgStartMs = now;
  g_emgActive  = true;                          // 收尾由 pedestrianUpdate 完成

  Serial.print('['); Serial.print(now); Serial.print(']');
  Serial.print(F(" [emg] emergency from "));
  Serial.print(phasesName(cur));
  Serial.println(F(", all red + buzzer"));
}

// 行人过街：按下即切（抢占），但优先级低于紧急车辆
static void handlePedestrian(unsigned long now) {
  // --------------------------------------------------------------------------
  //  TODO（任务二 · 第 4 处）：行人按钮直接切换红绿灯
  //
  //  需求：按 D2 → 立刻开始切换（不再"等当前绿灯跑完"）
  //        当前方向先黄灯 T_YELLOW 缓冲（清空路口中正在通过的车），
  //        再全红 T_PED_CROSS 让行人通过，然后回到正常循环。
  //
  //  为什么不能"按下就瞬间全红"（答辩必问）：
  //      此刻另一个方向可能刚进绿灯 1 秒。直接转红等于让人闯红灯 ——
  //      真实路口的黄灯就是干这件事的：不是"提醒"，而是给已经越过
  //      停止线的车一个安全通过的窗口。
  //      所以把"切换"拆成 黄灯 3 秒 + 全红 5 秒 两个阶段，
  //      按下后立刻进入第一阶段，观感上仍然是"按了就切"。
  //
  //  为什么收尾停在 ALL_RED 而不是自己跳回某个绿灯：
  //      "下一个该放行哪个方向"只有状态机知道（phases.cpp 里的 g_nextGreen
  //      是 static 私有变量，外部读不到）。收尾停在 ALL_RED，
  //      下一轮 phasesUpdate() 会按它自己记的方向继续，绝不会出现
  //      "同一个方向连绿两次"这种灯卡住的经典 bug。
  //
  //  优先级：紧急 > 行人。紧急进行中时行人不抢占；
  //          行人进行中时按 D3，紧急立刻打断它（反过来不行）。
  // --------------------------------------------------------------------------
  if (g_pedRequest) {
    g_pedRequest = false;

    // 紧急模式进行中 → 行人请求丢弃，不能抢占（紧急 > 行人）
    if (g_emgActive) {
      Serial.print('['); Serial.print(now); Serial.print(']');
      Serial.println(F(" [ped] rejected: emergency in progress"));
    } else if (!g_pedActive) {
      // ★ 真正执行"切换"：按下即切
      Phase cur = phasesCurrent();
      g_pedStartMs = now;

      if (cur == PHASE_NS_GREEN) {
        phasesGoto(PHASE_NS_YELLOW, T_YELLOW, now);
      } else if (cur == PHASE_EW_GREEN) {
        phasesGoto(PHASE_EW_YELLOW, T_YELLOW, now);
      } else if (cur != PHASE_NS_YELLOW && cur != PHASE_EW_YELLOW) {
        // 此刻已经是全红（普通清空期）：直接把它改成行人过街的 T_PED_CROSS
        phasesGoto(PHASE_ALL_RED, T_PED_CROSS, now);
        g_pedActive  = true;
        g_pedPending = false;
        Serial.print('['); Serial.print(now); Serial.print(']');
        Serial.println(F(" [ped] ALL_RED extended for crossing"));
        return;
      }
      // 绿灯→已切黄灯；本来就在黄灯→不打断，让它自然跑完。
      // 两种情况都交给下面的"推进"分支，等黄灯结束后再接管全红。
      g_pedActive  = true;
      g_pedPending = false;     // 抢占时不需要排队，直接就用掉了

      Serial.print('['); Serial.print(now); Serial.print(']');
      Serial.print(F(" [ped] button pressed in "));
      Serial.print(phasesName(cur));
      Serial.println(F(", switching: yellow buffer -> ALL_RED"));
    } else {
      Serial.print('['); Serial.print(now); Serial.print(']');
      Serial.println(F(" [ped] ignored: pedestrian phase already running"));
    }
  }

  // 行人相位推进：黄灯缓冲跑完 → 全红 T_PED_CROSS → 交还给状态机
  if (!g_pedActive) return;

  if (phasesCurrent() == PHASE_NS_YELLOW || phasesCurrent() == PHASE_EW_YELLOW) {
    // 第一阶段：黄灯缓冲还在跑，什么都不做，等它自己结束
    return;
  }

  if (phasesCurrent() == PHASE_ALL_RED) {
    // 第二阶段起点：刚从黄灯进入全红。把这段全红延长到 T_PED_CROSS，
    // 并从这里重新开始计时（黄灯那 3 秒不该算进行人过街的 5 秒里）。
    // 只在"还没延长过"时动作一次，避免每轮重复重置导致全红永远结束不了。
    if (phasesDuration() < T_PED_CROSS) {
      phasesSetDuration(T_PED_CROSS);
      g_pedStartMs = now;
      Serial.print('['); Serial.print(now); Serial.print(']');
      Serial.println(F(" [ped] ALL_RED extended for crossing"));
    }
    return;
  }

  // 走到这里说明状态机已经离开全红，行人相位自然结束 —— 把标志清掉
  g_pedActive = false;
  Serial.print('['); Serial.print(now); Serial.print(']');
  Serial.println(F(" [ped] crossing phase done, back to normal cycle"));
}

void pedestrianUpdate(unsigned long now) {
  // 紧急模式：确保"黄灯 → 全红 5 秒"两个阶段都走完。
  //   · 补过黄灯（g_emgRedPending），黄灯跑完进全红时从这里重新计时；
  //   · 若此刻已在全红（无论谁把它设成 1.5 秒的普通清空期），
  //     都拉长到 T_EMG_HOLD —— 否则状态机 1.5 秒后就跳去绿灯，
  //     蜂鸣器还响着、红灯却已经放行了。
  if (g_emgActive && phasesCurrent() == PHASE_ALL_RED) {
    if (g_emgRedPending) {          // 刚补完黄灯，全红从这一刻开始
      g_emgRedPending = false;
      g_emgRedSince   = now;
    }
    if (phasesDuration() < T_EMG_HOLD) {
      phasesSetDuration(T_EMG_HOLD);   // 保证红灯真够 5 秒
    }
  }

  // 紧急模式到点就收尾。
  // 这一段已经写好，用来示范"非阻塞地等一段时间"的标准写法：
  //     记开始时刻 → 每轮比一次"过去了多久" → 到点了才做事。
  // ⚠ 收尾用的是"全红真正开始"的时刻 g_emgRedSince：补过黄灯时全红
  //   比按键晚 T_YELLOW 才开始，否则会提前 3 秒恢复。
  if (g_emgActive && !g_emgRedPending && (now - g_emgRedSince >= T_EMG_HOLD)) {
    digitalWrite(PIN_BUZZ, LOW);
    g_emgActive = false;
    Serial.println(F("[emg] emergency over, back to normal cycle"));
  }

  // 紧急事件优先处理：它可能打断正在进行的行人相位
  handleEmergency(now);
  handlePedestrian(now);
}
