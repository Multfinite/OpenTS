# Verifies the EC rollout inventory without a compiler: every class the plan
# includes has a class tag registered in code/flecs/register.cpp and an attach
# call in a constructor, and every excluded or undecided class has neither.
# Run through CTest as the entityinventory check; OPENTS_SOURCE_DIR names the
# checkout.

if(NOT OPENTS_SOURCE_DIR)
    message(FATAL_ERROR "OPENTS_SOURCE_DIR is not set")
endif()

set(CODE_DIR "${OPENTS_SOURCE_DIR}/code")

file(GLOB SOURCES "${CODE_DIR}/*.cpp")
set(ALL_SOURCES "")
foreach(source IN LISTS SOURCES)
    file(READ "${source}" contents)
    string(APPEND ALL_SOURCES "${contents}\n")
endforeach()

file(READ "${CODE_DIR}/flecs/register.cpp" REGISTER)

set(INCLUDED
    AITriggerTypeClass AircraftTypeClass AnimTypeClass BuildingTypeClass
    BulletTypeClass CampaignClass HouseTypeClass InfantryTypeClass
    IsometricTileTypeClass OverlayTypeClass ParticleSystemTypeClass
    ParticleTypeClass ScriptTypeClass SideClass SmudgeTypeClass
    SuperWeaponTypeClass TagTypeClass TaskForceClass TeamTypeClass
    TerrainTypeClass TiberiumClass TriggerTypeClass UnitTypeClass
    VoxelAnimTypeClass WarheadTypeClass WeaponTypeClass
    AircraftClass BuildingClass InfantryClass UnitClass
    BuildingLightClass BulletClass OverlayClass ParticleClass
    ParticleSystemClass SmudgeClass TerrainClass VeinholeMonsterClass
    VoxelAnimClass WaveClass
    AlphaShapeClass CellClass EMPulseClass FactoryClass FoggedObjectClass
    HouseClass LightSourceClass NeuronClass ScriptClass SuperClass TagClass
    TeamClass TriggerClass TubeClass WaypointPathClass
    DriveLocomotionClass DropPodLocomotionClass FlyLocomotionClass
    HoverLocomotionClass JumpjetLocomotionClass LevitateLocomotionClass
    MechLocomotionClass TeleportLocomotionClass TunnelLocomotionClass
    WalkLocomotionClass
)

set(EXCLUDED AnimClass IsometricTileClass LogicClass TActionClass TEventClass)
set(UNDECIDED Tactical)

foreach(class IN LISTS INCLUDED)
    string(FIND "${REGISTER}" "Register_Tag(\"${class}\")" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${class} has no Register_Tag in code/flecs/register.cpp")
    endif()
    string(FIND "${ALL_SOURCES}" ", \"${class}\");" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${class} has no attach call in a constructor")
    endif()
endforeach()

foreach(class IN LISTS EXCLUDED UNDECIDED)
    string(FIND "${REGISTER}" "\"${class}\"" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "${class} is excluded or undecided but is registered in code/flecs/register.cpp")
    endif()
    string(FIND "${ALL_SOURCES}" ", \"${class}\");" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR "${class} is excluded or undecided but has an attach call")
    endif()
endforeach()

list(LENGTH INCLUDED count)
message(STATUS "entity inventory: ${count} included classes verified")
