#include "Infected/AftermathInfectedManager.h"

#include "Aftermath.h"
#include "Combat/AftermathNoise.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Infected/AftermathInfected.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NavigationSystem.h"
#include "Player/AftermathCharacter.h"
#include "TimerManager.h"

namespace
{
	/** Whole-world tick timing (server cost per frame), for the perf stats line. */
	struct FWorldTickTimer
	{
		TWeakObjectPtr<UWorld> World;
		double Start = 0.0;
		double SumMs = 0.0;
		double MaxMs = 0.0;
		int32 Frames = 0;
		FDelegateHandle StartHandle;
		FDelegateHandle EndHandle;
	};
	TMap<const UAftermathInfectedManager*, TSharedPtr<FWorldTickTimer>> GTickTimers;
}

bool UAftermathInfectedManager::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

TStatId UAftermathInfectedManager::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAftermathInfectedManager, STATGROUP_Tickables);
}

int32 UAftermathInfectedManager::GetRequestedInfectedCount()
{
	int32 N = 0;
	FParse::Value(FCommandLine::Get(), TEXT("-AftermathInfected="), N);
	return FMath::Clamp(N, 0, 2000);
}

void UAftermathInfectedManager::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (UAftermathNoiseSubsystem* Noise = InWorld.GetSubsystem<UAftermathNoiseSubsystem>())
	{
		NoiseHandle = Noise->OnNoise.AddUObject(this, &UAftermathInfectedManager::HandleNoise);
	}

	// Frame-cost timing for the stats line.
	TSharedPtr<FWorldTickTimer> Timer = MakeShared<FWorldTickTimer>();
	Timer->World = &InWorld;
	Timer->StartHandle = FWorldDelegates::OnWorldTickStart.AddLambda([Timer](UWorld* W, ELevelTick, float)
	{
		if (W == Timer->World.Get())
		{
			Timer->Start = FPlatformTime::Seconds();
		}
	});
	Timer->EndHandle = FWorldDelegates::OnWorldTickEnd.AddLambda([Timer](UWorld* W, ELevelTick, float)
	{
		if (W == Timer->World.Get() && Timer->Start > 0.0)
		{
			const double Ms = (FPlatformTime::Seconds() - Timer->Start) * 1000.0;
			Timer->SumMs += Ms;
			Timer->MaxMs = FMath::Max(Timer->MaxMs, Ms);
			++Timer->Frames;
		}
	});
	GTickTimers.Add(this, Timer);

	// Report the runtime navmesh once it has finished building (tests and operators check this).
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&InWorld))
	{
		Nav->OnNavigationGenerationFinishedDelegate.AddDynamic(this, &UAftermathInfectedManager::OnNavBuilt);
	}

	// Extra infected for load tests (-AftermathInfected=N). Wait a moment so the runtime navmesh exists.
	if (GetRequestedInfectedCount() > 0)
	{
		FTimerHandle Handle;
		InWorld.GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateUObject(this, &UAftermathInfectedManager::SpawnFromCommandLine), 3.f, false);
	}
}

void UAftermathInfectedManager::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (UAftermathNoiseSubsystem* Noise = World->GetSubsystem<UAftermathNoiseSubsystem>())
		{
			Noise->OnNoise.Remove(NoiseHandle);
		}
	}
	if (TSharedPtr<FWorldTickTimer>* Timer = GTickTimers.Find(this))
	{
		FWorldDelegates::OnWorldTickStart.Remove((*Timer)->StartHandle);
		FWorldDelegates::OnWorldTickEnd.Remove((*Timer)->EndHandle);
		GTickTimers.Remove(this);
	}
	Super::Deinitialize();
}

void UAftermathInfectedManager::Register(AAftermathInfected* In)
{
	if (In)
	{
		Infected.AddUnique(In);
	}
}

void UAftermathInfectedManager::Unregister(AAftermathInfected* In)
{
	Infected.Remove(In);
}

void UAftermathInfectedManager::GatherPlayers()
{
	Players.Reset();
	for (TActorIterator<AAftermathCharacter> It(GetWorld()); It; ++It)
	{
		if (!It->IsDead())
		{
			Players.Add(*It);
		}
	}
}

void UAftermathInfectedManager::HandleNoise(const FAftermathNoiseEvent& Event)
{
	// Noises made by infected (none yet) don't trigger other infected.
	if (Cast<AAftermathInfected>(Event.Instigator.Get()))
	{
		return;
	}
	const float MaxR2 = FMath::Square(Event.Radius);
	for (const TWeakObjectPtr<AAftermathInfected>& Weak : Infected)
	{
		AAftermathInfected* I = Weak.Get();
		if (I && !I->IsDead() && FVector::DistSquared(I->GetActorLocation(), Event.Location) < MaxR2)
		{
			I->HearNoise(Event);
		}
	}
}

void UAftermathInfectedManager::Roar(AAftermathInfected* Source, AAftermathCharacter* Target)
{
	if (!Source)
	{
		return;
	}
	const FVector From = Source->GetActorLocation();
	const float R2 = FMath::Square(AftermathInfectedMath::RoarRadius);
	int32 Called = 0;
	for (const TWeakObjectPtr<AAftermathInfected>& Weak : Infected)
	{
		AAftermathInfected* I = Weak.Get();
		if (I && I != Source && FVector::DistSquared(I->GetActorLocation(), From) < R2 && I->HearRoar(From, Target))
		{
			++Called;
		}
	}
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] InfectedRoar by=%s target=%s called=%d"),
		*Source->GetName(), *GetNameSafe(Target), Called);
}

void UAftermathInfectedManager::ForceThinkAll()
{
	for (const TWeakObjectPtr<AAftermathInfected>& Weak : Infected)
	{
		if (AAftermathInfected* I = Weak.Get())
		{
			I->bThinkNow = true;
		}
	}
}

void UAftermathInfectedManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double T0 = FPlatformTime::Seconds();

	Infected.RemoveAll([](const TWeakObjectPtr<AAftermathInfected>& W) { return !W.IsValid(); });
	GatherPlayers();

	int32 Thinks = 0;
	int32 Awake = 0;
	for (const TWeakObjectPtr<AAftermathInfected>& Weak : Infected)
	{
		AAftermathInfected* I = Weak.Get();
		if (!I || I->IsDead())
		{
			continue;
		}
		const FVector L = I->GetActorLocation();
		float Nearest2 = TNumericLimits<float>::Max();
		for (const AAftermathCharacter* P : Players)
		{
			Nearest2 = FMath::Min(Nearest2, static_cast<float>(FVector::DistSquared(P->GetActorLocation(), L)));
		}
		const float Nearest = Players.Num() > 0 ? FMath::Sqrt(Nearest2) : TNumericLimits<float>::Max();

		const bool bSleep = !I->bThinkNow && AftermathInfectedMath::ShouldSleep(Nearest, I->GetState());
		I->SetSleeping(bSleep);
		if (bSleep)
		{
			I->ThinkAccum = 0.f;
			continue;
		}
		++Awake;
		I->ThinkAccum += DeltaTime;
		// Half a frame of slack: at 30 Hz a 0.1 s interval means every 3rd frame, not every 4th.
		if (I->bThinkNow || I->ThinkAccum + DeltaTime * 0.5f >= AftermathInfectedMath::ThinkInterval(Nearest, I->GetState()))
		{
			const float Dt = FMath::Max(I->ThinkAccum, DeltaTime);
			I->ThinkAccum = 0.f;
			I->bThinkNow = false;
			I->ServerThink(Dt, Players);
			++Thinks;
		}
	}
	AwakeCount = Awake;
	ThinksLastTick = Thinks;

	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	AvgTickMs = AvgTickMs <= 0.0 ? Ms : FMath::Lerp(AvgTickMs, Ms, 0.05);

	// Periodic stats for load tests and server operators.
	const double Now = GetWorld()->GetTimeSeconds();
	if (Infected.Num() > 0 && Now - StatsLogTime >= 5.0)
	{
		StatsLogTime = Now;
		double WorldAvg = 0.0;
		double WorldMax = 0.0;
		int32 Frames = 0;
		if (TSharedPtr<FWorldTickTimer>* Timer = GTickTimers.Find(this))
		{
			FWorldTickTimer& Tm = **Timer;
			Frames = Tm.Frames;
			WorldAvg = Tm.Frames > 0 ? Tm.SumMs / Tm.Frames : 0.0;
			WorldMax = Tm.MaxMs;
			Tm.SumMs = 0.0;
			Tm.MaxMs = 0.0;
			Tm.Frames = 0;
		}
		int32 Chasing = 0;
		int32 Alive = 0;
		for (const TWeakObjectPtr<AAftermathInfected>& Weak : Infected)
		{
			if (const AAftermathInfected* I = Weak.Get(); I && !I->IsDead())
			{
				++Alive;
				const EAftermathInfectedState S = I->GetState();
				Chasing += (S == EAftermathInfectedState::Chase || S == EAftermathInfectedState::Attack) ? 1 : 0;
			}
		}
		UE_LOG(LogAftermath, Display,
			TEXT("[Aftermath] InfectedStats infected=%d alive=%d awake=%d chasing=%d players=%d frames=%d fps=%.1f world_tick_ms_avg=%.2f world_tick_ms_max=%.2f manager_ms_avg=%.3f"),
			Infected.Num(), Alive, AwakeCount, Chasing, Players.Num(), Frames, Frames / 5.0, WorldAvg, WorldMax, AvgTickMs);
	}
}

void UAftermathInfectedManager::OnNavBuilt(ANavigationData* NavData)
{
	if (bLoggedNavBuilt || !NavData)
	{
		return;
	}
	bLoggedNavBuilt = true;
	const FBox Bounds = NavData->GetBounds();
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] NavMeshReady data=%s bounds=%s"),
		*NavData->GetName(), Bounds.IsValid ? *Bounds.GetExtent().ToCompactString() : TEXT("none"));
}

int32 UAftermathInfectedManager::SpawnAround(const FVector& Center, float Radius, int32 Count, EAftermathInfectedType Type)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const bool bHasNav = Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	int32 Spawned = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		FVector Loc;
		FNavLocation NavLoc;
		if (bHasNav && Nav->GetRandomPointInNavigableRadius(Center, Radius, NavLoc))
		{
			Loc = NavLoc.Location + FVector(0.f, 0.f, 95.f);
		}
		else
		{
			const FVector2D Off = FMath::RandPointInCircle(Radius);
			Loc = Center + FVector(Off.X, Off.Y, 95.f);
		}
		const FTransform Xf(FRotator(0.f, FMath::FRandRange(-180.f, 180.f), 0.f), Loc);
		AAftermathInfected* I = World->SpawnActorDeferred<AAftermathInfected>(AAftermathInfected::StaticClass(), Xf, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding);
		if (!I)
		{
			continue;
		}
		I->SetInfectedType(Type);
		I->FinishSpawning(Xf);
		++Spawned;
	}
	return Spawned;
}

void UAftermathInfectedManager::SpawnFromCommandLine()
{
	if (bSpawnedFromCommandLine)
	{
		return;
	}
	bSpawnedFromCommandLine = true;
	const int32 N = GetRequestedInfectedCount();
	// Mix like a town: mostly civilians, some police, a few soldiers.
	const int32 Soldiers = N / 10;
	const int32 Police = N / 5;
	const int32 Civilians = N - Soldiers - Police;
	const FVector Center(0.f, 0.f, 0.f);
	const float Radius = 2700.f;
	int32 Spawned = 0;
	Spawned += SpawnAround(Center, Radius, Civilians, EAftermathInfectedType::Civilian);
	Spawned += SpawnAround(Center, Radius, Police, EAftermathInfectedType::Police);
	Spawned += SpawnAround(Center, Radius, Soldiers, EAftermathInfectedType::Soldier);
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	UE_LOG(LogAftermath, Display, TEXT("[Aftermath] InfectedLoadSpawn requested=%d spawned=%d navmesh=%d"),
		N, Spawned, (Nav && Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) ? 1 : 0);
}
