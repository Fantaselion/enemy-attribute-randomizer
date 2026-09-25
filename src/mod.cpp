#include "enemies/enemy_patches.hpp"
#include "randomizer.hpp"

#include "mods/service.hpp"
#include "mods/svc/actor_attribute.h"
#include "mods/svc/config.h"
#include "mods/svc/hook.h"
#include "mods/svc/log.h"
#include "mods/svc/ui.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>

DEFINE_MOD();

// Import each service once, in this translation unit. Enemy files can use the
// declarations supplied by the service headers without importing them again.
IMPORT_SERVICE(LogService, svc_log);
// The randomizer only uses fields present in minor version 0. Requesting that
// minimum keeps a mod built against a newer SDK loadable on a Dusklight runtime
// that still provides HookService/UIService 1.0.
IMPORT_SERVICE_VERSION(HookService, svc_hook, 0);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE_VERSION(UiService, svc_ui, 0);
IMPORT_SERVICE(ActorAttributeService, svc_actor_attribute);

namespace {

ConfigVarHandle g_enabled = 0;
ConfigVarHandle g_scaleGravityWithMovement = 0;
ConfigVarHandle g_noticeRangeUsesSize = 0;
ConfigVarHandle g_seed = 0;
ConfigVarHandle g_sizeMinimum = 0;
ConfigVarHandle g_sizeMaximum = 0;
ConfigVarHandle g_movementMinimum = 0;
ConfigVarHandle g_movementMaximum = 0;
ConfigVarHandle g_healthMinimum = 0;
ConfigVarHandle g_healthMaximum = 0;
ConfigVarHandle g_damageMinimum = 0;
ConfigVarHandle g_damageMaximum = 0;
ConfigVarHandle g_noticeMinimum = 0;
ConfigVarHandle g_noticeMaximum = 0;
ConfigVarHandle g_playerKnockbackMinimum = 0;
ConfigVarHandle g_playerKnockbackMaximum = 0;
ConfigVarHandle g_stunMinimum = 0;
ConfigVarHandle g_stunMaximum = 0;

// Keep these limits separate even while they share the same initial values.
// This makes each attribute's allowed UI/config range independently editable
// without changing the common control-building code.
constexpr enemy_randomizer::FloatRange kSizeLimits{0.1f, 4.0f};
constexpr enemy_randomizer::FloatRange kMovementLimits{0.1f, 4.0f};
constexpr enemy_randomizer::FloatRange kHealthLimits{0.1f, 100.0f};
constexpr enemy_randomizer::FloatRange kDamageLimits{0.1f, 100.0f};
constexpr enemy_randomizer::FloatRange kNoticeLimits{0.1f, 100.0f};
constexpr enemy_randomizer::FloatRange kPlayerKnockbackLimits{0.1f, 100.0f};
constexpr enemy_randomizer::FloatRange kStunLimits{0.1f, 4.0f};

ModResult register_bool(
    const char* name,
    bool defaultValue,
    ConfigVarHandle& output,
    ModError* error
) {
    ConfigVarDesc description = CONFIG_VAR_DESC_INIT;
    description.name = name;
    description.type = CONFIG_VAR_BOOL;
    description.default_bool = defaultValue;

    const ModResult result = svc_config->register_var(
        mod_ctx,
        &description,
        &output
    );

    if (result != MOD_OK) {
        return mods::set_error(
            error,
            result,
            "failed to register a randomizer boolean setting"
        );
    }

    return MOD_OK;
}

ModResult register_int(
    const char* name,
    std::int64_t defaultValue,
    ConfigVarHandle& output,
    ModError* error
) {
    ConfigVarDesc description = CONFIG_VAR_DESC_INIT;
    description.name = name;
    description.type = CONFIG_VAR_INT;
    description.default_int = defaultValue;

    const ModResult result = svc_config->register_var(
        mod_ctx,
        &description,
        &output
    );

    if (result != MOD_OK) {
        return mods::set_error(
            error,
            result,
            "failed to register a randomizer integer setting"
        );
    }

    return MOD_OK;
}

std::int64_t get_int(ConfigVarHandle handle, std::int64_t fallback) {
    std::int64_t value = fallback;
    if (svc_config->get_int(mod_ctx, handle, &value) != MOD_OK) {
        return fallback;
    }
    return value;
}

bool get_bool(ConfigVarHandle handle, bool fallback) {
    bool value = fallback;
    if (svc_config->get_bool(mod_ctx, handle, &value) != MOD_OK) {
        return fallback;
    }
    return value;
}

bool notice_range_controls_disabled(ModContext*, void*) {
    return get_bool(g_noticeRangeUsesSize, false);
}

bool always_disabled(ModContext*, void*) {
    return true;
}

constexpr std::int64_t multiplier_to_percent(float multiplier) {
    return static_cast<std::int64_t>((multiplier * 100.0f) + 0.5f);
}

enemy_randomizer::FloatRange percent_range(
    ConfigVarHandle minimumHandle,
    ConfigVarHandle maximumHandle,
    enemy_randomizer::FloatRange limits
) {
    std::int64_t minimum = get_int(minimumHandle, 100);
    std::int64_t maximum = get_int(maximumHandle, 100);

    std::int64_t allowedMinimum = multiplier_to_percent(limits.minimum);
    std::int64_t allowedMaximum = multiplier_to_percent(limits.maximum);
    if (allowedMinimum > allowedMaximum) {
        std::swap(allowedMinimum, allowedMaximum);
    }

    minimum = std::clamp(minimum, allowedMinimum, allowedMaximum);
    maximum = std::clamp(maximum, allowedMinimum, allowedMaximum);
    if (minimum > maximum) {
        std::swap(minimum, maximum);
    }

    return enemy_randomizer::FloatRange{
        .minimum = static_cast<float>(minimum) / 100.0f,
        .maximum = static_cast<float>(maximum) / 100.0f,
    };
}

enemy_randomizer::Settings read_settings() {
    return enemy_randomizer::Settings{
        .enabled = get_bool(g_enabled, true),
        .scaleGravityWithMovement = get_bool(
            g_scaleGravityWithMovement,
            true
        ),
        .noticeRangeUsesSize = get_bool(g_noticeRangeUsesSize, false),
        .seed = static_cast<std::uint32_t>(
            get_int(g_seed, 12345)
        ),
        .size = percent_range(g_sizeMinimum, g_sizeMaximum, kSizeLimits),
        .movementSpeed = percent_range(
            g_movementMinimum,
            g_movementMaximum,
            kMovementLimits
        ),
        .health = percent_range(
            g_healthMinimum,
            g_healthMaximum,
            kHealthLimits
        ),
        .attackDamage = percent_range(
            g_damageMinimum,
            g_damageMaximum,
            kDamageLimits
        ),
        .noticeRange = percent_range(
            g_noticeMinimum,
            g_noticeMaximum,
            kNoticeLimits
        ),
        .playerKnockback = percent_range(
            g_playerKnockbackMinimum,
            g_playerKnockbackMaximum,
            kPlayerKnockbackLimits
        ),
        // Keep the resolver at vanilla even if an older config file still
        // contains non-vanilla stun values.
        .stunDuration = enemy_randomizer::FloatRange{1.0f, 1.0f},
    };
}

std::uint32_t mix_seed(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

std::uint32_t make_fresh_seed() {
    static std::uint32_t nonce = 0;
    nonce += 0x9E3779B9u;

    const std::uint64_t ticks = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    const std::uint32_t current = static_cast<std::uint32_t>(
        get_int(g_seed, 12345)
    );
    std::uint32_t seed = mix_seed(
        static_cast<std::uint32_t>(ticks) ^
        static_cast<std::uint32_t>(ticks >> 32) ^ nonce ^ current
    ) & 0x7FFFFFFFu;

    if (seed == current) {
        seed = (seed + 1u) & 0x7FFFFFFFu;
    }
    return seed;
}

void on_randomize_seed_and_reroll(ModContext*, void*) {
    const std::uint32_t seed = make_fresh_seed();
    if (svc_config->set_int(mod_ctx, g_seed, seed) != MOD_OK) {
        svc_log->error(mod_ctx, "failed to save a newly randomized enemy seed");
        return;
    }

    // Do not rewrite actor state such as current/max health. Clearing only the
    // stable roll cache makes every still-loaded enemy obtain fresh attributes
    // the next time one of its integrated attribute sites asks for them.
    enemy_randomizer::configure(read_settings());
    enemy_randomizer::clear();

    char message[96];
    std::snprintf(
        message,
        sizeof(message),
        "enemy seed randomized to %u; loaded enemy rolls cleared",
        static_cast<unsigned>(seed)
    );
    svc_log->info(mod_ctx, message);
}

ModResult add_button(
    UiElementHandle panel,
    const char* label,
    const char* help,
    UiPressedFn onPressed
) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_BUTTON;
    control.label = label;
    control.help_rml = help;
    control.on_pressed = onPressed;

    return svc_ui->pane_add_control(mod_ctx, panel, &control, nullptr);
}

ModResult add_toggle(
    UiElementHandle panel,
    const char* label,
    const char* help,
    ConfigVarHandle handle
) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_TOGGLE;
    control.label = label;
    control.help_rml = help;
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = handle;

    return svc_ui->pane_add_control(mod_ctx, panel, &control, nullptr);
}

ModResult add_number(
    UiElementHandle panel,
    const char* label,
    const char* help,
    ConfigVarHandle handle,
    std::int64_t minimum,
    std::int64_t maximum,
    const char* suffix = nullptr,
    UiPredicateFn isDisabled = nullptr
) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_NUMBER;
    control.label = label;
    control.help_rml = help;
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = handle;
    control.min = minimum;
    control.max = maximum;
    control.step = 1;
    control.suffix = suffix;
    control.is_disabled = isDisabled;

    return svc_ui->pane_add_control(mod_ctx, panel, &control, nullptr);
}

ModResult add_range_controls(
    UiElementHandle panel,
    const char* minimumLabel,
    const char* maximumLabel,
    const char* help,
    ConfigVarHandle minimumHandle,
    ConfigVarHandle maximumHandle,
    enemy_randomizer::FloatRange limits,
    UiPredicateFn isDisabled = nullptr
) {
    std::int64_t allowedMinimum = multiplier_to_percent(limits.minimum);
    std::int64_t allowedMaximum = multiplier_to_percent(limits.maximum);
    if (allowedMinimum > allowedMaximum) {
        std::swap(allowedMinimum, allowedMaximum);
    }

    ModResult result = add_number(
        panel,
        minimumLabel,
        help,
        minimumHandle,
        allowedMinimum,
        allowedMaximum,
        "%",
        isDisabled
    );

    if (result != MOD_OK) {
        return result;
    }

    return add_number(
        panel,
        maximumLabel,
        help,
        maximumHandle,
        allowedMinimum,
        allowedMaximum,
        "%",
        isDisabled
    );
}

ModResult build_mods_panel(
    ModContext*,
    UiElementHandle panel,
    void*,
    ModError*
) {
    ModResult result = svc_ui->pane_add_section(
        mod_ctx,
        panel,
        "General"
    );
    if (result != MOD_OK) {
        return result;
    }

    result = add_toggle(
        panel,
        "Enable randomizer",
        "Disabling returns 100% for subsequent attribute requests. "
        "It cannot undo values already written to active actors, such as health.",
        g_enabled
    );
    if (result != MOD_OK) {
        return result;
    }

    result = add_number(
        panel,
        "Seed",
        "The same seed gives each room placement the same attributes across visits and restarts. "
        "Dynamic spawns also use their spawn details; identical spawns use live slots.",
        g_seed,
        0,
        2147483647
    );
    if (result != MOD_OK) {
        return result;
    }

    result = add_button(
        panel,
        "Randomize seed and reroll loaded enemies",
        "Chooses a new seed and clears every stable enemy roll currently "
        "cached by this mod. Loaded enemies receive new attributes on their "
        "next request. Actor state already written by creation logic, such as "
        "current health, is not rewritten.",
        on_randomize_seed_and_reroll
    );
    if (result != MOD_OK) {
        return result;
    }

    result = svc_ui->pane_add_section(mod_ctx, panel, "Ranges");
    if (result != MOD_OK) {
        return result;
    }

    const char* rangeHelp =
        "Values are generated once per actor. Range edits affect actors that "
        "receive their values afterward. An enemy-specific patch must still "
        "apply each multiplier to the correct vanilla calculation.";

    result = add_range_controls(
        panel,
        "Minimum size",
        "Maximum size",
        rangeHelp,
        g_sizeMinimum,
        g_sizeMaximum,
        kSizeLimits
    );
    if (result != MOD_OK) {
        return result;
    }

    result = add_toggle(
        panel,
        "Use size for notice range",
        "Enable to make enemy awareness distances use the size multiplier. "
        "Normally awareness uses the independent notice-range values below. "
        "This does not change attack reach, grabs, "
        "scripted triggers, or ordinary movement distances.",
        g_noticeRangeUsesSize
    );
    if (result != MOD_OK) {
        return result;
    }

    result = add_range_controls(
        panel,
        "Minimum notice range",
        "Maximum notice range",
        "Controls how far away an enemy can notice, acquire, or forget Link. "
        "These values are ignored while 'Use size for notice range' is enabled.",
        g_noticeMinimum,
        g_noticeMaximum,
        kNoticeLimits,
        notice_range_controls_disabled
    );
    if (result != MOD_OK) {
        return result;
    }

    result = add_range_controls(
        panel,
        "Minimum movement / animation speed",
        "Maximum movement / animation speed",
        "Ordinary enemy movement, animation, and action timing share one "
        "per-actor multiplier. Genuine stun windows use the separate "
        "stun-duration range. Actor-specific exceptions remain in the host.",
        g_movementMinimum,
        g_movementMaximum,
        kMovementLimits
    );
    if (result != MOD_OK) {
        return result;
    }

    result = add_range_controls(
        panel,
        "Minimum health",
        "Maximum health",
        rangeHelp,
        g_healthMinimum,
        g_healthMaximum,
        kHealthLimits
    );
    if (result != MOD_OK) {
        return result;
    }

    result = add_range_controls(
        panel,
        "Minimum attack damage",
        "Maximum attack damage",
        rangeHelp,
        g_damageMinimum,
        g_damageMaximum,
        kDamageLimits
    );
    if (result != MOD_OK) {
        return result;
    }

    result = add_range_controls(
        panel,
        "Minimum stun duration",
        "Maximum stun duration",
        "Temporarily fixed at 100% while remaining enemy timer interactions "
        "are audited. The controls and underlying setting are retained for "
        "later re-enablement.",
        g_stunMinimum,
        g_stunMaximum,
        kStunLimits,
        always_disabled
    );
    if (result != MOD_OK) {
        return result;
    }

    return add_range_controls(
        panel,
        "Minimum Link knockback distance",
        "Maximum Link knockback distance",
        "Controls the horizontal distance Link is pushed by ordinary enemy "
        "hits. This preserves the vanilla normal/large/huge reaction type and "
        "does not alter scripted grabs, throws, horse reactions, or vertical "
        "launch height.",
        g_playerKnockbackMinimum,
        g_playerKnockbackMaximum,
        kPlayerKnockbackLimits
    );
}

ModResult register_settings(ModError* error) {
    ModResult result = register_bool("randomizerEnabled", true, g_enabled, error);
    if (result != MOD_OK) return result;

    result = register_bool(
        "noticeRangeUsesSize",
        false,
        g_noticeRangeUsesSize,
        error
    );
    if (result != MOD_OK) return result;

    result = register_bool(
        "scaleGravityWithMovement",
        true,
        g_scaleGravityWithMovement,
        error
    );
    if (result != MOD_OK) return result;

    result = register_int("seed", 12345, g_seed, error);
    if (result != MOD_OK) return result;

    result = register_int("sizeMinimumPercent", 50, g_sizeMinimum, error);
    if (result != MOD_OK) return result;
    result = register_int("sizeMaximumPercent", 200, g_sizeMaximum, error);
    if (result != MOD_OK) return result;

    result = register_int("movementMinimumPercent", 50, g_movementMinimum, error);
    if (result != MOD_OK) return result;
    result = register_int("movementMaximumPercent", 200, g_movementMaximum, error);
    if (result != MOD_OK) return result;

    result = register_int("healthMinimumPercent", 50, g_healthMinimum, error);
    if (result != MOD_OK) return result;
    result = register_int("healthMaximumPercent", 200, g_healthMaximum, error);
    if (result != MOD_OK) return result;

    result = register_int("damageMinimumPercent", 50, g_damageMinimum, error);
    if (result != MOD_OK) return result;
    result = register_int("damageMaximumPercent", 200, g_damageMaximum, error);
    if (result != MOD_OK) return result;

    result = register_int("noticeMinimumPercent", 50, g_noticeMinimum, error);
    if (result != MOD_OK) return result;
    result = register_int("noticeMaximumPercent", 200, g_noticeMaximum, error);
    if (result != MOD_OK) return result;

    result = register_int("stunMinimumPercent", 100, g_stunMinimum, error);
    if (result != MOD_OK) return result;
    result = register_int("stunMaximumPercent", 100, g_stunMaximum, error);
    if (result != MOD_OK) return result;

    result = register_int(
        "playerKnockbackMinimumPercent",
        50,
        g_playerKnockbackMinimum,
        error
    );
    if (result != MOD_OK) return result;
    result = register_int(
        "playerKnockbackMaximumPercent",
        200,
        g_playerKnockbackMaximum,
        error
    );
    if (result != MOD_OK) return result;

    // Override persisted pre-freeze values too, so the disabled controls
    // visibly agree with the effective vanilla-only stun setting.
    if (svc_config->set_int(mod_ctx, g_stunMinimum, 100) != MOD_OK ||
        svc_config->set_int(mod_ctx, g_stunMaximum, 100) != MOD_OK)
    {
        return mods::set_error(
            error,
            MOD_ERROR,
            "failed to reset the temporarily disabled stun-duration setting"
        );
    }

    return MOD_OK;
}

}  // namespace

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    enemy_randomizer::clear();

    ModResult result = register_settings(error);
    if (result != MOD_OK) {
        return result;
    }

    enemy_randomizer::configure(read_settings());

    UiModsPanelDesc panel = UI_MODS_PANEL_DESC_INIT;
    panel.build = build_mods_panel;

    result = svc_ui->register_mods_panel(mod_ctx, &panel);
    if (result != MOD_OK) {
        return mods::set_error(
            error,
            result,
            "failed to register the randomizer settings panel"
        );
    }

    result = enemy_patches::initialize_all(error);
    if (result != MOD_OK) {
        return result;
    }

    svc_log->info(
        mod_ctx,
        "Enemy Attribute Randomizer starter initialized"
    );
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    // Config reads are cheap, and this lets range changes affect the next actor
    // without requiring a reload. Already-recorded actors retain their values.
    enemy_randomizer::configure(read_settings());
    enemy_patches::update_all();
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    enemy_patches::shutdown_all();
    enemy_randomizer::clear();
    return MOD_OK;
}

}  // extern "C"
