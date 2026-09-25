#include "randomizer.hpp"

#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_name.h"
#include "mods/svc/log.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

namespace enemy_randomizer {
namespace {

struct ActorRecord {
    fpc_ProcID id;
    fopAc_ac_c* address;
    std::uint64_t placementKey;
    std::uint32_t instance;
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

// Hash explicit bytes instead of using std::hash or a process ID. Neither the
// actor allocation address nor its process ID survives a room reload.
constexpr std::uint64_t kHashOffset = 14695981039346656037ull;
constexpr std::uint64_t kHashPrime = 1099511628211ull;

std::uint64_t hash_byte(std::uint64_t hash, std::uint8_t value) {
    return (hash ^ value) * kHashPrime;
}

std::uint64_t hash_word(std::uint64_t hash, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        hash = hash_byte(hash, static_cast<std::uint8_t>(value));
        value >>= 8;
    }
    return hash;
}

std::uint64_t hash_position(std::uint64_t hash, const cXyz& position) {
    // The stage loader supplies the same float coordinates on each visit.
    // memcpy avoids aliasing violations and host-dependent std::hash<float>.
    std::uint32_t bits = 0;
    std::memcpy(&bits, &position.x, sizeof(bits));
    hash = hash_word(hash, bits);
    std::memcpy(&bits, &position.y, sizeof(bits));
    hash = hash_word(hash, bits);
    std::memcpy(&bits, &position.z, sizeof(bits));
    return hash_word(hash, bits);
}

std::uint64_t placement_key(fopAc_ac_c* actor, int parentDepth = 0) {
    std::uint64_t hash = kHashOffset;
    const char* stage = dComIfGp_getStartStageName();
    if (stage != nullptr) {
        for (int i = 0; i < 16 && stage[i] != '\0'; ++i) {
            hash = hash_byte(hash, static_cast<std::uint8_t>(stage[i]));
        }
    }
    hash = hash_byte(hash, 0);

    hash = hash_word(hash, static_cast<std::uint32_t>(actor->home.roomNo));
    hash = hash_word(hash, static_cast<std::uint16_t>(fopAcM_GetName(actor)));
    hash = hash_word(hash, actor->setID);
    hash = hash_word(hash, fopAcM_GetParam(actor));
    hash = hash_word(hash, static_cast<std::uint32_t>(actor->argument));
    hash = hash_position(hash, actor->home.pos);
    hash = hash_word(hash, static_cast<std::uint16_t>(actor->home.angle.x));
    hash = hash_word(hash, static_cast<std::uint16_t>(actor->home.angle.y));
    hash = hash_word(hash, static_cast<std::uint16_t>(actor->home.angle.z));

    // 0xFFFF means the actor has no stage placement ID. A parent's stable
    // identity separates children from different spawners at the same spot.
    if (actor->setID == 0xFFFF && parentDepth < 4) {
        fopAc_ac_c* parent = fopAcM_SearchByID(actor->parentActorID);
        if (parent != nullptr && parent != actor) {
            const std::uint64_t parentKey = placement_key(parent, parentDepth + 1);
            hash = hash_word(hash, static_cast<std::uint32_t>(parentKey));
            hash = hash_word(hash, static_cast<std::uint32_t>(parentKey >> 32));
        }
    }
    return hash;
}

std::uint32_t unused_instance(std::uint64_t placementKey) {
    // Two live actors can have precisely the same placement metadata (for
    // example, multiple children spawned at one point). Give each a distinct
    // roll while allowing that slot to be reused on a later room visit.
    std::uint32_t instance = 0;
    while (std::any_of(g_actorRecords.begin(), g_actorRecords.end(), [placementKey, instance](const ActorRecord& record) {
        return record.placementKey == placementKey && record.instance == instance;
    })) {
        ++instance;
    }
    return instance;
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

Attributes generate_for(std::uint64_t placementKey, std::uint32_t instance) {
    const std::uint32_t actorKey = mix(g_settings.seed ^ static_cast<std::uint32_t>(placementKey) ^ static_cast<std::uint32_t>(placementKey >> 32) ^ mix(instance));

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

    const std::uint64_t placementKey = placement_key(actor);
    const std::uint32_t instance = unused_instance(placementKey);
    const Attributes attributes = generate_for(placementKey, instance);
    g_actorRecords.push_back(ActorRecord{
        .id = id,
        .address = actor,
        .placementKey = placementKey,
        .instance = instance,
        .attributes = attributes,
    });

    const char* stage = dComIfGp_getStartStageName();
    const int room = actor->home.roomNo >= 0 ? actor->home.roomNo : actor->current.roomNo;
    char message[320];
    std::snprintf(
        message,
        sizeof(message),
        "stage %.16s room %d actor %u placement %016llX slot %u attributes: size %.3fx, movement/animation %.3fx, "
        "health %.3fx, damage %.3fx, notice %.3fx%s, Link knockback %.3fx, "
        "stun duration %.3fx",
        stage != nullptr ? stage : "unknown",
        room,
        static_cast<unsigned>(id),
        static_cast<unsigned long long>(placementKey),
        static_cast<unsigned>(instance),
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
