#include "Variant_Shooter/ShooterTDMLoadoutUI.h"
#include "ShooterCharacter.h"

void UShooterTDMLoadoutUI::SetOwningCharacter(AShooterCharacter* Character)
{
	OwningCharacter = Character;
}

void UShooterTDMLoadoutUI::SelectWeapon(TSubclassOf<AShooterWeapon> Weapon)
{
	// Forward the pick to the character; it equips the weapon when the panel closes
	// (on move / fire). If the panel is already closed, this is a no-op.
	if (AShooterCharacter* Char = OwningCharacter.Get())
	{
		Char->SetPendingLoadoutWeapon(Weapon);
	}
}
