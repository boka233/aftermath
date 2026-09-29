#include "Aftermath.h"
#include "Modules/ModuleManager.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY(LogAftermath);

namespace
{
	/**
	 * Windows 11 ignores a process's 1 ms timer resolution request (timeBeginPeriod) when it has no visible
	 * window - which is every dedicated server. Sleeps then round up to the 15.6 ms scheduler tick, so a
	 * 30 Hz server that sleeps ~33 ms a frame actually sleeps 46.9 ms and runs at ~21 fps. Opt out of
	 * that power-throttling behaviour so the server keeps its tick rate. Harmless where not supported.
	 */
	void KeepHighResolutionTimer()
	{
#if PLATFORM_WINDOWS
		PROCESS_POWER_THROTTLING_STATE State = {};
		State.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
		State.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
		State.StateMask = 0; // 0 = do NOT ignore our timer resolution request
		const BOOL bOk = ::SetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling, &State, sizeof(State));
		UE_LOG(LogAftermath, Display, TEXT("[Aftermath] TimerResolution keep_1ms=%d"), bOk ? 1 : 0);
#endif
	}
}

/**
 * Primary game module.
 *
 * Iris warns ("BroadcastLoadedModulesUpdated() called while there are 1 active ReplicationSystems")
 * whenever a module loads after the first replication system exists. Some engine modules load lazily
 * only after the default map - and therefore the NetDriver - is up:
 *   server + client (non-Shipping): AutomationWorker, AutomationController, PerfCounters
 *   client: Voice (first local player), GameplayDebugger (first player controller)
 *   editor binaries only: SequenceRecorder, CollisionAnalyzer
 * None of them register net serializers, so it was harmless, but a warning that's "always there" hides
 * real ones. Loading them here, before any world exists, keeps the logs clean. LoadModule on an
 * already-loaded module is a no-op; modules missing from a target are skipped.
 */
class FAftermathModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		// Dedicated servers only need it, but it's cheap and also helps windowed clients in the background.
		KeepHighResolutionTimer();

		FModuleManager& Modules = FModuleManager::Get();
		auto Preload = [&Modules](const TCHAR* Name)
		{
			if (Modules.ModuleExists(Name))
			{
				Modules.LoadModule(Name);
			}
		};

#if !UE_BUILD_SHIPPING
		Preload(TEXT("AutomationWorker"));
		Preload(TEXT("AutomationController"));
		Preload(TEXT("PerfCounters"));
#endif
#if WITH_EDITOR
		// Editor binaries run as -server / -game (test scripts) lazy-load these after the first map.
		Preload(TEXT("SequenceRecorder"));
		Preload(TEXT("CollisionAnalyzer"));
#endif
		// Client-side lazy loads (not present in server builds).
		Preload(TEXT("Voice"));
		Preload(TEXT("GameplayDebugger"));
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FAftermathModule, Aftermath, "Aftermath");
