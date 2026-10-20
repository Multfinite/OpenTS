# The harness exercises the EC bridge on its own: world lifetime, attach,
# class tags, parented locomotor entities, replacement, C++20 ComponentOps
# lifecycle dispatch, lockstep CRC folding, save stream framing, and reset.
# It links only the bridge translation units, crc.cpp, and flecs_static, so
# it runs standalone with no game assets.

opents_add_test(FlecsTest
    NAME flecs
    SOURCES main.cpp
    ENGINE
        _flecs.cpp
        crc.cpp
        flecs/world.cpp
        flecs/register.cpp
    LIBRARIES flecs_static
)
