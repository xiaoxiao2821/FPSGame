# Overview: ABP_FP_Weapon GetController Null Error Fix

## 问题

PIE 中持续报错（日志累计 9840 次）：
```
蓝图运行时错误："尝试读取非UClass中的(real)属性CallFunc_GetController_ReturnValue，结果为无"
节点: Set PitchN | 蓝图: ABP_FP_Weapon / ABP_FP_Pistol
```

## 根本原因

AnimBP 的 EventGraph 每帧执行 `TryGetPawnOwner → GetController → GetControlRotation → Pitch`，但 `GetController()` 返回 null。

**时序问题**：武器在角色 `BeginPlay` 中被激活（`SetAnimInstanceClass` 设置 AnimBP），但此时 `PlayerController` 尚未 Possess 角色（`OnPossess` 在 `BeginPlay` 之后调用）。分屏 PIE 中第二个玩家角色尤为明显。

## 已完成

1. 在 `AFPSCharacter` 中新增 `GetAimPitch()` 和 `GetAimYaw()` 函数（BlueprintPure, BlueprintThreadSafe）
2. 这两个函数内部使用 `IsValid` 检查 controller，controller 为 null 时安全返回 0.0f
3. 创建修复指南 `fix_guide.md`

## 待用户操作

1. 在 Unreal Editor 中 **Compile** 编译 C++ 代码
2. 打开 `ABP_FP_Weapon` 和 `ABP_FP_Pistol` 的 EventGraph
3. 将 `TryGetPawnOwner → Cast to FPSCharacter → GetController → GetControlRotation → Break → Pitch` 替换为 `TryGetPawnOwner → Cast to FPSCharacter → GetAimPitch()`
4. 保存、编译 AnimBP，重新运行 PIE 验证
