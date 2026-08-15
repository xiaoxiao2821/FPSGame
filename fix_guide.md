# ABP_FP_Weapon / ABP_FP_Pistol GetController Null 修复指南

## 问题

PIE 运行时持续报错（日志中累计 9840 次）：

```
PIE: Error: 蓝图运行时错误："尝试读取非UClass中的(real)属性CallFunc_GetController_ReturnValue，结果为无"。
节点: Set PitchN  图表: EventGraph  函数: Execute Ubergraph ABP FP Weapon  蓝图: ABP_FP_Weapon
```

同样错误也出现在 `ABP_FP_Pistol`。

## 根本原因

AnimBP 的 EventGraph 每帧执行以下调用链：

```
TryGetPawnOwner() → GetController() → GetControlRotation() → Pitch → Set PitchN
```

**`GetController()` 返回 null**，原因：

1. 角色 `BeginPlay` 中激活武器 → `SetAnimInstanceClass` 设置 AnimBP
2. 此时角色尚未被 `PlayerController` Possess（`OnPossess` 在 `BeginPlay` 之后才调用）
3. AnimBP 立即开始每帧执行，`GetController()` 返回 null
4. 后续节点尝试从 null 读取 `ControlRotation.Pitch` → 运行时错误

在分屏 PIE 中尤为明显（`BP_ShooterCharacter_C_1` 是第二个玩家角色）。

## 已实施的 C++ 修复

已在 `FPSCharacter` 中新增两个 BlueprintPure 函数：

### FPSCharacter.h

```cpp
UFUNCTION(BlueprintPure, BlueprintThreadSafe, Category="Animation")
float GetAimPitch() const;

UFUNCTION(BlueprintPure, BlueprintThreadSafe, Category="Animation")
float GetAimYaw() const;
```

### FPSCharacter.cpp

```cpp
float AFPSCharacter::GetAimPitch() const
{
    if (const AController* Ctrl = GetController())
    {
        return Ctrl->GetControlRotation().Pitch;
    }
    return 0.0f;
}

float AFPSCharacter::GetAimYaw() const
{
    if (const AController* Ctrl = GetController())
    {
        return Ctrl->GetControlRotation().Yaw;
    }
    return 0.0f;
}
```

## 你需要在编辑器中完成的操作

### 步骤 1：编译 C++ 代码

在 Unreal Editor 中点击 **Compile** 按钮（或通过 IDE 编译项目）。

### 步骤 2：修改 AnimBP

打开 `Content/Variant_Shooter/Anims/ABP_FP_Weapon`（和 `ABP_FP_Pistol`）：

1. 进入 **EventGraph**
2. 找到调用链：`TryGetPawnOwner → GetController → GetControlRotation → Break Rotator → Pitch → Set PitchN`
3. 将 `GetController → GetControlRotation → Break Rotator → Pitch` 替换为：
   - 右键搜索 **GetAimPitch**（从 FPSCharacter 类）
   - 或者：`Cast to FPSCharacter` → `GetAimPitch()`
4. 如果使用的是 `TryGetPawnOwner` 返回值，需要先 Cast 到 `FPSCharacter`，然后调用 `GetAimPitch()`

### 替换后的调用链

```
TryGetPawnOwner() → Cast to FPSCharacter → GetAimPitch() → Set PitchN
```

### 步骤 3：同样修改 Yaw 相关逻辑

如果 AnimBP 中还有读取 Yaw 的节点，同样替换为 `GetAimYaw()`。

### 步骤 4：保存并重新编译 AnimBP

保存所有修改的 AnimBP，然后重新运行 PIE 验证错误消失。

## 注意事项

- `BlueprintThreadSafe` 标记确保函数可在 AnimBP 的动画线程中安全调用
- 这个修复是防御性的：即使 Controller 不可用（Possess 前、NPC 未被控制、角色死亡等），AnimBP 也不会崩溃
- 如果 `PitchN` 变量需要归一化处理（如 -1 到 1 范围），在 `Set PitchN` 节点前添加 `Normalize` 或 `MapRange` 节点
