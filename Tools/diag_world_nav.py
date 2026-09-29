"""
Navigation diagnostic for /Game/Maps/World: loads the cells around the city, builds the navmesh there and
logs what the navigation octree holds (does the landscape contribute geometry?).
Run: UnrealEditor.exe <project> -ExecutePythonScript=<abs path>/diag_world_nav.py -unattended -nosplash -nullrhi
"""

import unreal

SEED = 1337


def log(msg):
    unreal.log("[NavDiag] " + msg)


try:
    lib = unreal.AftermathWorldBuilderLibrary
    if not unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/World"):
        raise RuntimeError("load_map failed")
    city = [s for s in lib.get_world_sites(SEED) if s.kind == "city"][0]
    log("city=%s center=(%.0f, %.0f, %.0f)" % (city.name, city.center.x, city.center.y, city.center.z))
    log("result: " + lib.diagnose_navigation_at(city.center, 10000.0))
    # A spot 1 km inland from the city, off the pad (open hills, no buildings).
    hill = unreal.Vector(city.center.x + 100000.0, city.center.y - 100000.0, 0.0)
    hill.z = lib.get_world_height(SEED, hill.x, hill.y)
    log("hill=(%.0f, %.0f, %.0f) result: %s" % (hill.x, hill.y, hill.z, lib.diagnose_navigation_at(hill, 10000.0)))
    log("RESULT=OK")
except Exception as e:  # noqa: BLE001
    unreal.log_error("[NavDiag] RESULT=FAIL " + str(e))
finally:
    unreal.SystemLibrary.quit_editor()
