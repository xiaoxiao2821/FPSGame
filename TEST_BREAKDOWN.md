# Gameplay Programmer 编程测试 V2.0 —— 拆解与对齐分析

> 分析对象：`Gameplay Programmer-编程测试题-V2.0.md`（Studio Surgical Scalpels）
> 分析目的：把模糊需求拆成可执行的开发任务，并把你现有的 `D:\UnrealProject\FPS` 项目与测试要求做缺口对齐。
> ⚠️ 本文件是规划草稿，提交前可保留、改名或删除，不影响游戏本体。

---

## 1. 这份测试「真实考什么 / 不考什么」

它不是算法考试，是**真实项目开发流程模拟**。核心只有三件事：

1. **工程拆解能力** —— 能否把"做个射击游戏"这种模糊需求拆成可执行任务。
2. **AI 协作效率** —— 能否用 AI 加速开发、定位问题、验证方案（不是让 AI 代写）。
3. **验证与交付意识** —— 能否产出**可运行的完整产物**，而非未完成的片段。

代码水平是 baseline（门槛），**process 才是得分点**。评委要看的是"你怎么干活"，不是"你写了多牛的算法"。

---

## 2. 提交物逐条拆解 + 隐藏陷阱

| # | 提交物 | 重点 / 陷阱 |
|---|--------|------------|
| 1 | Git Repository / 源码 | 你之前已解决 `.gitignore`、`.gitattributes`、LFS、分支名、push 超时等一堆坑，**基础设施已就绪**，直接用。 |
| 2 | **Windows Build** | 必须**能直接运行**，不止"代码能编译"。要验证 EXE 双击能进游戏、胜负能触发。 |
| 3 | README | 不能只写"怎么编译"。要写清**需求拆解思路 + 架构 + 如何运行 + 已知限制**。 |
| 4 | **AI 全部聊天记录（原始导出）** | 🔥 **这是被评分的提交物！** 评委要看你"怎么与 AI 协作、怎么迭代、怎么验证"。本对话（WorkBuddy 历史）就是素材，记得导出原始记录。 |
| 5 | 开发总结（建议） | 加分项。用来正面展示你的拆解方法、AI 协作路径、验证严谨性——正好对应评审维度 1/2/3。 |

**最容易被忽视的陷阱**：第 4 条。很多人只交代码，结果评委看不到"协作过程"这一整块分数。务必保留完整聊天导出。

---

## 3. 五个评审维度 → 你应该「做给评委看」什么

1. **问题拆解与架构设计** → 在 README / 总结里画出任务拆分树、模块依赖（C++/蓝图层）。
2. **AI 协作过程** → 聊天记录体现"提问→AI 给方案→你验证→迭代"的闭环。
3. **代码质量与验证** → 可读性、UPROPERTY/IsValid 规范；**把 AnimBP GetController null 修复作为"验证 AI 输出"的案例写进总结**（UHT 非法说明符那次就是典型）。
4. **最终交付完整度** → README + Build + 源码三件套齐全、一键可跑。
5. **游戏可玩性** → Gameplay Loop 完整、胜负判定清晰、操作顺手。

---

## 4. 现有 FPS 项目 vs 测试要求：缺口对齐（核心）

### ✅ 已具备（强基础，省下大量时间）
- `ShooterCharacter` —— 角色、HUD 更新钩子
- `ShooterWeapon` + `IShooterWeaponHolder` —— 武器、弹药、弹匣
- `ShooterNPC`（AI 敌人）+ `ShooterNPCSpawner` —— 敌人 AI 与生成
- `ShooterPlayerController` —— 子弹计数器 UI、移动控件
- `ShooterGameMode` —— `TeamScores`（TMap）、`IncrementTeamScore`、`BP_UpdateScore`、`ChoosePlayerStart`、`ShouldSpawnEnemyNPCs`
- `ShooterUI`（计分）、`ShooterBulletCounterUI`（弹药） —— HUD 框架已搭

### ❌ 缺口（必须补齐才能满足"可玩 + 胜负 + 结算"）
| 缺口 | 测试要求对应 | 建议做法 |
|------|-------------|---------|
| **胜负判定逻辑缺失** | "能够明确判定胜利或失败" | `ShooterGameMode` 里无 `GameState`/`EndMatch`/胜利条件检查。需加：`CheckWinCondition()`（如击杀数达上限 / 敌人清空）、`CheckLoseCondition()`（玩家阵亡且无命）。 |
| **结算界面缺失** | "游戏结束后展示最终结果" | 现有只有计分/弹药 HUD，**无 Victory/Defeat 结果屏**。需新增 `WBP_ResultScreen`（显示胜/负 + 分数 + 重开按钮）。 |
| **完整 Gameplay Loop 编排缺失** | "具备完整 Gameplay Loop" | 当前似为直接生成开打，无"开始→进行→结束→结算→重开"的状态机。建议用 `AGameMode` 的 MatchState 或自定义状态枚举串起流程。 |
| **README / 开发总结缺失** | 提交物 3、5 | 需新建。 |
| ⚠️ AnimBP 报错（已知） | 影响"可玩性 + 验证" | `ABP_FP_Weapon`/`ABP_FP_Pistol` 的 `GetController` null 报错我们已在 C++ 加了 `GetAimPitch/GetAimYaw`，**待编译 + 蓝图替换节点**后才彻底消掉。 |

---

## 5. 推荐 Gameplay Loop 设计（给评委的"清晰可玩性"）

**模式建议：清剿 / 生存模式（最易判定胜负）**
- **胜利**：击杀全部敌人（或达到目标分数）。
- **失败**：玩家阵亡且无剩余复活次数。
- **流程**：主菜单 → 开始 → 生成敌人 → 战斗 → 胜/负触发 → 结算屏（结果 + 分数 + 「再来一局」）→ 重开。

**为什么这个设计好交**：胜负条件单一、可程序化判定、评委一眼能验证，且能复用你已有的 `TeamScores`/`IncrementTeamScore` 体系（再加一个"击杀目标"常量即可）。

---

## 6. 24 小时开发节奏建议

| 阶段 | 内容 | 时长 |
|------|------|------|
| Phase 1 | 需求拆解 + 架构草图（写进 README 骨架） | 2–3h |
| Phase 2 | 核心 Gameplay Loop：生成、战斗、武器手感 | 8–10h |
| Phase 3 | 胜负判定 + 结算界面 + 重开 | 3–4h |
| Phase 4 | Windows Build + 真机验证 + README + 开发总结 | 3–4h |
| 全程 | **保留 AI 聊天记录导出**（本对话） | — |

---

## 7. 给评委的加分动作（低成本高回报）

1. README 里显式写出**任务拆分树**与**架构图**（哪怕文字版）。
2. 导出**完整原始聊天记录**作为提交物 4。
3. 开发总结里讲清：如何拆解 → 如何与 AI 协作 → 如何验证 AI 输出（用 AnimBP 修复案例佐证）。
4. 把"验证"做扎实：Build 后实机跑一遍胜/负两种结局并截图，放进 README。

---

## 8. 你下一步可以马上做的

- [ ] 确认胜负模式（清剿 / 限时 / 计分），定下"胜利条件"常量
- [ ] 在 `ShooterGameMode` 加 `CheckWin/LoseCondition` + 触发结算
- [ ] 新增 `WBP_ResultScreen` 蓝图 Widget
- [ ] 编译 C++ 并在蓝图里替换 AnimBP 的 `GetController` 节点（消报错）
- [ ] 出 Windows Build 并真机验证两种结局
- [ ] 写 README + 开发总结，导出聊天记录
