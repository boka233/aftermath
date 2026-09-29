"""
Builds the 10 x 10 km World Partition map /Game/Maps/World from code (stage 5).

Run unattended (editor must not already have the project open). It needs a real GPU, NOT -nullrhi: the
landscape's final heightmap is produced by a GPU merge of its edit layers; without it the terrain renders
flat and its bounds are wrong (navigation then ignores it):
  UnrealEditor.exe <project>.uproject -ExecutePythonScript="<abs path>/build_world_map.py" -unattended -nosplash -RenderOffscreen

Everything comes from AftermathTerrainGen (C++, unit-tested) through UAftermathWorldBuilderLibrary:
  - 4033 x 4033 landscape (32 x 32 components, ~2.5 m per quad), split into World Partition streaming proxies
  - sea: a big flat plane at sea level (0), extending past the map edge
  - sky: movable sun + sky atmosphere + sky light + height fog + volumetric clouds
  - settlements: grey-box houses/sheds/hangars on the flattened pads (door on each house)
  - road markers along the road polylines
  - 10 coastal PlayerStarts (tag "Coast"), 1 in the city (tag "City") for tests

Idempotent: deletes and regenerates the map each run. Quits the editor when done.
Judge success by "[WorldMap] RESULT=OK" in the log, not the exit code: with a real RHI the editor can
segfault in D3D12 teardown after "LogExit: Exiting." - everything is saved by then.
"""

import math
import random
import time

import unreal

MAP_PATH = "/Game/Maps/World"
SEED = 1337
VERTS = 4033                 # 32 components x 126 quads + 1
QUADS_PER_SECTION = 63
SECTIONS_PER_COMPONENT = 2
Z_SCALE = 100.0              # +-256 m of range
GRID_SIZE = 2                # components per streaming proxy (2x2 = ~630 m)

CUBE = "/Engine/BasicShapes/Cube.Cube"
PLANE = "/Engine/BasicShapes/Plane.Plane"
GRID_MAT = "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"
WATER_MAT = "/Engine/EngineMaterials/WaterMaterial.WaterMaterial"

lib = unreal.AftermathWorldBuilderLibrary
actor_subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
asset_subsys = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
level_subsys = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

door_cls = unreal.load_class(None, "/Script/Aftermath.AftermathDoor")
pickup_cls = unreal.load_class(None, "/Script/Aftermath.AftermathPickup")
loot_point_cls = unreal.load_class(None, "/Script/Aftermath.AftermathLootSpawnPoint")

counts = {}


def log(msg):
    unreal.log("[WorldMap] " + msg)


def bump(key, n=1):
    counts[key] = counts.get(key, 0) + n


def ground(x, y):
    return lib.get_world_height(SEED, x, y)


def box(label, center, size, folder, rot_yaw=0.0, mat=None):
    """Static cube scaled to `size` (cm) centred at `center`, rotated by yaw."""
    actor = actor_subsys.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center), unreal.Rotator(0, 0, rot_yaw))
    comp = actor.static_mesh_component
    comp.set_static_mesh(cube_mesh)
    comp.set_material(0, mat or grid_mat)
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    bump("boxes")
    return actor


def rot(dx, dy, yaw_deg):
    a = math.radians(yaw_deg)
    return dx * math.cos(a) - dy * math.sin(a), dx * math.sin(a) + dy * math.cos(a)


def house(name, x, y, z, yaw, folder, width=800.0, depth=600.0, height=300.0, wall=20.0, door_w=100.0, with_door=True, loot_tag="House"):
    """Grey-box house: 4 walls + roof, door opening on the local -X wall with an AAftermathDoor."""
    hz = z + height / 2.0

    def at(dx, dy, dz=0.0):
        rx, ry = rot(dx, dy, yaw)
        return (x + rx, y + ry, dz)

    def part(suffix, dx, dy, sx, sy, sz, zc):
        cx, cy, _ = at(dx, dy)
        box(name + suffix, (cx, cy, zc), (sx, sy, sz), folder, yaw)

    part("_N", depth / 2.0, 0.0, wall, width, height, hz)
    part("_E", 0.0, width / 2.0, depth, wall, height, hz)
    part("_W", 0.0, -width / 2.0, depth, wall, height, hz)
    seg = (width - door_w) / 2.0
    part("_S1", -depth / 2.0, -door_w / 2.0 - seg / 2.0, wall, seg, height, hz)
    part("_S2", -depth / 2.0, door_w / 2.0 + seg / 2.0, wall, seg, height, hz)
    lintel_h = height - 220.0
    part("_Lintel", -depth / 2.0, 0.0, wall, door_w, lintel_h, z + 220.0 + lintel_h / 2.0)
    part("_Roof", 0.0, 0.0, depth + wall, width + wall, 20.0, z + height + 10.0)
    # Floor slab so the house sits flat even on a slight slope.
    part("_Floor", 0.0, 0.0, depth, width, 40.0, z - 10.0)
    if with_door:
        dx, dy, _ = at(-depth / 2.0, -door_w / 2.0)
        door = actor_subsys.spawn_actor_from_class(door_cls, unreal.Vector(dx, dy, z + 10.0), unreal.Rotator(0, 0, yaw + 90.0))
        door.set_actor_label(name + "_Door")
        door.set_folder_path(folder)
        bump("doors")
    # Loot spawn points on the floor inside, spread over the room (the economy fills them at runtime).
    if loot_tag:
        # A grid of points on the floor, one per ~20 m2, inset from the walls (60% of the room each way).
        cols = max(1, int(round(width / 500.0)))
        rows = max(1, int(round(depth / 450.0)))
        n = 0
        for r in range(rows):
            for c in range(cols):
                fx = ((r + 0.5) / rows - 0.5) * depth * 0.6
                fy = ((c + 0.5) / cols - 0.5) * width * 0.6
                sx, sy, _ = at(fx, fy)
                loot_point("%s_Loot%d" % (name, n), (sx, sy, z + 25.0), loot_tag, folder)
                n += 1
    bump("houses")


def loot_point(label, loc, tag, folder):
    """Loot economy spawn point (server-only, invisible)."""
    p = actor_subsys.spawn_actor_from_class(loot_point_cls, unreal.Vector(*loc))
    p.set_editor_property("LootTag", tag)
    p.set_actor_label(label)
    p.set_folder_path(folder + "/LootPoints")
    bump("loot_points_" + tag)
    return p


def pickup(label, loc, item_id, folder, count=1):
    p = actor_subsys.spawn_actor_from_class(pickup_cls, unreal.Vector(*loc))
    p.set_editor_property("ItemId", item_id)
    p.set_editor_property("Count", count)
    p.set_actor_label(label)
    p.set_folder_path(folder)
    bump("pickups")
    return p


def player_start(tag, loc, yaw, label):
    ps = actor_subsys.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(*loc), unreal.Rotator(0, 0, yaw))
    ps.set_editor_property("player_start_tag", tag)
    ps.set_actor_label(label)
    ps.set_folder_path("Spawns")
    bump("player_starts")


# ---------------------------------------------------------------------------
# Settlements
# ---------------------------------------------------------------------------

# How many buildings, how far apart. Loot comes from the economy (Content/Data/loot.json) through loot
# spawn points placed inside each building, tagged by building type.
SITE_RULES = {
    "city":     dict(buildings=30, spacing=1500.0),
    "town":     dict(buildings=12, spacing=1500.0),
    "village":  dict(buildings=6,  spacing=1400.0),
    "farm":     dict(buildings=2,  spacing=1400.0),
    "airfield": dict(buildings=4,  spacing=3000.0),
    "base":     dict(buildings=4,  spacing=1800.0),
}


def building_tag(kind, index, rng):
    """Loot tag for a building: military sites are all Military; towns get a clinic and a police station."""
    if kind in ("airfield", "base"):
        return "Military"
    if kind == "farm":
        return "Farm"
    if kind in ("city", "town"):
        if index == 1:
            return "Medical"
        if index == 2:
            return "Police"
        if kind == "city" and index in (3, 4):
            return "Medical" if index == 3 else "Police"
    return "House"


def place_site(site, rng):
    kind = site.kind
    rule = SITE_RULES.get(kind, SITE_RULES["village"])
    cx, cy, cz = site.center.x, site.center.y, site.center.z
    folder = "Sites/%s_%s" % (kind, site.name)
    radius = site.radius * 0.8

    if kind == "airfield":
        # Runway: a long flat slab through the centre, hangars along one side.
        box(site.name + "_Runway", (cx, cy, cz + 5.0), (radius * 2.0, 3000.0, 10.0), folder)
        for i in range(rule["buildings"]):
            hx = cx - radius * 0.6 + i * (radius * 1.2 / max(rule["buildings"] - 1, 1))
            house("%s_Hangar%d" % (site.name, i), hx, cy + 3500.0, cz, 90.0, folder, width=2000.0, depth=1500.0, height=800.0,
                  door_w=400.0, loot_tag="Military")
    else:
        # Buildings on a jittered grid inside the pad, all facing the site centre-ish (door toward the road).
        placed = []
        attempts = 0
        while len(placed) < rule["buildings"] and attempts < rule["buildings"] * 30:
            attempts += 1
            ang = rng.uniform(0, 2 * math.pi)
            r = math.sqrt(rng.uniform(0.02, 1.0)) * radius
            hx, hy = cx + math.cos(ang) * r, cy + math.sin(ang) * r
            if any((hx - px) ** 2 + (hy - py) ** 2 < rule["spacing"] ** 2 for px, py in placed):
                continue
            placed.append((hx, hy))
            yaw = math.degrees(math.atan2(cy - hy, cx - hx)) + 180.0  # door (local -X) faces the centre
            big = kind in ("base",) or (kind == "city" and rng.random() < 0.25)
            house("%s_B%02d" % (site.name, len(placed)), hx, hy, cz, yaw, folder,
                  width=1200.0 if big else 800.0, depth=900.0 if big else 600.0, height=600.0 if big else 300.0,
                  loot_tag=building_tag(kind, len(placed), rng))
        bump("buildings_" + kind, len(placed))


def place_roads(roads):
    # Grey-box road: flat slabs every ~40 m along each polyline, slightly above the ground.
    for ri, road in enumerate(roads):
        pts = road.points
        for i in range(1, len(pts)):
            a, b = pts[i - 1], pts[i]
            dx, dy = b.x - a.x, b.y - a.y
            seg_len = math.hypot(dx, dy)
            steps = max(1, int(seg_len / 4000.0))
            yaw = math.degrees(math.atan2(dy, dx))
            for s in range(steps):
                t = (s + 0.5) / steps
                x, y = a.x + dx * t, a.y + dy * t
                z = ground(x, y)
                if z < 50.0:
                    continue
                box("Road%02d_%03d_%02d" % (ri, i, s), (x, y, z + 3.0), (seg_len / steps, 600.0, 6.0), "Roads/Road%02d" % ri, yaw, road_mat)
                bump("road_slabs")


# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------

def place_environment():
    # Sea: a 30 x 30 km plane at sea level (the map is 10 x 10 km; the rest is horizon).
    sea = actor_subsys.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0))
    comp = sea.static_mesh_component
    comp.set_static_mesh(unreal.load_asset(PLANE))
    comp.set_material(0, water_mat)
    comp.set_collision_profile_name("NoCollision")
    sea.set_actor_scale3d(unreal.Vector(30000.0, 30000.0, 1.0))  # Plane is 100 x 100 uu
    sea.set_actor_label("Sea")
    sea.set_folder_path("Environment")
    # Always loaded (not spatially streamed): one actor covers the whole map.
    sea.set_editor_property("is_spatially_loaded", False)

    sun = actor_subsys.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0.0, 0.0, 100000.0), unreal.Rotator(0, -40, 30))
    sun.set_actor_label("Sun")
    sun.set_folder_path("Environment")
    sun.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    light = sun.get_component_by_class(unreal.DirectionalLightComponent)
    light.set_editor_property("atmosphere_sun_light", True)
    light.set_editor_property("intensity", 10.0)
    sun.set_editor_property("is_spatially_loaded", False)

    for cls, label in ((unreal.SkyAtmosphere, "SkyAtmosphere"), (unreal.SkyLight, "SkyLight"),
                       (unreal.ExponentialHeightFog, "HeightFog"), (unreal.VolumetricCloud, "Clouds")):
        a = actor_subsys.spawn_actor_from_class(cls, unreal.Vector(0.0, 0.0, 0.0))
        a.set_actor_label(label)
        a.set_folder_path("Environment")
        a.set_editor_property("is_spatially_loaded", False)
        if cls == unreal.SkyLight:
            sl = a.get_component_by_class(unreal.SkyLightComponent)
            sl.set_editor_property("real_time_capture", True)
            sl.set_mobility(unreal.ComponentMobility.MOVABLE)


def place_navigation():
    # Invoker-based navmesh still needs bounds: one volume over the whole map (sea floor to hill tops).
    # Tiles are only built around survivors/awake infected (bGenerateNavigationOnlyAroundNavigationInvokers).
    nav = actor_subsys.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0.0, 0.0, 17500.0))
    nav.set_actor_scale3d(unreal.Vector(5000.0, 5000.0, 250.0))   # default brush is 200 uu
    nav.set_actor_label("NavBounds")
    nav.set_folder_path("Navigation")
    nav.set_editor_property("is_spatially_loaded", False)
    origin, extent = nav.get_actor_bounds(False)
    if extent.x < 490000.0 or extent.y < 490000.0:
        raise RuntimeError("NavMeshBoundsVolume has no usable brush (extent %s)" % extent)
    log("nav bounds extent=(%.0f, %.0f, %.0f)" % (extent.x, extent.y, extent.z))


def place_navmesh_actor():
    # A world-partitioned navmesh: the navmesh itself is baked afterwards into streaming chunk actors by
    # the WorldPartitionNavigationDataBuilder commandlet (Tools/build_world_nav.sh). At runtime it only
    # applies modifiers (doors) instead of rebuilding tiles - rebuilding the landscape on a dedicated server
    # doesn't pick up the terrain (see DefaultEngine.ini).
    nav = actor_subsys.spawn_actor_from_class(unreal.RecastNavMesh, unreal.Vector(0.0, 0.0, 0.0))
    nav.set_actor_label("RecastNavMesh-Default")
    nav.set_folder_path("Navigation")
    nav.set_editor_property("is_world_partitioned", True)
    nav.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC_MODIFIERS_ONLY)
    nav.set_editor_property("force_rebuild_on_load", False)
    nav.set_editor_property("tile_size_uu", 5000.0)
    log("navmesh actor: world_partitioned=%s runtime=%s" % (nav.get_editor_property("is_world_partitioned"),
        nav.get_editor_property("runtime_generation")))


def build():
    global cube_mesh, grid_mat, water_mat, road_mat
    t0 = time.time()
    cube_mesh = unreal.load_asset(CUBE)
    grid_mat = unreal.load_asset(GRID_MAT)
    water_mat = unreal.load_asset(WATER_MAT)
    road_mat = unreal.load_asset("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")

    if asset_subsys.does_asset_exist(MAP_PATH):
        asset_subsys.delete_asset(MAP_PATH)
    # World Partition actors live as separate packages next to the map; clear them too.
    for folder in ("/Game/__ExternalActors__/Maps/World", "/Game/__ExternalObjects__/Maps/World"):
        if asset_subsys.does_directory_exist(folder):
            asset_subsys.delete_directory(folder)
    if not level_subsys.new_level(MAP_PATH, True):
        raise RuntimeError("new_level failed")
    log("new World Partition map %s" % MAP_PATH)

    land = lib.create_world_landscape(SEED, VERTS, QUADS_PER_SECTION, SECTIONS_PER_COMPONENT, Z_SCALE, GRID_SIZE, grid_mat)
    if land is None:
        raise RuntimeError("create_world_landscape failed")
    log("landscape done (%.0fs)" % (time.time() - t0))

    place_environment()
    place_navigation()
    place_navmesh_actor()

    rng = random.Random(SEED)
    sites = lib.get_world_sites(SEED)
    for s in sites:
        place_site(s, rng)
        bump("sites_" + s.kind)
    log("sites placed: %d (%.0fs)" % (len(sites), time.time() - t0))

    place_roads(lib.get_world_roads(SEED))
    log("roads placed (%.0fs)" % (time.time() - t0))

    for i, sp in enumerate(lib.get_world_spawns(SEED)):
        player_start("Coast", (sp.location.x, sp.location.y, sp.location.z), sp.yaw, "Spawn_Coast%02d" % i)
    city = [s for s in sites if s.kind == "city"][0]
    player_start("City", (city.center.x - 600.0, city.center.y, city.center.z + 100.0), 0.0, "Spawn_City")

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    elapsed = time.time() - t0
    size_mb = lib.get_content_folder_bytes("/Game") / (1024.0 * 1024.0)
    log("saved %s in %.0fs; content folder %.0f MB; %s" % (MAP_PATH, elapsed, size_mb,
        " ".join("%s=%d" % kv for kv in sorted(counts.items()))))


try:
    build()
    log("RESULT=OK")
except Exception as e:  # noqa: BLE001 - surface any failure to the log for the calling script
    unreal.log_error("[WorldMap] RESULT=FAIL " + str(e))
finally:
    unreal.SystemLibrary.quit_editor()
