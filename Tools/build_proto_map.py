"""
Builds the stage-1 grey-box map /Game/Maps/Proto from code.

Run unattended (editor must not already have the project open):
  UnrealEditor.exe <project>.uproject -ExecutePythonScript="<abs path>/build_proto_map.py" -unattended -nosplash -nullrhi

The script is idempotent: it regenerates the map from scratch every run. Layout (cm, +X = north):

  - 60m x 60m floor at Z=0
  - 3 houses with a door opening and an AAftermathDoor in it
  - low walls to vault (60 / 100 / 125 cm) and one too-high wall (180 cm)
  - a ramp and a low-ceiling crawl space (prone only)
  - loot pickups inside the houses
  - PlayerStarts tagged StartA..StartD at known spots (used by automated tests via "#StartA")

Finally quits the editor so it can be used from CI / scripts.
"""

import unreal

MAP_PATH = "/Game/Maps/Proto"
CUBE = "/Engine/BasicShapes/Cube.Cube"
GRID_MAT = "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"

actor_subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
asset_subsys = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)

cube_mesh = unreal.load_asset(CUBE)
grid_mat = unreal.load_asset(GRID_MAT)

door_cls = unreal.load_class(None, "/Script/Aftermath.AftermathDoor")
pickup_cls = unreal.load_class(None, "/Script/Aftermath.AftermathPickup")
infected_cls = unreal.load_class(None, "/Script/Aftermath.AftermathInfected")


def log(msg):
    unreal.log("[ProtoMap] " + msg)


def box(label, center, size, folder="Geometry"):
    """Static cube scaled to `size` (cm) centred at `center`."""
    actor = actor_subsys.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center))
    comp = actor.static_mesh_component
    comp.set_static_mesh(cube_mesh)
    comp.set_material(0, grid_mat)
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    return actor


def house(name, origin, width=800.0, depth=600.0, height=300.0, wall=20.0, door_w=100.0):
    """
    Four walls + roof around `origin` (floor-level centre). The south wall (-X side) has a
    door opening centred on Y, with an AAftermathDoor hinged at the opening's left edge.
    """
    ox, oy, oz = origin
    hz = oz + height / 2.0
    folder = "Houses/" + name

    # North (+X) and side walls.
    box(name + "_N", (ox + depth / 2.0, oy, hz), (wall, width, height), folder)
    box(name + "_E", (ox, oy + width / 2.0, hz), (depth, wall, height), folder)
    box(name + "_W", (ox, oy - width / 2.0, hz), (depth, wall, height), folder)

    # South wall split around the door opening (+ lintel above the door).
    seg = (width - door_w) / 2.0
    sx = ox - depth / 2.0
    box(name + "_S1", (sx, oy - door_w / 2.0 - seg / 2.0, hz), (wall, seg, height), folder)
    box(name + "_S2", (sx, oy + door_w / 2.0 + seg / 2.0, hz), (wall, seg, height), folder)
    lintel_h = height - 220.0
    box(name + "_Lintel", (sx, oy, oz + 220.0 + lintel_h / 2.0), (wall, door_w, lintel_h), folder)

    # Roof slab.
    box(name + "_Roof", (ox, oy, oz + height + 10.0), (depth + wall, width + wall, 20.0), folder)

    # Door: hinge at the left edge of the opening; leaf extends along +Y (actor yaw 90 maps local +X to world +Y).
    door = actor_subsys.spawn_actor_from_class(door_cls, unreal.Vector(sx, oy - door_w / 2.0, oz), unreal.Rotator(0, 0, 90))
    door.set_actor_label(name + "_Door")
    door.set_folder_path(folder)

    return (ox, oy, oz)


def infected(label, loc, type_index, yaw):
    """Infected placed in the map (0 civilian, 1 police, 2 soldier)."""
    a = actor_subsys.spawn_actor_from_class(infected_cls, unreal.Vector(*loc), unreal.Rotator(0, 0, yaw))
    a.set_editor_property("TypeIndex", type_index)
    a.set_actor_label(label)
    a.set_folder_path("Infected")
    return a


def pickup(label, loc, item_id, count=1):
    """Loose item. Name, size and stats come from Content/Data/items.json via the item id."""
    p = actor_subsys.spawn_actor_from_class(pickup_cls, unreal.Vector(*loc))
    p.set_editor_property("ItemId", item_id)
    p.set_editor_property("Count", count)
    p.set_actor_label(label)
    p.set_folder_path("Loot")
    return p


def player_start(tag, loc, yaw=0.0):
    ps = actor_subsys.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(*loc), unreal.Rotator(0, 0, yaw))
    ps.set_editor_property("player_start_tag", tag)
    ps.set_actor_label("PlayerStart_" + tag)
    ps.set_folder_path("Spawns")
    return ps


def build():
    if asset_subsys.does_asset_exist(MAP_PATH):
        asset_subsys.delete_asset(MAP_PATH)

    world = unreal.EditorLoadingAndSavingUtils.new_map_from_template(
        "/Engine/Maps/Templates/Template_Default", False)
    if world is None:
        raise RuntimeError("new_map_from_template failed")

    # Template_Default brings sky/sun/fog and a small floor. Replace its floor with ours.
    for a in actor_subsys.get_all_level_actors():
        if isinstance(a, unreal.StaticMeshActor):
            actor_subsys.destroy_actor(a)
        elif isinstance(a, unreal.PlayerStart):
            actor_subsys.destroy_actor(a)

    # Ground: 60m x 60m, top surface at Z=0.
    box("Ground", (0.0, 0.0, -25.0), (6000.0, 6000.0, 50.0), "Geometry")

    # Houses (door on the south side, facing the spawn area).
    house("HouseA", (1200.0, -1200.0, 0.0))
    house("HouseB", (1200.0, 0.0, 0.0))
    house("HouseC", (1200.0, 1200.0, 0.0))

    # Loot inside houses. HouseB keeps only the water bottle (the network test walks in and takes it).
    pickup("Loot_A_Bandage", (1250.0, -1200.0, 15.0), "Bandage", 2)
    pickup("Loot_A_Splint", (1350.0, -1350.0, 15.0), "Splint")
    pickup("Loot_A_Pills", (1350.0, -1050.0, 15.0), "Tetracycline")
    pickup("Loot_B_Water", (1250.0, 0.0, 15.0), "WaterBottle")
    pickup("Loot_C_Beans", (1250.0, 1200.0, 15.0), "CannedBeans")
    pickup("Loot_C_Opener", (1350.0, 1350.0, 15.0), "CanOpener")
    pickup("Loot_C_Backpack", (1350.0, 1050.0, 15.0), "Backpack")
    pickup("Loot_C_Hoodie", (1100.0, 1350.0, 15.0), "Hoodie")
    pickup("Loot_Yard_Knife", (0.0, 300.0, 15.0), "Knife")
    pickup("Loot_Yard_Mag", (-200.0, 500.0, 15.0), "Mag9mm")
    pickup("Loot_Yard_Ammo", (-250.0, 550.0, 15.0), "Ammo9mm", 30)
    pickup("Loot_Yard_Meat", (-300.0, 400.0, 15.0), "RawMeat")
    pickup("Loot_Yard_Soda", (-150.0, 350.0, 15.0), "Soda")

    # Fall-damage tower: a staircase of solid blocks (each step 40cm deep, 25cm higher, walkable)
    # up to an 8m ledge with a 4m ledge halfway. Jump off 4m: hurts. Jump off 8m: breaks legs.
    step_rise, step_run = 25.0, 40.0
    x0, y0 = -2600.0, 1800.0
    for i in range(32):
        top = (i + 1) * step_rise
        box("Stair_%02d" % i, (x0 + i * step_run, y0, top / 2.0), (step_run, 200.0, top), "FallTower")
    box("Ledge_4m", (x0 + 15 * step_run, y0 + 250.0, 400.0 - 10.0), (step_run, 300.0, 20.0), "FallTower")
    box("Ledge_8m", (x0 + 31 * step_run + 120.0, y0, 800.0 - 10.0), (200.0, 200.0, 20.0), "FallTower")

    # Vault course along +X at Y = -2200 (walls face -X).
    walls = [("Wall_60", 60.0, 400.0), ("Wall_100", 100.0, 800.0), ("Wall_125", 125.0, 1200.0), ("Wall_180_TooHigh", 180.0, 1600.0)]
    for label, h, x in walls:
        box(label, (x, -2200.0, h / 2.0), (30.0, 300.0, h), "VaultCourse")

    # Ramp (20 deg) and a platform.
    ramp = box("Ramp", (-800.0, 1800.0, 90.0), (500.0, 300.0, 20.0), "Terrain")
    ramp.set_actor_rotation(unreal.Rotator(20.0, 0.0, 0.0), False)  # roll, pitch, yaw -> pitch 20 via Y
    box("Platform", (-400.0, 1800.0, 170.0), (400.0, 300.0, 20.0), "Terrain")

    # Crawl space: ceiling 100cm above ground (stand 180, crouch 120 won't fit; prone 70 fits).
    box("Crawl_Roof", (-1500.0, -800.0, 110.0), (600.0, 300.0, 20.0), "CrawlSpace")
    box("Crawl_WallL", (-1500.0, -960.0, 50.0), (600.0, 20.0, 100.0), "CrawlSpace")
    box("Crawl_WallR", (-1500.0, -640.0, 50.0), (600.0, 20.0, 100.0), "CrawlSpace")

    # Shooting range at X -2000..-1000 / Y 2600..3400: two lanes, 20 m apart, with a low wall halfway
    # on lane 2 (crouch behind it = covered, stand = exposed) and a tall wall on lane 3 (full cover).
    box("Range_Floor_Mark", (-1500.0, 3000.0, 1.0), (2000.0, 20.0, 2.0), "Range")
    box("Range_LowCover", (-1500.0, 3300.0, 50.0), (30.0, 200.0, 100.0), "Range")
    box("Range_FullCover", (-1500.0, 2700.0, 150.0), (30.0, 200.0, 300.0), "Range")
    pickup("Loot_Range_Pistol", (-2050.0, 3050.0, 15.0), "Pistol")
    pickup("Loot_Range_Rifle", (-2050.0, 2950.0, 15.0), "Rifle")
    pickup("Loot_Range_Mag545", (-2100.0, 3000.0, 15.0), "Mag545")
    pickup("Loot_Range_Ammo545", (-2100.0, 3100.0, 15.0), "Ammo545", 60)
    pickup("Loot_Range_Helmet", (-950.0, 3050.0, 15.0), "Helmet")
    pickup("Loot_Range_Vest", (-950.0, 2950.0, 15.0), "PlateCarrier")
    pickup("Loot_Range_Axe", (-950.0, 3150.0, 15.0), "Axe")
    pickup("Loot_Range_Bat", (-950.0, 2850.0, 15.0), "Bat")

    # Stage 4: navmesh bounds over the whole ground (the server builds the navmesh at load; closed doors
    # cut it, open doors don't), and an infected yard in the NE quadrant, north of the houses.
    nav = actor_subsys.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0.0, 0.0, 200.0))
    # Default brush is a 200 uu cube; scale it to 60 x 60 x 6 m.
    nav.set_actor_scale3d(unreal.Vector(30.0, 30.0, 3.0))
    nav.set_actor_label("NavBounds")
    nav.set_folder_path("Navigation")
    origin, extent = nav.get_actor_bounds(False)
    log("NavBounds extent=%s" % extent)
    if extent.x < 2900.0 or extent.y < 2900.0:
        raise RuntimeError("NavMeshBoundsVolume has no usable brush (extent %s)" % extent)
    # Infected compound: 3 m walls around X 1800..2950 / Y -2950..2950, one gate (a door) in the south
    # wall facing the houses. Infected can't open doors, so they stay inside until a player opens the gate;
    # the walls also keep them from seeing the rest of the map.
    cx0, cx1, cy = 1800.0, 2950.0, 2950.0
    ch, gate_w = 300.0, 100.0
    box("Compound_N", (cx1, 0.0, ch / 2.0), (20.0, cy * 2.0, ch), "Compound")
    box("Compound_E", ((cx0 + cx1) / 2.0, cy, ch / 2.0), (cx1 - cx0, 20.0, ch), "Compound")
    box("Compound_W", ((cx0 + cx1) / 2.0, -cy, ch / 2.0), (cx1 - cx0, 20.0, ch), "Compound")
    seg = cy - gate_w / 2.0
    box("Compound_S1", (cx0, -gate_w / 2.0 - seg / 2.0, ch / 2.0), (20.0, seg, ch), "Compound")
    box("Compound_S2", (cx0, gate_w / 2.0 + seg / 2.0, ch / 2.0), (20.0, seg, ch), "Compound")
    box("Compound_Lintel", (cx0, 0.0, 220.0 + (ch - 220.0) / 2.0), (20.0, gate_w, ch - 220.0), "Compound")
    gate = actor_subsys.spawn_actor_from_class(door_cls, unreal.Vector(cx0, -gate_w / 2.0, 0.0), unreal.Rotator(0, 0, 90))
    gate.set_actor_label("Compound_Gate")
    gate.set_folder_path("Compound")

    # Civilians mill around inside; one police and one soldier among them.
    infected("Inf_Civ_1", (2300.0, -1800.0, 100.0), 0, 180.0)
    infected("Inf_Civ_2", (2500.0, -600.0, 100.0), 0, 200.0)
    infected("Inf_Civ_3", (2400.0, 600.0, 100.0), 0, 160.0)
    infected("Inf_Police", (2600.0, 1600.0, 100.0), 1, 180.0)
    infected("Inf_Soldier", (2700.0, 2400.0, 100.0), 2, 180.0)

    # Stage 4 test lane: a single civilian 20 m from StartG, facing away from it (yaw 180 = -X) so it only
    # reacts once the player makes noise.
    infected("Inf_Lane", (-2000.0, -2600.0, 100.0), 0, 180.0)
    player_start("StartG", (0.0, -2600.0, 100.0), 180.0)       # infected lane: faces the lane civilian

    # Spawns (capsule half height 90 + a bit).
    player_start("StartE", (-2000.0, 3000.0, 100.0), 0.0)     # range: shooter end, facing +X
    player_start("StartF", (-1000.0, 3000.0, 100.0), 180.0)   # range: target end, facing -X
    player_start("StartA", (0.0, 0.0, 100.0), 0.0)            # yard, faces the houses
    player_start("StartB", (0.0, -300.0, 100.0), 0.0)
    player_start("StartC", (0.0, -2200.0, 100.0), 0.0)        # start of the vault course
    player_start("StartD", (500.0, 1200.0, 100.0), 0.0)       # 4m south of HouseC door

    if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
        raise RuntimeError("save_map failed")
    log("Saved " + MAP_PATH + " actors=" + str(len(actor_subsys.get_all_level_actors())))


try:
    build()
    log("RESULT=OK")
except Exception as e:  # noqa: BLE001 - surface any failure to the log for the calling script
    unreal.log_error("[ProtoMap] RESULT=FAIL " + str(e))
finally:
    unreal.SystemLibrary.quit_editor()
