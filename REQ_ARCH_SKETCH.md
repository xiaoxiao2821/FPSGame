# 需求拆解与架构草图（V1）

> 目的：把测试需求拆成可执行任务，并基于现有 UE FPS 模板给出架构草图。
> 状态：本版为「方案草稿」，开发方向待你确认后细化。

---

## 一、需求拆解

### 1.1 测试硬性需求（考题原文）
1. 一个**可玩**的 FPS / TPS 射击游戏
2. 具备**完整 Gameplay Loop**
3. 能够**明确判定胜利或失败**
4. 游戏结束后**展示最终结果**

> 玩法、系统设计、表现形式自定；AI 工具允许并鼓励使用。

### 1.2 评分维度 → 我们要"做给评委看"的产出
| 评审维度 | 对应产出 |
|----------|----------|
| 问题拆解与架构设计 | 本文档 + README 架构章 + 依赖图 |
| AI 协作过程 | 完整聊天记录导出（本对话） |
| 代码质量与验证 | UPROPERTY/IsValid 规范；AnimBP 修复案例；真机验证两种结局 |
| 最终交付完整度 | Git 仓库 + Windows Build + README + 聊天记录 + 开发总结 |
| 游戏可玩性 | Gameplay Loop 完整、胜负清晰、操作顺手 |

### 1.3 功能需求分解（模块级）
- **玩家控制**：移动 / 视角 / 射击 / 换弹 / 生命值
- **武器系统**：伤害 / 弹药 / 弹匣 / 拾取
- **敌人**：AI 行为 / 波次生成 / 被击杀计分
- **胜负**：条件判定 + 触发（缺失，本次重点）
- **UI**：HUD（分数 + 弹药）/ 结算屏（缺失，本次重点）
- **流程**：开始 → 进行 → 结束 → 重开（缺失，本次重点）

---

## 二、现有模板能力盘点（基线，非本次新增）

| 模块 | 现有内容 | 状态 |
|------|----------|------|
| 角色 | `ShooterCharacter`（含 HUD 更新钩子） | ✅ 可用 |
| 武器 | `ShooterWeapon` + `IShooterWeaponHolder`（弹药/弹匣） | ✅ 可用 |
| 敌人 AI | `ShooterNPC` + `ShooterNPCSpawner` | ✅ 可用 |
| 控制器 | `ShooterPlayerController`（子弹计数 UI、移动控件） | ✅ 可用 |
| 游戏模式 | `ShooterGameMode`（`TeamScores` / `IncrementTeamScore` / `BP_UpdateScore` / `ChoosePlayerStart` / `ShouldSpawnEnemyNPCs`） | ⚠️ 有计分，无胜负判定 |
| HUD | `ShooterUI`（分数）、`ShooterBulletCounterUI`（弹药） | ✅ 可用，缺结算屏 |
| 动画 | `ABP_FP_Weapon` / `ABP_FP_Pistol` | ⚠️ 存在 GetController null 报错（C++ 已修，待编译+蓝图替换） |

---

## 三、架构草图

### 3.1 分层结构（见附图）
- **Game Framework 层**：GameMode（增强：加 MatchState + 胜负）、PlayerController、Character
- **战斗系统层**：ShooterWeapon + IShooterWeaponHolder、Health/Damage
- **AI 层**：ShooterNPC、ShooterNPCSpawner
- **UI 层**：ShooterUI（分数）、BulletCounterUI（弹药）、**ResultScreen（新增）**
- **流程层（新增）**：MatchFlow 状态机，串起 开始→进行→胜/负→结算→重开

### 3.2 C++ / Blueprint 边界
| 职责 | 实现层 | 理由 |
|------|--------|------|
| 胜负判定、MatchState、计分触发 | **C++**（GameMode） | 每帧/事件级逻辑，确定性要求高 |
| 武器开火、伤害、弹道 | **C++**（Weapon） | 高频逻辑，避免 Blueprint VM 开销 |
| 敌人 AI 决策 | **C++**（NPC） | 同上 |
| HUD 布局、结算屏展示、按钮交互 | **Blueprint Widget** | 表现层，设计师友好 |
| 高层流程编排（状态切换演出） | **Blueprint / C++ 混合** | 流程清晰、易调试 |

### 3.3 新增核心：MatchFlow 状态机
```
NotStarted → Playing → (Win | Lose) → ResultShown
                ↑                        │
                └────── Restart ─────────┘
```
- `Playing` 中持续检查 `CheckWinCondition()` / `CheckLoseCondition()`
- 触发后切到 `ResultShown`，由 `MatchFlow` 通知 `ResultScreen` 显示结果 + 分数
- 「再来一局」→ 重置 GameMode 状态回到 `Playing`

---

## 四、待定开发方向（请选择）

| 方向 | 胜利条件 | 失败条件 | 复杂度 | 展示性 |
|------|----------|----------|--------|--------|
| **A. 清剿模式（推荐）** | 击杀全部敌人 / 达目标分数 | 玩家阵亡且无命 | 低 | 高（一眼验证） |
| B. 限时生存 | 撑过倒计时 | 玩家阵亡 | 中 | 中 |
| C. 计分竞速 | 限时内分数最高 | 时间耗尽 | 中 | 中 |

> 推荐 A：胜负条件单一、可程序化判定，能直接复用现有 `TeamScores`/`IncrementTeamScore`，评委一眼可验证两种结局，且开发量最小、风险最低。

---

## 五、下一步
1. 你确认开发方向（A / B / C）
2. 我据此细化任务拆分（含文件级改动清单）
3. 从 `ShooterGameMode` 胜负判定 + 结算触发开干（叠加模块第一块）
