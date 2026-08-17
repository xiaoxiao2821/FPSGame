# TDM 团竞模式 — C++ 代码梳理文档

> **版本**: v1.1 | **更新日期**: 2026-08-18
> **配套文档**: `TDM_需求文档.md`(效果基准, v1.4 最终版);本文档描述**实现层面**的代码结构与职责,按"类 → 函数 → 职责"组织。
> **架构基调**: 全链路 **DS 服务器权威** —— 伤害/死亡/计分/重生/AI 生成只在服务器执行;客户端只做输入上报与表现;队伍/HP/武器等状态通过**属性复制 + OnRep** 同步,表现通过 **NetMulticast RPC** 广播。
> **v1.1 补充**: 新增登录场景类(`AShooterLoginGameMode` / `AShooterLoginPlayerController`)与 DS 打包限制结论。

---

## 0. 网络设计总览(先看这张图)

```
【玩家客户端】
  输入(移动/瞄准/开火)
    ├─ 移动: 客户端预测 → CharacterMovement 服务器校正(引擎内建)
    ├─ 瞄准: LookInput → ControlRotation(本地)
    ├─ 开火: DoStartFiring
    │          └─ CurrentWeapon->StartFiring(AimTarget, MuzzleWorld)   ← 瞄准点+枪口位置(含俯仰)
    │                └─ ServerStartFiring(AimTarget, MuzzleWorld) [Server RPC]
    └─ 切枪: DoSwitchWeapon → ServerSwitchWeapon [Server RPC]

【服务器】
  ServerStartFiring → 存 PendingAimTarget/PendingMuzzleLocation → StartFiringInternal
    └─ Fire(服务器专属) → FireProjectile
          └─ GameMode.AcquireProjectile(对象池取子弹) → 投射物服务器模拟
                └─ NotifyHit(服务器) → ProcessHit → ApplyDamage(服务器)
                      └─ TakeDamage(服务器权威判定: 无敌/友伤/HasAuthority)
                            └─ Die → ReportKill(计分) → Client_UpdateScore 广播 → 2s 后 Revive
                                  └─ Multicast_OnDeath / Multicast_OnRevive(表现同步)
```

**属性复制**(客户端本地生效,解决"材质实例/UI 不复制"问题):
- `TeamByte`(角色/NPC/PC)→ `OnRep_Team` / `OnRep_TeamByte` → 本地染色、UI 判断己方
- `CurrentHP` → `OnRep_CurrentHP` → 客户端血条
- `CurrentWeapon` → `OnRep_CurrentWeapon` → 客户端弹药 HUD

---

## 1. `AFPSCharacter`(基类,Source/FPS/FPSCharacter.*)

| 函数 | 职责 |
|---|---|
| `AFPSCharacter()` 构造 | `SetReplicates(true)` + `SetReplicateMovement(true)` —— **角色复制**是 DS 下客户端互见的根基(移动由服务器权威驱动,复制到客户端) |
| `Tick()` | **第一人称手臂/武器跟随镜头俯仰**:本地控制时 `FirstPersonMesh->SetRelativeRotation(FRotator(ControlRotation.Pitch,0,0))`,抬枪时枪口真正指向上/下(相机 `bUsePawnControlRotation` 不受影响) |
| `ApplyTeamColor(Team)` | 给第三人称 mesh 每个材质槽创建动态材质实例,设 Mannequin 材质 `Paint Tint`/`LogoTint` 参数 → **红(RED 0.85,0.15,0.15)/蓝(BLUE 0.15,0.25,0.9)** 队伍区分。被角色/NPC 的 `SetTeam` 和 `OnRep_Team` 调用 |
| `GetAimPitch()/GetAimYaw()/GetAimForwardVector()` | 空指针安全的控制旋转读取,供 AnimBP 使用(替代易崩的 GetController() 链) |
| `DoAim/DoMove/DoJump*` | 输入路由(客户端本地) |

---

## 2. `AShooterCharacter`(玩家角色,Variant_Shooter/ShooterCharacter.*)

### 伤害与死亡(服务器权威)
| 函数 | 职责 |
|---|---|
| `TakeDamage()` | ①`!HasAuthority` 直接忽略(客户端本地伤害无效);②出生无敌期免疫;③**友军伤害免疫**(攻击者同队 → 0 伤害);④扣 HP,归零调 `Die` |
| `Die(Killer)` | 服务器专属:停武器/停移动/禁碰撞/加死亡 tag/解析击杀者队伍→`ReportKill` 计分;`Multicast_OnDeath` 广播表现;**不销毁 pawn**,定时 2s 后 `Revive` |
| `Multicast_OnDeath_Implementation()` | 全端表现:禁输入、清弹药 HUD、`BP_OnDeath`(蓝图死亡动画) |
| `Revive()` | **复用原 pawn 复活**(需求):传回己方出生点(`FindTeamPlayerStart`)、HP 回满、恢复移动/碰撞/可见性、重装武器、重新出生无敌、`Multicast_OnRevive` |
| `Multicast_OnRevive_Implementation()` | 全端表现:恢复输入、强制恢复可见性/碰撞、刷新血条与弹药 HUD |
| `OnRespawn()` | 旧路径兜底(保留):pawn 若被销毁则 Destroy 走 PC 重生 |

### 开火与输入
| 函数 | 职责 |
|---|---|
| `DoStartFiring()` | 客户端输入入口:关闭配枪浮层 → `CurrentWeapon->StartFiring(GetWeaponTargetLocation())`(**瞄准点含镜头俯仰**,服务器据此修正射击方向) |
| `GetWeaponTargetLocation()` | 客户端相机射线终点(ECC_Visibility trace,10000cm)——即"准星指向的世界点" |
| `DoSwitchWeapon()/ServerSwitchWeapon()/SwitchWeaponNext()` | 切枪走 Server RPC,服务器权威切换(客户端不直接改) |

### 复制与状态
| 函数 | 职责 |
|---|---|
| `GetLifetimeReplicatedProps()` | 注册 `CurrentHP`/`CurrentWeapon`/`TeamByte` 复制 |
| `OnRep_CurrentHP()` | 客户端血条刷新 |
| `OnRep_CurrentWeapon()` | 客户端弹药 HUD 刷新 |
| `OnRep_Team()` | 客户端本地染色(材质实例不复制,必须本地执行) |
| `SetTeam(Team)` | 服务器设队伍 + 染色 |
| `BeginPlay()` | 重置 HP、**强制 capsule 对 Visibility/WorldDynamic/Pawn = Block**(防蓝图改 profile 导致打不中人)、TDM 出生无敌 |

---

## 3. `AShooterNPC`(AI 单位,Variant_Shooter/AI/ShooterNPC.*)

| 函数 | 职责 |
|---|---|
| `BeginPlay()` | `!HasAuthority` 直接返回(客户端只渲染复制实例,不重复生成武器);服务器端生成武器 + 出生无敌 |
| `TakeDamage()` | 同角色:HasAuthority 防护 + 无敌 + 友伤免疫(玩家打 bot、bot 打 bot 同队免疫) |
| `Die(Killer)` | 服务器专属:标记死亡、`OnPawnDeath.Broadcast()`(**触发 GameMode 补位**)、解析击杀者队伍→计分、`Multicast_OnDeath` 广播 ragdoll、延迟销毁 |
| `Multicast_OnDeath_Implementation()` | 全端表现:ragdoll 物理(碰撞 profile + 模拟物理 + 混合权重) |
| `SetTeam()/OnRep_Team()` | 服务器/客户端染色(同角色) |
| `GetLifetimeReplicatedProps()` | 注册 `TeamByte` 复制 |
| `StartShooting/StopShooting` | StateTree 驱动的开火指令(服务器执行) |

---

## 4. `AShooterPlayerController`(Variant_Shooter/ShooterPlayerController.*)

### 对局流程
| 函数 | 职责 |
|---|---|
| `ServerStartMatch_Implementation()` | 客户端点"开始"→ 服务器调 `GameMode->StartPlayerMatch(本人)`——**只让点击者进场**(个人 Ready,不等待他人) |
| `ServerReturnToMainMenu_Implementation()` | 结算"返回菜单"→ 重载关卡 |
| `Client_ShowMainMenu()` | 客户端弹主菜单(UI 模式 + 鼠标) |
| `Client_OnMatchStarted()` | 客户端进场:藏菜单、建 HUD、恢复游戏输入、相机重绑 pawn |
| `Client_ShowEndScreen()` | 结算屏(获胜方/比分/返回按钮) |
| `Client_UpdateScore()` | **比分广播入口**(服务器 ReportKill 时逐个客户端推送 → 本地 HUD 刷新) |
| `OnPossess()` | 绑定 pawn 委托、设队伍、按"是否已 Ready"分流(未进场→弹菜单;已进场→配枪;已结束→不弹) |

### 重生与队伍
| 函数 | 职责 |
|---|---|
| `OnPawnDestroyed()` | 服务器权威重生(客户端跳过):`FindTeamPlayerStart` 找出生点 → SpawnActor(CharacterClass 或 GameMode 默认类兜底)→ Possess;带五类成败日志 |
| `AShooterPlayerController()` 构造 | **CharacterClass 默认加载 `BP_ShooterCharacter`** —— pawn 被销毁时必能重生(解决"丢 pawn") |
| `SetTeam/SetTeamTags` | 队伍/出生标签 |
| `GetLifetimeReplicatedProps()` | 注册 `TeamByte` 复制(客户端 HUD 判断己方 → 比分栏左己右敌) |
| `OnRep_TeamByte()` | 队伍复制到达回调 |

---

## 5. `AShooterTDMGameMode`(Variant_Shooter/ShooterTDMGameMode.*)

### 对局流程
| 函数 | 职责 |
|---|---|
| `PostLogin()` | 加入即 round-robin 分队(0/1 交替)、冻结输入(未点开始不可动)、下发出生标签 |
| `BeginPlay()` | 初始化阶段=MainMenu、清空服务器端 HUD(客户端各自建)、无菜单资产时直接开局 |
| `StartPlayerMatch(PC)` | **个人进场**:标记 Ready → 解冻该玩家 → `Client_OnMatchStarted` → 发武器;首个进场者置 Playing + `FillTeamRoster` + `BP_OnMatchStarted`;随后 `ReplaceBotWithPlayer`(踢一个 AI 顶替) |
| `StartMatch()` | 全局开局兜底(无菜单资产时用) |
| `EndMatch(WinningTeam)` | 置 Ended、广播结算屏、`BP_OnMatchEnded` |
| `ReportKill(KillerTeam)` | 计分(击杀者队伍 +1)→ **遍历所有客户端 `Client_UpdateScore` 广播** → 达 KillTarget 结束(自杀/误杀不计分由 Die 层过滤) |
| `ReturnToMainMenu()` | 重载关卡 |

### AI 补位
| 函数 | 职责 |
|---|---|
| `FillTeamRoster()` | 开局两队各补满 TeamSize 个 bot(忽略人类,满员 AI) |
| `ReplaceBotWithPlayer(PC)` | 玩家进场 → 随机踢掉该队一个存活 AI,人类顶替名额 |
| `RemoveRandomBot(Team)` | 随机销毁该队一个存活 bot |
| `SpawnBot(Team)` | 在队伍出生点生成 bot(带出生无敌、绑定死亡委托) |
| `OnBotDied()` | bot 死亡 → 按 `TeamSize − 人类数 − 存活bot数` 延迟补位(人类占位后自动少补) |
| `CountHumanPlayers()/CountAliveBots()` | 各队人类/存活 bot 计数 |

### 出生点
| 函数 | 职责 |
|---|---|
| `FindTeamPlayerStart(Team)` | **顺序轮询分配出生点**(不堆积):解析 PlayerStart 名字尾部数字,`0-4 = 红方、5-8 = 蓝方`(兼容关卡实际命名 Player0/PlayerStartN);无编号点回退 RED/BLUE 标签 |
| `ChoosePlayerStart_Implementation()` | 玩家出生选点(走 FindTeamPlayerStart) |

### 投射物对象池(消除连续射击卡顿)
| 函数 | 职责 |
|---|---|
| `AcquireProjectile(Class, Transform, Owner, Instigator, NoiseTag)` | 从池取同类型空闲子弹并激活;无空闲则新生成并纳入池 |
| `ReturnProjectile(Projectile)` | 归还池(超上限 64 才真正销毁) |

---

## 5.5 登录场景(Variant_Shooter/ShooterLoginGameMode.* / ShooterLoginPlayerController.*)

| 类 | 职责 |
|---|---|
| `AShooterLoginGameMode` | **纯配置**:构造函数设 `PlayerControllerClass = AShooterLoginPlayerController`。**不创建任何 UI**(GameMode::BeginPlay 只在服务器执行,客户端永远看不到——修复前登录菜单不显示的根因) |
| `AShooterLoginPlayerController::BeginPlay()` | **客户端入口**:`IsLocalController()` 判断后创建 `BP_LoginMenu` → `AddToViewport(100)` → 切 UI 输入模式(鼠标+焦点)。默认 `LoginMenuClass = BP_LoginMenu` |
| `UShooterLoginMenu::StartGame()` | 读取 `ServerAddress/ServerPort`(默认 127.0.0.1:7777)→ `OpenLevel(IP:Port)` 客户端 travel 连接 DS → `BP_OnLoginStarted` |

> **架构要点**: 登录场景是纯客户端入口,UI 归属客户端 PC;GameMode 只做类配置。蓝图在 Lvl_Login 的 WorldSettings 设置 GameMode Override = AShooterLoginGameMode(或 BP 子类)。

---

## 6. `AShooterWeapon`(武器,Variant_Shooter/Weapons/ShooterWeapon.*)

| 函数 | 职责 |
|---|---|
| `AShooterWeapon()` 构造 | `SetReplicates(true)`(武器复制,服务器权威) |
| `StartFiring(AimTarget, MuzzleWorld)` | 客户端:发 `ServerStartFiring(AimTarget, GetMuzzleWorldLocation())`;服务器(bot):直接内部开火 |
| `StopFiring()` | 客户端:发 `ServerStopFiring`;服务器:清连射计时器 |
| `ServerStartFiring_Implementation(AimTarget, MuzzleWorld)` | 服务器:死亡防护(尸体不可开火)→ 存瞄准点/枪口位置 → 开火 |
| `ServerStopFiring_Implementation()` | 服务器:停火 |
| `ServerUpdateAim_Implementation(AimTarget, MuzzleWorld)` | 连射期间刷新瞄准点(Unreliable,50ms 节流) |
| `StartFiringInternal()` | 冷却判定:超过射速立即射;冷却中**无条件补发**(修复"快速连点被吞") |
| `Fire()` | **服务器专属**:用最新 `PendingAimTarget` 定方向、`PendingMuzzleLocation` 定起点(距角色 >500cm 视为异常回退眼睛位置)→ `FireProjectile` → 弹药/噪声/`Multicast_OnFire` |
| `FireProjectile(Target, Start)` | 从 GameMode **对象池**获取子弹(替代 SpawnActor,消除卡顿) |
| `CalculateProjectileSpawnTransform(Target, Start)` | 起点=客户端枪口位置,方向=枪口→瞄准点(子弹从枪口射出、命中准星) |
| `GetMuzzleWorldLocation()` | 客户端武器 FP mesh 枪口 socket 世界位置(本地动画下准确) |
| `Multicast_OnFire_Implementation()` | 全端表现:开火蒙太奇/后坐力/弹药 HUD |
| `Tick()` | 客户端按住射击时每 50ms 上报最新瞄准点(连射跟随准星) |

---

## 7. `AShooterProjectile`(投射物/子弹,Variant_Shooter/Weapons/ShooterProjectile.*)

| 函数 | 职责 |
|---|---|
| `AShooterProjectile()` 构造 | `SetReplicates(true)` + `SetReplicateMovement(true)`(客户端看服务器模拟飞行,碰撞判定只在服务器) |
| `BeginPlay()` | 服务器强制碰撞配置(QueryAndPhysics + WorldDynamic + 全 Block + 对 Pawn Block)——**保证玩家间伤害可判定**;忽略射手本人 |
| `NotifyHit()` | 服务器命中回调:`PROJ_HIT` 日志 → 爆炸/单发 `ProcessHit` → `Multicast_OnHit` 特效 → **回池** |
| `ProcessHit()` | 命中角色且非 owner → `PROJ_DMG` 日志 → `ApplyDamage`(服务器);命中物理体给冲量 |
| `Multicast_OnHit_Implementation()` | 全端命中特效(BP 事件) |
| `ActivateFromPool()` | **复用前彻底重置**:位置/旋转/所有权/忽略列表/`StopMovementImmediately` + 重设 `Velocity` + `UpdateComponentVelocity` + 恢复 tick(不带上一发影响) |
| `DeactivateToPool()` | 停模拟/隐藏/禁碰撞/重置 bHit/取消旧 instigator 忽略 |
| `ReturnToPool()` | 交还 GameMode 池(替代 Destroy) |
| `OnDeferredDestruction()` | 旧销毁路径兜底(保留) |

---

## 8. 关键设计决策与踩坑记录

1. **DS 权威是第一原则**:伤害/死亡/计分/重生/AI 全部服务器;`TakeDamage` 开头 `if (!HasAuthority()) return 0`。
2. **材质实例不复制** → 队伍染色/HP/武器引用必须用 `ReplicatedUsing + OnRep` 在客户端本地执行。
3. **`OnRep` 只在值变化时触发**:首次复制若值==CDO 默认则**不触发** → `TeamByte` 默认值必须设 255(未分配),保证 SetTeam 后 0/1 必触发。
4. **`ReplicatedUsing` 属性必须同时暴露编辑器**(`EditAnywhere`/`BlueprintReadOnly`),否则 UHT 直接崩溃。
5. **服务器端第一人称 mesh 的骨骼/socket 不可靠**(动画不复制)→ 发射起点不用服务器 socket,而是**客户端上报枪口位置**(含距离保护)。
6. **投射物高速可能穿透**:碰撞加固 + 通道强制 Block。
7. **对象池**:命中/超时回池而非 Destroy,复用前 `ResetMoveState/重设 Velocity/取消旧忽略`(注:后续已弃用对象池,改每发新建以规避显示时序问题,见 §5 说明)。
8. **pawn 复活双路径**:正常走 `Revive`(复用 pawn);pawn 被销毁(如蓝图)走 `OnPawnDestroyed` 重生(CharacterClass 构造兜底 + 出生点统一 FindTeamPlayerStart)。
9. **出生点命名**:关卡实际为 `Player0`/`PlayerStartN`,解析规则 0-4 红/5-8 蓝;规范命名 Player1-8 后按 1-4/5-8。
10. **UBA 幻影失败**:本环境构建经常"全部文件秒败",手动用 cl.exe 逐文件编译 + link.exe 链接可绕开(头文件加 UFUNCTION 后须删 `Intermediate/Build/Win64/UnrealEditor/Inc/FPS/UHT` 强制 UHT 重生成)。
11. **客户端 PC 的 `OnPossess` 不执行**(DS 下服务器专用)→ UI 事件订阅与初始同步必须放 `SetPawn`(客户端经 ClientRestart 也会走 SetPawn)。
12. **登录场景 UI 归属客户端**:`GameMode::BeginPlay` 只在服务器执行 → 登录菜单必须由客户端 `AShooterLoginPlayerController::BeginPlay`(IsLocalController)创建,GameMode 只配置 PlayerControllerClass。
13. **DS 打包限制(Launcher 版引擎)**:`InstalledBuild.txt` 存在 → UBT 拒绝 `TargetType.Server`;改名后触发全量重编译且 ThirdParty 缺失必失败;`FPS.exe`(Game 目标)`-server` 无效(`IsRunningDedicatedServer` 对 UE_GAME 硬编码 false)。联机验证走编辑器 `-server` 或 Listen Server;正式 DS 需源码版引擎。
