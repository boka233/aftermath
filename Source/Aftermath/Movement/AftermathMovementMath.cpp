#include "Movement/AftermathMovementMath.h"

float FAftermathVaultPath::TotalPathLength() const
{
	const float A = FMath::Sqrt(FMath::Square(RiseEnd) + FMath::Square(PeakZ - StartZ));
	const float B = HoldEnd - RiseEnd;
	const float C = FMath::Sqrt(FMath::Square(Length - HoldEnd) + FMath::Square(PeakZ - EndZ));
	return A + B + C;
}

FVector FAftermathVaultPath::Evaluate(float Traveled) const
{
	const float A = FMath::Sqrt(FMath::Square(RiseEnd) + FMath::Square(PeakZ - StartZ));
	const float B = HoldEnd - RiseEnd;
	const float C = FMath::Sqrt(FMath::Square(Length - HoldEnd) + FMath::Square(PeakZ - EndZ));

	const float D = FMath::Clamp(Traveled, 0.f, A + B + C);

	float S = 0.f; // horizontal distance
	float Z = StartZ;

	if (D <= A)
	{
		const float T = A > UE_KINDA_SMALL_NUMBER ? D / A : 1.f;
		S = RiseEnd * T;
		Z = FMath::Lerp(StartZ, PeakZ, T);
	}
	else if (D <= A + B)
	{
		S = RiseEnd + (D - A);
		Z = PeakZ;
	}
	else
	{
		const float T = C > UE_KINDA_SMALL_NUMBER ? FMath::Min((D - A - B) / C, 1.f) : 1.f;
		S = FMath::Lerp(HoldEnd, Length, T);
		Z = FMath::Lerp(PeakZ, EndZ, T);
	}

	FVector Result = Start + Forward * S;
	Result.Z = Z;
	return Result;
}

FAftermathVaultPath FAftermathVaultPath::Build(const FVector& StartCenter, const FVector& Forward,
	float WallFrontDist, float WallBackDist, float WallTopZ, float EndCenterZ,
	float CapsuleRadius, float CapsuleHalfHeight, float Clearance)
{
	// Small skin so the capsule never grazes the wall faces during the sweep.
	constexpr float Skin = 3.f;

	FAftermathVaultPath Path;
	Path.Start = StartCenter;
	Path.Forward = Forward.GetSafeNormal2D();
	Path.RiseEnd = FMath::Max(0.f, WallFrontDist - CapsuleRadius - Skin);
	Path.HoldEnd = FMath::Max(Path.RiseEnd, WallBackDist + CapsuleRadius + Skin);
	Path.Length = FMath::Max(Path.HoldEnd, LandingDistance(WallBackDist, CapsuleRadius));
	Path.StartZ = StartCenter.Z;
	Path.PeakZ = FMath::Max(StartCenter.Z, WallTopZ + CapsuleHalfHeight + Clearance);
	Path.EndZ = EndCenterZ;
	return Path;
}

namespace AftermathMath
{
	float ComputeMaxGroundSpeed(const FAftermathSpeedConfig& Config, EAftermathStance Stance,
		bool bWantsSprint, bool bWantsWalk, bool bMovingForward)
	{
		switch (Stance)
		{
		case EAftermathStance::Prone:
			return Config.Prone;

		case EAftermathStance::Crouched:
			return bWantsWalk ? Config.CrouchWalk : Config.Crouch;

		case EAftermathStance::Standing:
		default:
			if (bWantsSprint && bMovingForward)
			{
				return Config.Sprint;
			}
			return bWantsWalk ? Config.Walk : Config.Jog;
		}
	}

	bool IsWithinInteractRange(const FVector& Eye, const FVector& Target, float Range, float Tolerance)
	{
		const float Limit = FMath::Max(0.f, Range + Tolerance);
		return FVector::DistSquared(Eye, Target) <= FMath::Square(Limit);
	}

	const TCHAR* StanceToString(EAftermathStance Stance)
	{
		switch (Stance)
		{
		case EAftermathStance::Standing: return TEXT("Standing");
		case EAftermathStance::Crouched: return TEXT("Crouched");
		case EAftermathStance::Prone:    return TEXT("Prone");
		default:                         return TEXT("Unknown");
		}
	}
}
