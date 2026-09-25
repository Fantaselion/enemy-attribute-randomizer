#include "randomizer.hpp"

#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_name.h"
#include "mods/svc/log.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace enemy_randomizer {
namespace {

struct ActorRecord {
    fpc_ProcID id;
    fopAc_ac_c* address;
    Attributes attributes;
};

Settings g_settings;
std::vector<ActorRecord> g_actorRecords;

FloatRange normalized(FloatRange range) {
    if (range.minimum > range.maximum) {
        std::swap(range.minimum, range.maximum);
    }

    // Zero/negative multipliers are rarely meaningful and frequently break
    // collision, animation, or divisions in vanilla code.
    range.minimum = std::max(range.minimum, 0.01f);
    range.maximum = std::max(range.maximum, range.minimum);
    return range;
}

std::uint32_t mix(std::uint32_t value) {
    // A compact integer avalanche hash. This is deterministic and does not
    // consume one global RNG stream, so asking for one attribute cannot shift
    // every later actor's results.
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

float unit_float(std::uint32_t value) {
    // Keep the top 24 bits, which a float can represent cleanly here.
    return static_cast<float>(mix(value) >> 8) * (1.0f / 16777215.0f);
}

float random_in_range(
    FloatRange range,
    std::uint32_t actorKey,
    std::uint32_t attributeSalt
) {
    const float t = unit_float(actorKey ^ attributeSalt);

    // Only rebalance ranges that cross vanilla and have a wider upper side.
    // For example, 0.25-4.0 becomes 50/50 below/above 1.0; 0.25-1.25
    // stays uniform over the full range (75% below, 25% above).
    if (g_settings.balanceWideUpperRanges &&
        range.minimum < 1.0f && range.maximum > 1.0f &&
        (range.maximum - 1.0f) > (1.0f - range.minimum))
    {
        // Reuse the same deterministic draw: each half of t selects one side,
        // then remap that half to a uniform draw within its selected interval.
        if (t < 0.5f) {
            return range.minimum + ((1.0f - range.minimum) * (t * 2.0f));
        }

        return 1.0f + ((range.maximum - 1.0f) * ((t - 0.5f) * 2.0f));
    }

    // Preserve the original values when the option is off or doesn't apply.
    return range.minimum + ((range.maximum - range.minimum) * t);
}

Attributes generate_for(fopAc_ac_c* actor) {
    const std::uint32_t id = fopAcM_GetID(actor);
    const std::uint32_t profile =
        static_cast<std::uint16_t>(fopAcM_GetName(actor));

    const std::uint32_t actorKey = mix(
        g_settings.seed ^ id ^ (profile << 16)
    );

    return Attributes{
        .size = random_in_range(g_settings.size, actorKey, 0xA341316Cu),
        .movementSpeed = random_in_range(
            g_settings.movementSpeed,
            actorKey,
            0xC8013EA4u
        ),
        .health = random_in_range(g_settings.health, actorKey, 0x7E95761Eu),
        .attackDamage = random_in_range(
            g_settings.attackDamage,
            actorKey,
            0x3C6EF372u
        ),
        .noticeRange = random_in_range(
            g_settings.noticeRange,
            actorKey,
            0x9E3779B9u
        ),
        .playerKnockback = random_in_range(
            g_settings.playerKnockback,
            actorKey,
            0xBB67AE85u
        ),
        .stunDuration = random_in_range(
            g_settings.stunDuration,
            actorKey,
            0x1B873593u
        ),
    };
}

}  // namespace

void configure(const Settings& settings) {
    g_settings = settings;
    g_settings.size = normalized(g_settings.size);
    g_settings.movementSpeed = normalized(g_settings.movementSpeed);
    g_settings.health = normalized(g_settings.health);
    g_settings.attackDamage = normalized(g_settings.attackDamage);
    g_settings.noticeRange = normalized(g_settings.noticeRange);
    g_settings.playerKnockback = normalized(g_settings.playerKnockback);
    g_settings.stunDuration = normalized(g_settings.stunDuration);
}

bool scale_gravity_with_movement() {
    return g_settings.enabled && g_settings.scaleGravityWithMovement;
}

bool notice_range_uses_size() {
    return g_settings.noticeRangeUsesSize;
}

Attributes attributes_for(fopAc_ac_c* actor) {
    if (actor == nullptr || !g_settings.enabled) {
        return Attributes{};
    }

    const fpc_ProcID id = fopAcM_GetID(actor);

    for (ActorRecord& record : g_actorRecords) {
        if (record.id == id && record.address == actor) {
            return record.attributes;
        }
    }

    // If a Delete hook was missed, still defend against an ID being recycled
    // for a different actor allocation.
    std::erase_if(g_actorRecords, [id](const ActorRecord& record) {
        return record.id == id;
    });

    const Attributes attributes = generate_for(actor);
    g_actorRecords.push_back(ActorRecord{
        .id = id,
        .address = actor,
        .attributes = attributes,
    });

    char message[320];
    std::snprintf(
        message,
        sizeof(message),
        "actor %u attributes: size %.3fx, movement/animation %.3fx, "
        "health %.3fx, damage %.3fx, notice %.3fx%s, Link knockback %.3fx, "
        "stun duration %.3fx",
        static_cast<unsigned>(id),
        attributes.size,
        attributes.movementSpeed,
        attributes.health,
        attributes.attackDamage,
        g_settings.noticeRangeUsesSize ? attributes.size : attributes.noticeRange,
        g_settings.noticeRangeUsesSize ? " (size)" : "",
        attributes.playerKnockback,
        attributes.stunDuration
    );
    svc_log->info(mod_ctx, message);

    return attributes;
}

void forget(fopAc_ac_c* actor) {
    if (actor == nullptr) {
        return;
    }

    const fpc_ProcID id = fopAcM_GetID(actor);
    std::erase_if(g_actorRecords, [actor, id](const ActorRecord& record) {
        return record.address == actor || record.id == id;
    });
}

void clear() {
    g_actorRecords.clear();
}

}  // namespace enemy_randomizer
