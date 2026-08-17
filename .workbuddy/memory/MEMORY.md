# FPS Project Memory

## 协作约定 (重要)
- **非常规/特殊做法必须先与用户确认**: 凡是不符合常规 UE 开发流程的方案(如 C++ 运行时重绘 UI、无头脚本改蓝图、自动绑定兜底等), 实施前必须提醒用户确认, 不要直接做。用户偏好标准流程: UI/交互在蓝图 Designer 做, C++ 只暴露入口函数。
- **TDM UI 现状**: `UShooterTDMMainMenu`/`UShooterTDMEndScreen` 是纯空壳基类(无按钮成员/无绑定)。按钮接线在蓝图: 主菜单按钮 OnClicked → Get GameMode → Cast BP_TDMGameMode → `ServerStartMatch`; 结算按钮 → `ServerReturnToMainMenu`。结算文本命名 ResultText/ScoreText 则由 C++ `Populate` 自动填充。

## Project Structure
- UE5 FPS project with three variants: base FPS, Horror, Shooter
- Shooter variant has: ShooterCharacter, ShooterWeapon, ShooterPickup, ShooterNPC (AI), ShooterPlayerController
- Character hierarchy: ACharacter → AFPSCharacter → AShooterCharacter / AShooterNPC
- Weapon system: AShooterWeapon (abstract, Blueprint subclasses) with IShooterWeaponHolder interface
- AnimBPs: ABP_FP_Weapon, ABP_FP_Pistol (first person), ABP_TP_Pistol, ABP_TP_Rifle (third person)
- Content in: Content/Variant_Shooter/Anims/

## Known Issues & Fixes
- 2026-08-15: ABP_FP_Weapon/Pistol GetController null error — AnimBP EventGraph calls GetController() before pawn is possessed. Fixed by adding GetAimPitch()/GetAimYaw() BlueprintPure functions to FPSCharacter that null-check the controller. User still needs to replace GetController chain in AnimBP with these functions.
- 2026-08-15: TDM 需 DS 联机架构 — 服务器管状态, 客户端管 UI (Client/Server RPC)。详见当日日志。


## 需求基准
- **TDM 需求文档**: `D:/UnrealProject/FPS/TDM_需求文档.md` 是 TDM 模式唯一效果基准。实现与文档冲突时**先与用户确认再改**; 需求变更需更新文档并升版本号。
- **出生无敌是需求**: 玩家和 AI 出生/重生都有 SpawnProtectionTime(3s)无敌, 不可移除; 开局前 3 秒互相打不掉血属正常。
