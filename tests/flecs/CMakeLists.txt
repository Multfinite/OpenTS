# The harness exercises the EC bridge on its own: world lifetime, attach,
# class tags, parented locomotor entities, replacement, and reset. It compiles
# the two bridge translation units and links the same flecs library the engine
# does, so it needs no game assets.

opents_add_test(FlecsTest
    NAME flecs
    SOURCES main.cpp
    ENGINE
        _flecs.cpp
        flecs/world.cpp
        flecs/register.cpp
    LIBRARIES flecs_static
)
