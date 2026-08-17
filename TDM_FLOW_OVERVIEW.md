# TDM 团竞模式 — 流程连通说明 (2026-08-15)

参考和平精英"经典团竞":双方出生点、4v4、击杀满 30 一方获胜、复活 3 秒无敌、复活可调武器、人数不足 AI 补位;并新增**开始菜单 / 结算屏 + 返回菜单**闭环。

## 已完成的 C++ (已编译通过, BUILD_EXIT_CODE=0)

### 比赛流程状态机 (`AShooterTDMGameMode`)
- `ETDMMatchPhase { MainMenu, Playing, Ended }`
- `BeginPlay()`:进入 `MainMenu` 阶段 → **冻结所有玩家输入** → 弹出 `MainMenuClass` 浮层(无菜单资产则直接 `StartMatch`)
- `StartMatch()`(BlueprintCallable):移除菜单 → `Playing` → **解禁输入** → 给每个已生成玩家开配枪浮层 → `BP_OnMatchStarted`
- `EndMatch(WinningTeam)`(内部):`bMatchEnded` 置位 → `Ended` → 创建 `EndScreenClass` 并 `Populate(胜队, 红分, 蓝分)` → `BP_OnMatchEnded(WinningTeam)`
- `ReturnToMainMenu()`(BlueprintCallable):`RestartLevel` 干净重载关卡,`BeginPlay` 重新弹主菜单
- `IsMatchPlaying()`(BlueprintPure):`OnPossess` 仅在 `Playing` 阶段才开配枪浮层

### 两个基类控件 (`UUserWidget`)
- `UShooterTDMMainMenu`:`StartButton` → `OnStartClicked` → `GameMode->StartMatch()`;`BP_OnMenuReady`
- `UShooterTDMEndScreen`:`ReturnButton` → `OnReturnClicked` → `GameMode->ReturnToMainMenu()`;`Populate_Implementation` 填"X 获胜!"/"RED n - m BLUE"
- **按钮兜底**:`StartButton`/`ReturnButton` 用 `BindWidgetOptional`,若蓝图里没严格按名放按钮,则**自动绑定控件树里第一个 UButton** —— 所以极简 UI 也能驱动流程,不会卡死。

### PlayerController
- 抽出 `OpenLoadoutUI(ShooterCharacter)`(public);`OnPossess` 仅在 `TDM->IsMatchPlaying()` 时开配枪浮层(主菜单阶段保持关闭)。

## 待你在本机编辑器生成 / 填写的蓝图 (`create_tdm_ui.py`)

我这边无法直接拉起你的编辑器(工程锁 + 当前内存吃紧),所以用**方案B**:脚本 `D:\UnrealProject\FPS\create_tdm_ui.py` 已写好,你在编辑器里一键跑即可。

**运行方式(二选一):**
1. Output Log 切到 **Python** 标签页,把 `create_tdm_ui.py` 内容整段粘贴执行;
2. 菜单 **Tools → Execute Python Script...**,选该文件。

**脚本会做什么:**
- 建 `BP_TDMMainMenu`(继承 `UShooterTDMMainMenu`):满屏暗底 + 标题 + 开始按钮
- 建 `BP_TDMEndScreen`(继承 `UShooterTDMEndScreen`):结果/比分文本 + 返回按钮
- 建/更新 `BP_TDMGameMode`(继承 `AShooterTDMGameMode`):设 `MainMenuClass`/`EndScreenClass`/`KillTarget=30`/`TeamSize=4`,并把**当前关卡**的 GameMode Override 指向它
- 全部编译 + 保存;可重复跑(已存在则加载而非新建)

## 你仍需手动补的点
1. `BP_TDMGameMode` 的 `LoadoutWeapons`:从现有武器类里挑(手枪/步枪…)
2. PlayerController 蓝图:`LoadoutUIClass = BP_TDMLoadoutUI`
3. `BP_TDMBot`(继承你的人物 NPC,预置武器)+ 关卡放 `AShooterTDMSpawner`(BotClass=BP_TDMBot, TeamSize=4)—— 之前的 TDM 待办沿用
4. 配枪浮层 / 主菜单 / 结算屏的**视觉排版**由你按手感调(脚本给的是能跑通的第一版)
5. `BP_OnMatchEnded` 里"比赛结束"的具体表现(结算 UI 已由基类弹出,可在此叠加额外逻辑)

## 流程全景
```
进关卡
  └─ BeginPlay (TDM) → 弹主菜单 + 冻结输入
        └─ [点击开始] → StartMatch → 解禁 + 开配枪
              └─ 对战(复活3秒无敌 / 移动或射击关配枪)
                    └─ 某队击杀满 30 → EndMatch
                          └─ 弹结算屏(胜队+比分)
                                └─ [返回菜单] → RestartLevel → 回到主菜单
```

计分语义:点数加在**击杀者**队伍(非死者);同队误杀/自杀不计分。
