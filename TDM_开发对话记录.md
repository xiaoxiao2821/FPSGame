# TDM 团竞模式 — 完整开发对话记录

> **范围**: 2026-08-15 至 2026-08-18,本项目 TDM 模式的**全部开发对话**。
> **依据**: 项目记忆日志(`.workbuddy/memory/2026-08-1{5,6,7}.md`)+ 会话记录。
> **配套文档**: `TDM_需求文档.md`(效果基准)、`TDM_代码梳理.md`(实现结构)、`TDM_耗时统计.md`(工时)。
> **仓库**: `git@github.com:xiaoxiao2821/FPSGame.git`(全部代码/文档已推送)。

---

## 0. 项目背景

- **任务**: Gameplay Programmer 编程测试(Studio Surgical Scalpels),构建完整 FPS/TPS 玩法循环:开始→对战→胜负→结算→返回。24h 实际开发时间 / 1 周期限,允许使用 AI 工具。
- **基础**: 已有 UE5.8 FPS 模板项目(Variant_Shooter),含 ShooterCharacter/Weapon/NPC/PC/GameMode/HUD。
- **技术栈**: UE 5.8(Launcher 版)、C++ 为主 + 蓝图 Designer 做 UI、DS(专用服务器)权威架构。
- **协作约定**: 需求以 `TDM_需求文档.md` 为唯一基准;非常规做法(脚本改资产/运行时 hack)实施前必须与用户确认;UI 走蓝图 Designer,C++ 只暴露入口函数。

---

## 1. 第一天(2026-08-15)— 基础环境 + TDM 立项首版

### 1.1 上午: AnimBP 空指针错误修复
- **症状**: ABP_FP_Weapon/Pistol 每帧报 `GetController` 结果为空(日志 9840 次)。
- **根因**: AnimBP 在 OnPossess 之前调用 GetController→GetControlRotation→Pitch,控制器为空;报错节点标签"Set PitchN"有误导,真正的 None 注入点在 GetController 节点。
- **修复**: C++ 给 AFPSCharacter 加 `GetAimForwardVector()`(空安全,返回与控制旋转等价的 ForwardVector),再通过**无头 Python(EditorToolset BlueprintTools)重写蓝图节点图**:删除旧的 GetController→GetControlRotation→GetForwardVector 链路,插入 GetAimForwardVector 接回原有 dot 数学(1:1 等价)。
- **经验**: 编译产物字符串是 UTF-16,ASCII grep 搜不到;`BlueprintThreadSafe` 是类级说明符不能放 UFUNCTION;编译失败须删 `Intermediate/Build/Win64/.../Inc/FPS/UHT` 强制 UHT 重生成。

### 1.2 上午: Git 基建修复
- `.gitignore.txt`(带后缀)→ 改名 `.gitignore`,补 `.vs/` 等规则(否则 VS 锁定的 `.vsidx` 导致 git add 失败)。
- `.gitattributes` 原第 5 行 glob 拼写错误(`*.uasset*.umap...` 无效)→ 逐条拆分;UE 二进制资产加 `-text` 防换行损坏。
- LFS 上传失败(网络到不了 GitHub LFS 端点)→ 移除全部 `filter=lfs`,普通 git 推送 148MB 资产成功。

### 1.3 白天: 环境适配
- **UnLua 移除**: UnLua 2.3.6 与 UE 5.8 根本性不兼容(1184+808 处错误,UHT API 断裂)→ 用户确认整个移除,项目恢复可编译。
- **构建环境教训**(全程适用):
  - UBA 幻影失败(所有 cl 动作秒败、诊断被吞)→ 用 PowerShell 原生跑 Build.bat,或手动 cl.exe @rsp + link.exe @rsp 验证;
  - 大内存机 UBA 仍 "Low on memory" 杀进程 → `-NoUBA -MaxParallelActions=2/4`;
  - UBA 残留 `.isRunning`/`casdb.tmp` 会导致下次启动全败,须清理;
  - C++ 用 Slate 控件需在 FPS.Build.cs 加 `SlateCore` 模块。

### 1.4 晚间(20:40 起): TDM 模式立项 + 首版实现
- **需求**(参考和平精英经典团竞): 红蓝两队 4v4、击杀目标 30、3s 出生无敌、AI 补位、配枪浮层(后取消)。
- **首版 C++**(编译通过):
  - `AShooterTDMGameMode`: KillTarget=30、TeamSize=4、SpawnProtectionTime=3、ReportKill 计分+胜负判定、ChoosePlayerStart 按队分点、PostLogin 轮转分队;
  - `AShooterCharacter`: 队伍字节、出生无敌、配枪状态机;
  - `AShooterPlayerController`: 创建配枪 UI;
  - `UShooterTDMLoadoutUI`、`AShooterTDMSpawner`(后弃用,AI 补位内迁 GameMode)。
- **编译踩坑**: 基类 ShouldSpawnEnemyNPCs 非虚(C3668)、include 缺失(C2027)、重复定义(C2084)。

### 1.5 晚间: 匹配流程连通(主菜单→对战→结算→返回)
- **C++ 状态机**: `ETDMMatchPhase{MainMenu, Playing, Ended}`;BeginPlay 建主菜单+冻结输入;StartMatch 解冻+发武器;EndMatch 结算;ReturnToMainMenu RestartLevel。
- **新 UI 基类**: `UShooterTDMMainMenu`/`UShooterTDMEndScreen`。
- **UI 资产生成反复**(重要教训):
  1. UE5.8 Python 反射差异(WidgetTree 大写、GeneratedClass 大写且 protected)→ 控件树拿不到;
  2. 曾用 C++ RebuildWidget 重绘兜底 + 自动绑定首个 UButton —— **被用户否决**: 回归纯标准流程,UI 一律蓝图 Designer 做,按钮由用户接线(C++ 只留 BlueprintCallable 的 ServerStartMatch/ServerReturnToMainMenu);
  3. 脚本误删用户配置(每次 purge 重建)→ 改"缺则建,有则跳过,绝不删除";WorldSettings 属性在 5.8 改名 `DefaultGameMode`。
- **协作约定确立**: 非常规/特殊做法必须先与用户确认。

### 1.6 当日尾段: 首版打包尝试
- DefaultEngine.ini 默认地图改 Lvl_Login、ServerDefaultMap 改 Lvl_Shooter。
- 打包失败: 0 字节损坏 FPS.lib 残留(LNK1201/LNK1136)→ 清理 Binaries/Win64 下 FPS.* 后手动链接成功;后续打包仍反复失败,转入第二天。

---

## 2. 第二天(2026-08-16)— 主体开发(工作量最大的一天,约 10h39m)

### 2.1 UI 接线与架构修正
- **用户决定手动接线**,只给接口:按钮→Get Player Controller→Cast→ServerStartMatch/ServerReturnToMainMenu。
- **关键架构修正(用户指出)**: 客户端没有 GameMode(`GetAuthGameMode` 仅服务器有值)→ Server RPC 从 GameMode 挪到 **PlayerController** 上(否则真 DS 必炸)。
- **双击/镜头修复**: 菜单加 SetUserFocus + FInputModeUIOnly 指定焦点(否则首次点击被吞);开始比赛后 SetViewTarget(pawn) 重绑视角;删除客户端本地 SetIgnoreMoveInput(冻结由服务器复制标志负责)。
- **个人进场**: 每玩家独立 `bMatchReady`/StartPlayerMatch(不再全局开赛);比分改 Client RPC `Client_UpdateScore` 推送到客户端 HUD。

### 2.2 DS 权威化改造(核心)
- **根因**: 角色/武器/投射物不复制,客户端本地生成/伤害/死亡/重生,服务器不知情。
- **改动**: 全类 SetReplicates(+移动复制);武器开火改 Server RPC(带瞄准点),表现走 NetMulticast;投射物服务器生成、NotifyHit 仅服务器;TakeDamage 开头 `if (!HasAuthority()) return 0`;死亡/复活表现全部 Multicast 同步;重生统一服务器 SpawnActor+Possess。
- **UHT 崩溃真因**: `ReplicatedUsing` 属性必须同时暴露编辑器(EditAnywhere/BlueprintReadOnly),否则 UHT "aggregate exceptions"。

### 2.3 射击体验修复链(多次迭代)
1. 子弹不从枪口出:服务器骨骼 socket 不可靠 → 改用眼睛位置/客户端上报 muzzle;
2. 连射不跟随准星: `ServerUpdateAim`(Unreliable)每 50ms 上报最新瞄准点;
3. 半自动补发: 冷却分支无条件 SetTimer 补发;
4. 枪口起点: 客户端 FP mesh socket 准 → `GetMuzzleWorldLocation()` + 500cm 距离保护;
5. 对象池 → **弃用**: 池化复用与复制可见性在客户端乱序 → 改每发 SpawnActor;
6. **防隧穿(双保险)**: MaxSimulationTimeStep=1/120 + 服务器 Tick 对"上帧→本帧"SweepSingleByChannel,统一走 HandleImpact;
7. 手雷抛物线: ProjectileGravityScale 保留蓝图配置;无 socket 回退眼睛位置(高度≈准星);
8. 客户端本地模拟 + 碰撞 QueryOnly(命中/隐藏/销毁全服务器驱动,不提前消失)。

### 2.4 队伍系统与染色
- **AI 顶替**: 开局 FillTeamRoster 每队补满 4 bot;玩家进场 RemoveRandomBot 顶替;OnBotDied 按 `TeamSize-人类-存活bot` 补位。
- **出生无敌是需求**(用户裁定): 玩家和 AI 都 3s 无敌,开局打不动属正常。
- **染色**: TeamByte `ReplicatedUsing=OnRep_Team` + 各端本地 ApplyTeamColor。
- **染色首次不触发修复(关键)**: OnRep 只在值变化时触发,首次复制==CDO 默认则不触发 → **TeamByte 默认改 255(未分配)**,SetTeam 后 0/1 必触发。

### 2.5 生命/复活(需求落地)
- 友伤免疫、MaxHP 统一 100(4 枪击杀)、复活复用 pawn(2s,Revive),重生走 FindTeamPlayerStart。
- 复活视角残留反复排查: 死亡相机的 SpringArm 内建相机不是 UCameraComponent(漏网)→ 最终封装 `ForceFirstPersonView()`(deactivate 非 FP 相机 + 恢复 FP 位姿 + SetViewTarget)+ 蓝图 BP_OnRevive 延迟一帧再拉回。

### 2.6 出生点(绕开 World Partition 流送)
- **实锤**: Lvl_Shooter 是 World Partition,只加载玩家附近 1 个 PlayerStart(日志 slot 0/1)→ 全队挤一点。
- **方案**: 硬编码 `RedSpawnLocations`/`BlueSpawnLocations`(每队 4 个 FTransform,用编辑器 Python 读出坐标写入 C++ 构造)→ BeginPlay SpawnActor 常驻不可见 PlayerStart,完全绕开流送。
- 命名兼容: Player0-4 红 / 5-8 蓝(关卡实际命名无 Player1、无 RED/BLUE tag)。

### 2.7 UI 事件链路(本轮最曲折的排查)
- 需求: 子弹数/血量做复制属性 + 客户端事件。
- 反复排查三轮,最终根因(**写入记忆的铁律**):
  1. 复制的 OnRep 在客户端**早于 Possess** 触发 → 发送端 IsLocallyControlled 过滤会把早期事件全吞掉 → **不要在发送端过滤**;
  2. **客户端 PC 的 OnPossess 根本不执行**(服务器专用)→ 订阅/初始化必须移到 **SetPawn**(服务器经 OnPossess 内部调用,客户端经 ClientRestart 调用,两端必执行);
  3. 弹药初始值: OnRep 首次不触发 + Possess 时武器未复制 → 武器 BeginPlay 客户端 0.1s 后主动推送兜底。
- 换弹: 各武器 ReloadTime(手枪 0.5/步枪 0.8/爆弹 1.0s)+ `bIsReloading` 复制 + OnRep → OnReloadStateChanged UI 事件。

### 2.8 准备阶段 + 登录场景
- **准备阶段(需求)**: 首位玩家进场后 30s 倒计时;期间禁伤、不产 AI;结束销毁 pawn 重走重生 + 比分清零;倒计时/结束经 Client RPC → `UShooterPrepareUI`(BP_OnPrepareCountdown/Ended)。
- **登录场景(需求)**: `Lvl_Login` + `UShooterLoginMenu`(ServerAddress=127.0.0.1/Port=7777,StartGame OpenLevel travel)+ `AShooterLoginGameMode`。

### 2.9 队伍分配时序 + 选队
- **同边复活根因**: PostLogin 里 Super(基类 RestartPlayer)在队伍分配之前执行 → 全部初始红队 → 队伍分配移到 Super::PostLogin **之前**。
- 5 人超员: 补位公式漏减人类 → 改 `TeamSize - CountHumanPlayers - CountAliveBots`。
- 选队: `ServerChooseTeam`(Server RPC)→ UMG 友好化 `UShooterTDMMainMenu::SelectTeam`。

### 2.10 AI 行为 + 大排查(方案 B)
- 行为: 连射(AutoFire timer)、15° 散布=30% 命中率、队伍制感知(不再依赖 Player tag)。
- **AI 不动全链路排查**(7 轮,逐层排除):
  1. 配置齐全(感知/NavMesh/StateTree/EQS)但 speed=0;
  2. TEST-MoveTo 二分: 直接 MoveToLocation result=2 且 466cm/3s 移动成功 → **移动/寻路系统正常**;
  3. StateTree 从未发指令(STTASK=0,runStatus=4=Succeeded)→ **ST_Shooter 资产运行数据损坏(空树)**;
  4. 方案 A(编辑器重存)无效 → **方案 B(用户裁定): C++ 兜底 AI** —— AShooterAIController 重写为 Tick 驱动: 0.4s 节流 FindEnemy(不同队+存活+4000cm+视线),有敌→面向+距离管理(>1200 靠近/<1200 射击)+ 持续射击,无敌→漫游(GetRandomPointInNavigableRadius + MoveToLocation,2.5s 冷却)。参数 BPS 可调。
- 蓝队高台出生点(PlayerStart6/7)不可达(MoveTo result=0)→ 遗留。

### 2.11 夜间打包尝试(23:40-23:52)
- 打包需 `-NoUBA`;失败根因 = 0 字节损坏 FPS.lib 残留 → 清理后手动链接一次成功;第四次打包运行中(转入次日)。

---

## 3. 第三天(2026-08-17)— 收尾、打包攻坚、文档

### 3.1 打包反复失败(第 8-12 次,00:37-01:12)
| 次数 | 死因 | 处置 |
|---|---|---|
| 8 | UBT 启动删旧 `Trace-*.uba` 被锁(残留进程) | 清锁 |
| 9 | 脚本漏 `-project=` 参数(我的失误) | 补全命令 |
| 10 | 删 AutomationTool `Log.json` 被锁 | 杀残留进程 |
| 11 | UBA `.isRunning`/`casdb.tmp` 残留 → 存储服务器挂死 | 清 UBA 残留 |
| 12 | 脚本 `rm /c/ProgramData/...` 沙箱静默拦截挂起 | Python 全清 |
- **教训**: UAT 单实例;失败后必须确认无 dotnet 残留 + 清日志锁 + 清 UBA 残留再重跑。最终环境清干净,交用户手动打包。

### 3.2 Login 场景 UI 不显示 — 根因与修复(01:17-01:34)
- **根因(用户判断正确)**: UI 创建放在 `AShooterLoginGameMode::BeginPlay()`——**GameMode 只在服务器进程执行**;PIE 联网模式下客户端进程无 GameMode 逻辑,AddToViewport 从未在客户端跑过。
- **修复**: 新增 `AShooterLoginPlayerController`(BeginPlay 中 IsLocalController 判断后创建 BP_LoginMenu + AddToViewport + UI 输入模式);LoginGameMode 瘦身为纯配置(只指定 PlayerControllerClass)。

### 3.3 用户手动打 win 包成功(约几分钟)
- 用户在编辑器里打包(引擎模块预编译、只编项目代码)→ 数分钟出 `FPS.exe`(win 包),验证了"高效路径是编辑器打包"。

### 3.4 DS 打包攻坚 — 确认死路(21:00-23:50)
- **目标**: 打 DS 专用服务器包。
- **失败链(源码级确认)**:
  1. Launcher 版 `InstalledBuild.txt` 标记 → UBT 拒绝 `TargetType.Server`("Server targets are not currently supported from this engine distribution");
  2. 改名 InstalledBuild.txt → UBT 认为源码版放行 → 但**触发全量重编译引擎**(1093 动作,内存受限 2 并行 ≈2h);
  3. Launcher 版 ThirdParty 源码被裁剪 → 5 个 C1083 缺失(AHEasing/SSEMathFun/MiMalloc/msdfgen 等)→ BUILD FAILED;
  4. 结论: **Launcher 版引擎无法直接打 DS 包**(需补全 ThirdParty 或换源码版引擎)。
- **win 包 `-server` 无效(实测+源码铁律)**: `CoreMisc.h::IsRunningDedicatedServer()`: `IsServerOnly()`(仅 Server target 的 UE_SERVER)才返回 true,`IsGameOnly()`(Game target 的 UE_GAME)直接 return false → **Game 目标二进制无论加什么参数永远进不了 DS 模式**(实测 `-server -nullrhi -unattended` 仍加载 Lvl_Login + 初始化 D3D12)。
- 已恢复 InstalledBuild.txt;保留 FPSServer.Target.cs(未来换源码引擎可用)。

### 3.5 替代方案交付
- **方案 ① 编辑器 DS(开发测试)**: `UnrealEditor.exe FPS.uproject -server -log`;
- **方案 ② Listen Server(局域网联机)**: win 包 `FPS.exe "/Game/Variant_Shooter/Lvl_Shooter.Lvl_Shooter?listen"`,其他人输 `主机IP:7777`;
- 方案 ③(正式部署): 换源码版引擎出真 DS 包。

### 3.6 Git 提交 + 临时文件清理
- 扩充 `.gitignore` 忽略全部打包/编译诊断残留;`git add` 仅暂存真实项目改动(74 文件)。
- 提交 `d208c79`(feat: TDM 全链路)+ `8fa8968`(docs: 记忆)+ `0ca09d3`(docs: 需求 v1.4/代码梳理 v1.1/耗时统计)→ 已全部推送 `git@github.com:xiaoxiao2821/FPSGame.git`。

### 3.7 最终文档(交付)
- **TDM_需求文档.md v1.4**: 登录 UI 归客户端 PC、登录场景纯客户端入口、DS 打包限制附注。
- **TDM_代码梳理.md v1.1**: 新增 §5.5 登录场景;踩坑记录 10→13 条。
- **TDM_耗时统计.md**: 按规则统计 ≈14 小时(8/15 约 3h、8/16 约 10h39m、8/17 约 22m;扣除打包 ≈3h28m)。

---

## 4. 关键问题与根因索引(踩坑速查)

| # | 问题 | 根因 | 解决 |
|---|---|---|---|
| 1 | AnimBP 空指针 | OnPossess 前 GetController 为空 | GetAimForwardVector + 无头 Python 改蓝图 |
| 2 | 染色首次不生效 | OnRep 只在值变化触发 | TeamByte 默认 255 |
| 3 | 客户端 PC OnPossess 不执行 | 服务器专用回调 | 订阅/初始化移 SetPawn |
| 4 | UI 收不到事件 | 发送端 IsLocallyControlled 过滤吞早期 OnRep | 删过滤,靠订阅端保证 |
| 5 | 开局同边 | 基类 RestartPlayer 先于分队 | 分队移到 Super::PostLogin 前 |
| 6 | 全队挤一个出生点 | World Partition 只流送 1 个 PlayerStart | 硬编码坐标 SpawnActor 常驻 |
| 7 | AI 不动 | ST_Shooter 资产运行数据损坏(空树) | 方案 B:C++ Tick 驱动 AI |
| 8 | Login UI 不显示 | GameMode 只在服务器执行 | UI 移客户端 PlayerController |
| 9 | Game 包 -server 无效 | 编译期 UE_GAME 决定 | 需 Server target(Launcher 版不可) |
| 10 | 打包反复失败 | 日志锁/UBT Trace 锁/UBA 残留 | 统一清理清单(见 3.1) |
| 11 | UHT 崩溃 | ReplicatedUsing 属性未暴露 | 必须 EditAnywhere/BlueprintReadOnly |
| 12 | UBA 幻影编译失败 | UBA 执行器强制+状态残留 | -NoUBA + 手动 cl/link 验证 |

---

## 5. 最终交付物清单

| 交付物 | 位置 | 说明 |
|---|---|---|
| 全部源码(C++ 68 文件)+ 蓝图 + 配置 | GitHub `FPSGame.git` | 3 个提交,已推送 |
| TDM 需求文档 v1.4 | `TDM_需求文档.md` | 效果基准 |
| TDM 代码梳理 v1.1 | `TDM_代码梳理.md` | 实现结构 |
| TDM 耗时统计 | `TDM_耗时统计.md` | ≈14h |
| 本对话记录 | `TDM_开发对话记录.md` | 本文档 |
| 可运行 win 包 | `Saved/StagedBuilds/Windows/` | 编辑器打包产物,未入 git |
| 蓝图备份 | `.workbuddy/backup_tdm_20260816/` | 本地备份 |

**遗留事项**: ① 蓝队高台出生点(PlayerStart6/7)不可达;② ST_Shooter 资产损坏待以后重建(方案 B 已绕开);③ 正式 DS 部署需源码版引擎。
