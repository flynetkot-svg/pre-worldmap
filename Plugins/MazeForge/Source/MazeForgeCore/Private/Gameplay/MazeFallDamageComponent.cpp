#include "Gameplay/MazeFallDamageComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "MazeForgeCore.h"

UMazeFallDamageComponent::UMazeFallDamageComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UMazeFallDamageComponent::BeginPlay()
{
	Super::BeginPlay();

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Maze Fall Damage on %s: it works on a Character only."),
			*GetNameSafe(GetOwner()));
		SetComponentTickEnabled(false);
		return;
	}
	Character->LandedDelegate.AddDynamic(this, &UMazeFallDamageComponent::HandleLanded);
}

void UMazeFallDamageComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->LandedDelegate.RemoveDynamic(this, &UMazeFallDamageComponent::HandleLanded);
	}
	Super::EndPlay(Reason);
}

void UMazeFallDamageComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	// The highest point of the current fall: where it began, or the top of the jump.
	const double Z = Character->GetActorLocation().Z;
	if (Movement->IsFalling())
	{
		ApexZ = bFalling ? FMath::Max(ApexZ, Z) : Z;
		bFalling = true;
	}
	else
	{
		bFalling = false;
	}
}

float UMazeFallDamageComponent::DamageForHeight(float Height) const
{
	if (Height <= SafeHeight)
	{
		return 0.0f;
	}
	if (Height >= LethalHeight)
	{
		return LethalDamage;
	}
	const float Alpha = (Height - SafeHeight) / FMath::Max(1.0f, LethalHeight - SafeHeight);
	return FMath::Lerp(MinDamage, MaxDamage, Alpha);
}

void UMazeFallDamageComponent::HandleLanded(const FHitResult& Hit)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}

	// Landed is told before this tick sees the walking mode: the fall is over here.
	const double LandZ = Character->GetActorLocation().Z;
	const float Height = bFalling ? static_cast<float>(FMath::Max(0.0, ApexZ - LandZ)) : 0.0f;
	bFalling = false;

	const float Damage = DamageForHeight(Height);
	if (Damage <= 0.0f)
	{
		return;
	}

	const bool bLethal = Height >= LethalHeight;
	UE_LOG(LogMazeForge, Log, TEXT("Fall: %s fell %.0f cm — %.0f damage%s."), *Character->GetName(), Height,
		Damage, bLethal ? TEXT(", lethal") : TEXT(""));

	OnFall.Broadcast(Height, Damage, bLethal);
	if (bApplyDamage)
	{
		UGameplayStatics::ApplyDamage(Character, Damage, Character->GetController(), Character,
			UDamageType::StaticClass());
	}
}
