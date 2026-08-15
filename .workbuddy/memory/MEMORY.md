# FPS Project Memory

## Project Structure
- UE5 FPS project with three variants: base FPS, Horror, Shooter
- Shooter variant has: ShooterCharacter, ShooterWeapon, ShooterPickup, ShooterNPC (AI), ShooterPlayerController
- Character hierarchy: ACharacter → AFPSCharacter → AShooterCharacter / AShooterNPC
- Weapon system: AShooterWeapon (abstract, Blueprint subclasses) with IShooterWeaponHolder interface
- AnimBPs: ABP_FP_Weapon, ABP_FP_Pistol (first person), ABP_TP_Pistol, ABP_TP_Rifle (third person)
- Content in: Content/Variant_Shooter/Anims/

## Known Issues & Fixes
- 2026-08-15: ABP_FP_Weapon/Pistol GetController null error — AnimBP EventGraph calls GetController() before pawn is possessed. Fixed by adding GetAimPitch()/GetAimYaw() BlueprintPure functions to FPSCharacter that null-check the controller. User still needs to replace GetController chain in AnimBP with these functions.
