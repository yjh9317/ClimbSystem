// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/CustomMovementComponent.h"

#include <DebugHelper.h>

#include "MotionWarpingComponent.h"
#include "Components/CapsuleComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"

void UCustomMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	OwningPlayerAnimInstance = CharacterOwner->GetMesh()->GetAnimInstance();

	if(OwningPlayerAnimInstance)
	{
		OwningPlayerAnimInstance->OnMontageEnded.AddDynamic(this,&UCustomMovementComponent::OnClimbMontageEnded);
		OwningPlayerAnimInstance->OnMontageBlendingOut.AddDynamic(this,&UCustomMovementComponent::OnClimbMontageEnded);
	}

	OwningPlayerCharacter = Cast<AClimbingSystemCharacter>(CharacterOwner);
}

void UCustomMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                             FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
	// const FVector UnrotatedLastInputVector = 
	// UKismetMathLibrary::Quat_UnrotateVector(UpdatedComponent->GetComponentQuat(),GetLastInputVector());
	// UnrotatedLastInputVector.GetSafeNormal().ToString();
	// Debug::Print(UnrotatedLastInputVector.GetSafeNormal().ToString(),FColor::Cyan,1);
}

void UCustomMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	if(IsClimbing())
	{
		bOrientRotationToMovement = false;
		CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(48.f);

		OnEnterClimbStateDelegate.ExecuteIfBound();
	}

	if(PreviousMovementMode == MOVE_Custom && PreviousCustomMode == ECustomMovementMode::MOVE_CLIMB)
	{
		bOrientRotationToMovement = true;
		CharacterOwner->GetCapsuleComponent()->SetCapsuleHalfHeight(96.f);

		const FRotator DirtyRotation = UpdatedComponent->GetComponentRotation();
		const FRotator CleanStandRotation = FRotator(0.f,DirtyRotation.Yaw,0.f);
		UpdatedComponent->SetRelativeRotation(CleanStandRotation);
		
		// FVector Loc = GetActorLocation();
		// UE_LOG(LogTemp,Warning,TEXT("%f %f %f"),DirtyRotation.Vector().X,DirtyRotation.Vector().Y,DirtyRotation.Vector().Z);
		// UE_LOG(LogTemp,Warning,TEXT("%f %f %f"),CleanStandRotation.Vector().X,CleanStandRotation.Vector().Y,CleanStandRotation.Vector().Z);
		// DrawDebugLine(GetWorld(), Loc,Loc + DirtyRotation.Vector() * 1000.f,FColor::Red,false,30);
		// DrawDebugLine(GetWorld(), Loc,Loc + CleanStandRotation.Vector() * 1000.f,FColor::Blue,false,30);
		
		StopMovementImmediately();

		OnExitClimbStateDelegate.ExecuteIfBound();
	}

	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}

void UCustomMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	if(IsClimbing())
	{
		PhysClimb(deltaTime, Iterations);
	}
	Super::PhysCustom(deltaTime, Iterations);
}

float UCustomMovementComponent::GetMaxSpeed() const
{
	if(IsClimbing())
	{
		return MaxClimbSpeed;
	}
	else
	{
		return Super::GetMaxSpeed();
	}
}

float UCustomMovementComponent::GetMaxAcceleration() const
{
	if(IsClimbing())
	{
		return MaxClimbAcceleration;
	}
	else
	{
		return Super::GetMaxAcceleration();
	}
}

FVector UCustomMovementComponent::ConstrainAnimRootMotionVelocity(const FVector& RootMotionVelocity,
	const FVector& CurrentVelocity) const
{
	const bool bIsPlayingRMMontage = IsFalling() && OwningPlayerAnimInstance && OwningPlayerAnimInstance->IsAnyMontagePlaying();

	if(bIsPlayingRMMontage)
	{
		return RootMotionVelocity;
	}
	else
	{
		return Super::ConstrainAnimRootMotionVelocity(RootMotionVelocity, CurrentVelocity);
	}
}

TArray<FHitResult> UCustomMovementComponent::DoCapsuleTraceMultiByObject(const FVector& Start, const FVector& End,
                                                                         bool bShowDebugShape, bool bDrawPersistantShapes)
{
	TArray<FHitResult> OutCapsuleTraceHitResults;
	
	EDrawDebugTrace::Type DebugTraceType= EDrawDebugTrace::None;
	if(bShowDebugShape)
	{
		DebugTraceType = EDrawDebugTrace::ForOneFrame;
		if(bDrawPersistantShapes)
		{
			DebugTraceType = EDrawDebugTrace::Persistent;
		}
	}
	
	UKismetSystemLibrary::CapsuleTraceMultiForObjects(
		this,
		Start,
		End,
		ClimbCapsuleTraceRadius,
		ClimbCapsuleTraceHalfHeight,
		ClimbableSurfaceTraceTypes,
		false,
		TArray<AActor*>(),
		DebugTraceType,
		OutCapsuleTraceHitResults,
		false,
		FLinearColor::Red,
		FLinearColor::Green,
		3
	);

	return OutCapsuleTraceHitResults;
}

FHitResult UCustomMovementComponent::DoLineTraceSingleByObject(const FVector& Start, const FVector& End,
	bool bShowDebugShape, bool bDrawPersistantShapes)
{
	FHitResult OutHit;

	EDrawDebugTrace::Type DebugTraceType= EDrawDebugTrace::None;
	if(bShowDebugShape)
	{
		DebugTraceType = EDrawDebugTrace::ForDuration;
		if(bDrawPersistantShapes)
		{
			DebugTraceType = EDrawDebugTrace::Persistent;
		}
	}

	
	UKismetSystemLibrary::LineTraceSingleForObjects(
	this,
	Start,
	End,
	ClimbableSurfaceTraceTypes,
	false,
	TArray<AActor*>(),
	DebugTraceType,
	OutHit,
	false,FLinearColor::Red,FLinearColor::Green,3);
	return OutHit;
}

bool UCustomMovementComponent::TraceClimbableSurfaces()
{
	const FVector StartOffset = UpdatedComponent->GetForwardVector() * 30.f;  
	const FVector Start = UpdatedComponent->GetComponentLocation() + StartOffset;
	const FVector End = Start + UpdatedComponent->GetForwardVector();

	ClimbableSurfacesTracedResults = DoCapsuleTraceMultiByObject(Start,End,true);

	return !ClimbableSurfacesTracedResults.IsEmpty();
}

FHitResult UCustomMovementComponent::TraceFromEyeHeight(float TraceDistance, float TraceStartOffset,
	bool bShowDebugShape, bool bDrawPersistantShapes)
{
	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
	const FVector EyeHeightOffset = UpdatedComponent->GetUpVector() * (CharacterOwner->BaseEyeHeight + TraceStartOffset);

	const FVector Start = ComponentLocation + EyeHeightOffset;
	const FVector End = Start + UpdatedComponent->GetForwardVector() * TraceDistance;
	

	return DoLineTraceSingleByObject(Start,End,bShowDebugShape,bDrawPersistantShapes);
}

FHitResult UCustomMovementComponent::TraceFromEyeWidth(float StartLocationOffset, float TraceDistance, float TraceStartOffset,
	bool bShowDebugShape, bool bDrawPersistantShapes)
{
	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation()+ UpdatedComponent->GetUpVector() * StartLocationOffset;
	const FVector EyeWidthOffset = UpdatedComponent->GetRightVector() * TraceStartOffset;

	const FVector Start = ComponentLocation + EyeWidthOffset;
	const FVector End = Start + UpdatedComponent->GetForwardVector() * TraceDistance;

	
	
	return DoLineTraceSingleByObject(Start,End,bShowDebugShape,bDrawPersistantShapes);
}


#pragma region ClimbCore

void UCustomMovementComponent::ToggleClimbing(bool bEnableClimb)
{
	if(bEnableClimb)
	{
		if(CanStartClimbing())
		{
			PlayClimbMontage(IdleToClimbMontage);
		}
		else if(CanClimbDownLedge())
		{
			PlayClimbMontage(ClimbDownLedgeMontage);			
		}
		else
		{
			TryStartVaulting();
		}
	}

	if(!bEnableClimb)
	{
		// Stop
		StopClimbing();
	}
}

void UCustomMovementComponent::RequestHopping()
{
	const FVector UnrotatedLastInputVector = 
	UKismetMathLibrary::Quat_UnrotateVector(UpdatedComponent->GetComponentQuat(),GetLastInputVector());

	const float HeightDotResult =	FVector::DotProduct(UnrotatedLastInputVector,FVector::UpVector);
	const float WidthDotResult =	FVector::DotProduct(UnrotatedLastInputVector,FVector::RightVector);
	// Debug::Print(TEXT("Height Dot Result: ") + FString::SanitizeFloat(HeightDotResult));
	// Debug::Print(TEXT("Width Dot Result: ") + FString::SanitizeFloat(WidthDotResult));

	if(HeightDotResult >= 0.9f && WidthDotResult >= -0.1f && WidthDotResult <=0.1f)
	{
		HandleHopUp();
	}
	else if (HeightDotResult <= -0.9f && WidthDotResult >= -0.1f && WidthDotResult <=0.1f)
	{
		HandleHopDown();
	}
	if(WidthDotResult >= 0.9f && HeightDotResult >= -0.1f && HeightDotResult <=0.1f)
	{
		HandleHopRight();
	}
	else if(WidthDotResult <= -0.9f && HeightDotResult >= -0.1f && HeightDotResult <=0.1f)
	{
		HandleHopLeft();
	}
}

bool UCustomMovementComponent::CanStartClimbing()
{
	if(IsFalling()) return false;
	if(!TraceClimbableSurfaces()) return false;	// Capsule 충돌 확인
	if(!TraceFromEyeHeight(100.f,0).bBlockingHit) return false; // Eye LineTrace 충돌 확인
	return true;
}

bool UCustomMovementComponent::CanClimbDownLedge()
{
	if(IsFalling()) return false;

	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
	const FVector ComponentForward = UpdatedComponent->GetForwardVector();
	const FVector DownVector = -UpdatedComponent->GetUpVector();

	const FVector WalkableSurfaceTraceStart = ComponentLocation + ComponentForward * ClimbDownWalkableSurfaceTraceOffset;
	const FVector WalkableSurfaceTraceEnd = WalkableSurfaceTraceStart + DownVector * 100.f;

	FHitResult WalkableSurfaceHit = DoLineTraceSingleByObject(WalkableSurfaceTraceStart,WalkableSurfaceTraceEnd,true);

	const FVector LedgeTraceStart = WalkableSurfaceHit.TraceStart + ComponentForward * ClimbDownLedgeTraceOffset;
	const FVector LedgeTraceEnd = LedgeTraceStart + DownVector * 200.f;

	FHitResult LedgeTraceHit = DoLineTraceSingleByObject(LedgeTraceStart,LedgeTraceEnd,true);

	if(WalkableSurfaceHit.bBlockingHit && !LedgeTraceHit.bBlockingHit)
	{
		return true;
	}
	return false;
}

void UCustomMovementComponent::StartClimbing()
{
	SetMovementMode(MOVE_Custom,ECustomMovementMode::MOVE_CLIMB);
}

void UCustomMovementComponent::StopClimbing()
{
	SetMovementMode(MOVE_Falling);
}

void UCustomMovementComponent::PhysClimb(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}
	/* process all the climbable surfaces info*/
	TraceClimbableSurfaces();
	ProcessClimbableSurfaceInfo();
	
	/* Check if we should stop climbing */
	if(CheckShouldStopClimbing() || CheckHasReachedFloor())
	{
		StopClimbing();
	}
	
	RestorePreAdditiveRootMotionVelocity();

	if( !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() )
	{
		//Define the max climb speed and acceleration
		CalcVelocity(deltaTime, 0.f, true, MaxBreakClimbDeceleration);
	}

	ApplyRootMotionToVelocity(deltaTime);

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = Velocity * deltaTime;
	FHitResult Hit(1.f);

	// Handle climb rotation
	SafeMoveUpdatedComponent(Adjusted, GetClimbRotation(deltaTime), true, Hit);

	if (Hit.Time < 1.f)
	{
		//adjust and try again
		HandleImpact(Hit, deltaTime, Adjusted);
		SlideAlongSurface(Adjusted, (1.f - Hit.Time), Hit.Normal, Hit, true);
	}

	if(!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() )
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;
	}

	/* Snap movement to climbable surfaces */
	SnapMovementToClimbableSurfaces(deltaTime);

	if(CheckHasReachedLedge())
	{
		PlayClimbMontage(ClimbToTopMontage);
	}
}

void UCustomMovementComponent::ProcessClimbableSurfaceInfo()
{
	CurrentClimbableSurfaceLocation = FVector::ZeroVector;
	CurrentClimbableSurfaceNormal = FVector::ZeroVector;

	if(ClimbableSurfacesTracedResults.IsEmpty()) return;

	for(const FHitResult& TracedHitResult : ClimbableSurfacesTracedResults)
	{
		CurrentClimbableSurfaceLocation += TracedHitResult.ImpactPoint;
		CurrentClimbableSurfaceNormal += TracedHitResult.ImpactNormal;
	}

	// Surface Location의 평균 값 구하기
	CurrentClimbableSurfaceLocation /= ClimbableSurfacesTracedResults.Num();
	CurrentClimbableSurfaceNormal = CurrentClimbableSurfaceNormal.GetSafeNormal();

	// Debug::Print(TEXT("ClimbableSurfaceLocation") + CurrentClimbableSurfaceLocation.ToCompactString(),FColor::Cyan,1);
	// Debug::Print(TEXT("ClimbableSurfaceNormal") + CurrentClimbableSurfaceNormal.ToCompactString(),FColor::Red,2);
}

bool UCustomMovementComponent::CheckShouldStopClimbing()
{
	if(ClimbableSurfacesTracedResults.IsEmpty()) return true;

	// 표면의 노말(플레이어의 등방향)과 업벡터를 내적 후 각도를 구해서
	const float DotResult = FVector::DotProduct(CurrentClimbableSurfaceNormal,FVector::UpVector);
	const float DegreeDiff = FMath::RadiansToDegrees(FMath::Acos(DotResult));

	// Debug::Print(TEXT("Degree Diff : ") + FString::SanitizeFloat(DegreeDiff),FColor::Cyan,1);
	
	// 60아래로 되면 중단
	if(DegreeDiff <=60.f)
	{
		return true;
	}
	return false;
}

bool UCustomMovementComponent::CheckHasReachedFloor()
{
	const FVector DownVector = -UpdatedComponent->GetUpVector();
	const FVector StartOffset = DownVector * 50.f;

	const FVector Start = UpdatedComponent->GetComponentLocation() + StartOffset;
	const FVector End = Start + DownVector;

	TArray<FHitResult> PossibleFloorHits = DoCapsuleTraceMultiByObject(Start,End);

	if(PossibleFloorHits.IsEmpty()) return false;

	for(const FHitResult& PossibleFloorHit : PossibleFloorHits)
	{
		// 1. 바닥에 쏜 LineTrace와 UpVector가 평행이면서, 2. 아래로 움직였는지

		const bool bFloorReached = 
		FVector::Parallel(-PossibleFloorHit.ImpactNormal,FVector::UpVector) &&
			GetUnrotatedClimbVelocity().Z < -10.f;

		if(bFloorReached)
		{
			return true;
		}
	}
	return  false;
}

FQuat UCustomMovementComponent::GetClimbRotation(float Deltatime)
{
	const FQuat CurrentQuat = UpdatedComponent->GetComponentQuat();

	if(HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity())
	{
		return CurrentQuat;
	}
	const FQuat TargetQuat = FRotationMatrix::MakeFromX(-CurrentClimbableSurfaceNormal).ToQuat();

	return FMath::QInterpTo(CurrentQuat,TargetQuat,Deltatime,5.f);
}

void UCustomMovementComponent::SnapMovementToClimbableSurfaces(float Deltatime)
{
	const FVector ComponentForward = UpdatedComponent->GetForwardVector();
	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();

	const FVector ProjectedCharacterToSurface = (CurrentClimbableSurfaceLocation - ComponentLocation).ProjectOnTo(ComponentForward);
	const FVector SnapVector = -CurrentClimbableSurfaceNormal * ProjectedCharacterToSurface.Length();

	UpdatedComponent->MoveComponent(
		SnapVector*Deltatime*MaxClimbSpeed,
		UpdatedComponent->GetComponentQuat(),
		true);
}

bool UCustomMovementComponent::CheckHasReachedLedge()
{
	FHitResult LedgeHitResult = TraceFromEyeHeight(100.f,50.f);

	if(!LedgeHitResult.bBlockingHit)
	{
		const FVector WalkableSurfaceTraceStart = LedgeHitResult.TraceEnd;
		
		const FVector DownVector = -UpdatedComponent->GetUpVector();
		const FVector WalkableSurfaceTraceEnd = WalkableSurfaceTraceStart + DownVector * 100.f;

		FHitResult WalkableSurfaceHitResult = 
		DoLineTraceSingleByObject(WalkableSurfaceTraceStart,WalkableSurfaceTraceEnd);

		if(WalkableSurfaceHitResult.bBlockingHit && GetUnrotatedClimbVelocity().Z > 10.f)
		{
			return true;
		}
	}
	return false;
}

void UCustomMovementComponent::TryStartVaulting()
{
	FVector VaultStartPosition;
	FVector VaultLandPosition;
	
	if(CanStartVaulting(VaultStartPosition, VaultLandPosition))
	{
		SetMotionWarpTarget(FName("VaultStartPoint"),VaultStartPosition);
		SetMotionWarpTarget(FName("VaultEndPoint"),VaultLandPosition);

		StartClimbing();
		PlayClimbMontage(VaultMontage);
	}
}

bool UCustomMovementComponent::CanStartVaulting(FVector& OutVaultStartPosition, FVector& OutVaultLandPosition)
{
	if(IsFalling()) return false;

	OutVaultStartPosition = FVector::ZeroVector;
	OutVaultLandPosition = FVector::ZeroVector;

	const FVector ComponentLocation = UpdatedComponent->GetComponentLocation();
	const FVector ComponentForward = UpdatedComponent->GetForwardVector();
	const FVector UpVector = UpdatedComponent->GetUpVector();
	const FVector DownVector = -UpdatedComponent->GetUpVector();

	for(int i = 0 ; i<5;i++)
	{
		const FVector Start = ComponentLocation + UpVector * 100.f + ComponentForward * 80.f * (i + 1);
		const FVector End = Start + DownVector * 100.f * (i+1);

		FHitResult VaultTraceHit =  DoLineTraceSingleByObject(Start,End,true);

		if(i == 0 && VaultTraceHit.bBlockingHit)
		{
			OutVaultStartPosition = VaultTraceHit.ImpactPoint;
		}

		if(i == 3  && VaultTraceHit.bBlockingHit)
		{
			OutVaultLandPosition = VaultTraceHit.ImpactPoint;
		}
	}

	if(OutVaultStartPosition != FVector::ZeroVector && OutVaultLandPosition != FVector::ZeroVector)
	{
		return true;
	}
	else
	{
		return false;	
	}
}

void UCustomMovementComponent::PlayClimbMontage(UAnimMontage* MontageToPlay)
{
	if(!MontageToPlay) return;
	if(!OwningPlayerAnimInstance) return;
	if(OwningPlayerAnimInstance->IsAnyMontagePlaying()) return;

	OwningPlayerAnimInstance->Montage_Play(MontageToPlay);
}

void UCustomMovementComponent::OnClimbMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if(Montage == IdleToClimbMontage || Montage == ClimbDownLedgeMontage)
	{
		StartClimbing();
		StopMovementImmediately();
	}

	if(Montage == ClimbToTopMontage || Montage == VaultMontage)
	{
		SetMovementMode(MOVE_Walking);
	}
}

void UCustomMovementComponent::SetMotionWarpTarget(const FName& InWarpTargetName, const FVector& InTargetPosition)
{
	if(!OwningPlayerCharacter) return;

	OwningPlayerCharacter->GetMotionWarpingComponent()->AddOrUpdateWarpTargetFromLocation(
		InWarpTargetName,
		InTargetPosition);
}

void UCustomMovementComponent::HandleHopUp()
{
	FVector HopUpTargetPoint;
	if(CheckCanHopUp(HopUpTargetPoint))
	{
		SetMotionWarpTarget(FName("HopUpTargetPoint"),HopUpTargetPoint);
		PlayClimbMontage(HopUpMontage);
	}
}

bool UCustomMovementComponent::CheckCanHopUp(FVector& OutHopUpTargetPosition)
{
	FHitResult HopUpHit = TraceFromEyeHeight(100.f,-20.f);
	FHitResult SafetyLedgeHit = TraceFromEyeHeight(100.f,150.f,true);

	if(HopUpHit.bBlockingHit && SafetyLedgeHit.bBlockingHit)
	{
		OutHopUpTargetPosition = HopUpHit.ImpactPoint;
		return true;
	}
	return false;
}

void UCustomMovementComponent::HandleHopDown()
{
	FVector HopDownTargetPoint;

	if(CheckCanHopDown(HopDownTargetPoint))
	{
		SetMotionWarpTarget(FName("HopDownTargetPoint"),HopDownTargetPoint);

		PlayClimbMontage(HopDownMontage);
	}
}

bool UCustomMovementComponent::CheckCanHopDown(FVector& OutHopDownTargetPosition)
{
	FHitResult HopDownHit = TraceFromEyeHeight(100.f,-300.f,true);

	if(HopDownHit.bBlockingHit)
	{
		OutHopDownTargetPosition = HopDownHit.ImpactPoint;
		return true;
	}
	return false;
}

void UCustomMovementComponent::HandleHopLeft()
{
	FVector HopLeftTargetPoint;

	if(CheckCanHopLeft(HopLeftTargetPoint))
	{
		SetMotionWarpTarget(FName("HopLeftTargetPoint"),HopLeftTargetPoint);
		PlayClimbMontage(HopLeftMontage);
	}
}

bool UCustomMovementComponent::CheckCanHopLeft(FVector& OutHopLeftTargetPosition)
{
	FHitResult HopLeftHit = TraceFromEyeWidth(-100.f,100.f,-200.f,true);

	if(HopLeftHit.bBlockingHit)
	{
		OutHopLeftTargetPosition = HopLeftHit.ImpactPoint;
		return true;
	}
	return false;
}

void UCustomMovementComponent::HandleHopRight()
{
	FVector HopRightTargetPoint;

	if(CheckCanHopRight(HopRightTargetPoint))
	{
		SetMotionWarpTarget(FName("HopRightTargetPoint"),HopRightTargetPoint);
		PlayClimbMontage(HopRightMontage);
	}
}

bool UCustomMovementComponent::CheckCanHopRight(FVector& OutHopRightTargetPosition)
{
	FHitResult HopRightHit = TraceFromEyeWidth(-100.f,100.f,200.f,true);

	if(HopRightHit.bBlockingHit)
	{
		OutHopRightTargetPosition = HopRightHit.ImpactPoint;
		return true;
	}
	return false;
}

bool UCustomMovementComponent::IsClimbing() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == ECustomMovementMode::MOVE_CLIMB;
}

FVector UCustomMovementComponent::GetUnrotatedClimbVelocity() const
{
	return UKismetMathLibrary::Quat_UnrotateVector(UpdatedComponent->GetComponentQuat(),Velocity);
}
#pragma endregion 
