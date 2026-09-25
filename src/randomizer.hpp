#pragma once

#include "f_op/f_op_actor.h"

#include <cstdint>

namespace enemy_randomizer {

struct FloatRange {
    float minimum = 1.0f;
    float maximum = 1.0f;
};

struct Settings {
    bool enabled = true;
    bool scaleGravityWithMovement = true;
    bool noticeRangeUsesSize = false;
    std::uint32_t seed = 12345;

    FloatRange size{0.5f, 2.0f};
    FloatRange movementSpeed{0.5f, 2.0f};
    FloatRange health{0.5f, 2.0f};
    FloatRange attackDamage{0.5f, 2.0f};
    FloatRange noticeRange{0.5f, 2.0f};
    FloatRange playerKnockback{0.5f, 2.0f};
    // Temporarily fixed at vanilla until the remaining actor timer issues are
    // audited. The setting remains part of the data model for later use.
    FloatRange stunDuration{1.0f, 1.0f};

    // For each range crossing 1.0, use a 50/50 below/above-vanilla split only
    // when its upper side is wider. Other ranges keep their uniform sampling.
    // Set false to restore the original sampling for every attribute.
    bool balanceWideUpperRanges = true;
};

struct Attributes {
    float size = 1.0f;
    float movementSpeed = 1.0f;
    float health = 1.0f;
    float attackDamage = 1.0f;
    float noticeRange = 1.0f;
    float playerKnockback = 1.0f;
    float stunDuration = 1.0f;
};

// Called by mod.cpp. Changing settings affects actors first seen afterward;
// existing actors keep the values that were generated for them.
void configure(const Settings& settings);

// This is a live global policy rather than a randomized per-actor value.
bool scale_gravity_with_movement();

// When true, the effective notice multiplier is the actor's size multiplier.
bool notice_range_uses_size();

// Returns a COPY of this actor's stable values. Rolls use stage/room placement
// identity rather than the session-local process ID. Do not store a reference.
Attributes attributes_for(fopAc_ac_c* actor);

// Call this from the enemy's Delete hook so a recycled process ID cannot
// inherit an old actor's random values.
void forget(fopAc_ac_c* actor);

// Clears all session-local actor records during mod shutdown/reload.
void clear();

}  // namespace enemy_randomizer
