# Exercises EC::ProduceCashComponent (code/ec/components/produce_cash.h) through the
# production EC::GameComponents registry over the vendored flecs storage with 1:1
# flecs::entity root lifetime and real CDTimerClass<FrameTimerClass> + CRCEngine:
# native Entity.add<ProduceCashComponent>() attachment, interval payouts, budget clamping,
# upkeep drain, power pause/resume, deconstruction, neutral capture bonuses, two-subpass
# EC01 save/load round-trips, and 4-level deterministic CRC.

opents_add_test(ProduceCash
    NAME producecash
    SOURCES producecashtest.cpp
    ENGINE
        crc.cpp
        ec/world.cpp
    DEFINITIONS WIN32 _WINDOWS _MBCS NOMINMAX
    LIBRARIES flecs_static
)
