# Exercises the static C++20 entity-component layer over the real vendored flecs storage:
# lazy TypedEntity<Root> materialization across ObjectClass, CellClass, and LocomotionClass,
# compile-time ComponentRegistry<Ts...> recursion for all 39 global and entity hooks,
# flecs::Disabled suspension for limbo and piggybacked locomotors, the two-subpass EC01
# save trailer with pointer swizzle stability, and all four levels of Compute_CRC.

opents_add_test(ECTest
    NAME ec
    SOURCES ectest.cpp
    ENGINE
        crc.cpp
        ec/world.cpp
    DEFINITIONS WIN32 _WINDOWS _MBCS NOMINMAX
    LIBRARIES flecs_static
)
