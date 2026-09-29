#include "Aftermath.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAftermath);

/**
 * Primary game module.
 *
 * Iris warns ("BroadcastLoadedModulesUpdated() called while there are 1 active ReplicationSystems")
 * whenever a module loads after the first replication system exists. Some engine modules load lazily
 * only after the default map - and therefore the NetDriver - is up:
 *   server + client (non-Shipping): AutomationWorker, AutomationController, PerfCounters
 *   client: Voice (first local player), GameplayDebugger (first player controller)
 * None of them register net serializers, so it was harmless, but a warning that's "always there" hides
 * real ones. Loading them here, before any world exists, keeps the logs clean. LoadModule on an
 * already-loaded module is a no-op; modules missing from a target are skipped.
 */
class FAftermathModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
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
		// Client-side lazy loads (not present in server builds).
		Preload(TEXT("Voice"));
		Preload(TEXT("GameplayDebugger"));
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FAftermathModule, Aftermath, "Aftermath");
