#include "enemy_patches.hpp"

#include "randomizer.hpp"

#include "c/c_damagereaction.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_name.h"
#include "m_Do/m_Do_ext.h"
#include "d/actor/d_a_e_rd.h"
#include "d/actor/d_a_e_wb.h"
#include "d/actor/d_a_e_yc.h"
#include "mods/service.hpp"
#include "mods/svc/actor_attribute.h"
#include "mods/svc/hook.hpp"

#include <algorithm>

// Include headers here only for enemies that need genuinely actor-specific
// mod hooks or state. Ordinary enemies only need their profile added to
// is_supported_enemy() below.
//
// #include "enemies/unusual_enemy_patch.hpp"

namespace {

/*
 * fopAc_Delete is the common final deletion wrapper for all actors. A post
 * hook runs after the actor-specific Delete function, but before fpcBs_Delete
 * frees the actor allocation.
 */
DEFINE_HOOK_SYMBOL(
    "fopAc_Delete",
    int(fopAc_ac_c*),
    ActorDelete
);

ActorAttributeResolverHandle g_resolverHandle = 0;
bool g_actorDeleteHookInstalled = false;

bool is_supported_enemy(const s16 profile) {
    switch (profile) {
    /*
     * Add an enemy here only after its actor source has been audited and its
     * semantic attribute-consumption sites have been integrated.
     */
    case fpcNm_L7ODR_e:
    case fpcNm_B_BH_e:
    case fpcNm_B_BQ_e:
    case fpcNm_B_DR_e:
    case fpcNm_B_DRE_e:
    case fpcNm_B_DS_e:
    case fpcNm_B_GG_e:
    case fpcNm_B_GM_e:
    case fpcNm_B_GND_e:
    case fpcNm_B_MGN_e:
    case fpcNm_B_OB_e:
    case fpcNm_B_OH_e:
    case fpcNm_B_OH2_e:
    case fpcNm_B_TN_e:
    case fpcNm_B_YO_e:
    case fpcNm_B_YOI_e:
    case fpcNm_B_ZANT_e:
    case fpcNm_B_ZANTM_e:
    case fpcNm_B_ZANTZ_e:
    case fpcNm_B_ZANTS_e:
    case fpcNm_E_AI_e:
    case fpcNm_E_ARROW_e:
    case fpcNm_E_BA_e:
    case fpcNm_E_BEE_e:
    case fpcNm_E_BG_e:
    case fpcNm_E_BI_e:
    case fpcNm_E_BI_LEAF_e:
    case fpcNm_E_BS_e:
    case fpcNm_E_BU_e:
    case fpcNm_E_BUG_e:
    case fpcNm_E_CR_e:
    case fpcNm_E_CR_EGG_e:
    case fpcNm_E_DB_e:
    case fpcNm_E_DB_LEAF_e:
    case fpcNm_E_DD_e:
    case fpcNm_E_DK_e:
    case fpcNm_E_DN_e:
    case fpcNm_E_DT_e:
    case fpcNm_E_FB_e:
    case fpcNm_E_FK_e:
    case fpcNm_E_FM_e:
    case fpcNm_E_FS_e:
    case fpcNm_E_FZ_e:
    case fpcNm_E_GB_e:
    case fpcNm_E_GE_e:
    case fpcNm_E_GI_e:
    case fpcNm_E_GM_e:
    case fpcNm_E_GOB_e:
    case fpcNm_E_HB_e:
    case fpcNm_E_HB_LEAF_e:
    case fpcNm_E_HM_e:
    case fpcNm_E_HP_e:
    case fpcNm_E_HZ_e:
    case fpcNm_E_HZELDA_e:
    case fpcNm_E_IS_e:
    case fpcNm_E_KG_e:
    case fpcNm_E_KK_e:
    case fpcNm_E_KR_e:
    case fpcNm_E_MB_e:
    case fpcNm_E_MF_e:
    case fpcNm_E_MK_e:
    case fpcNm_E_MK_BO_e:
    case fpcNm_E_MM_e:
    case fpcNm_E_MM_MT_e:
    case fpcNm_E_MS_e:
    case fpcNm_E_NEST_e:
    case fpcNm_E_NZ_e:
    case fpcNm_E_OC_e:
    case fpcNm_E_OctBg_e:
    case fpcNm_E_OT_e:
    case fpcNm_E_PM_e:
    case fpcNm_E_PO_e:
    case fpcNm_E_PZ_e:
    case fpcNm_E_RB_e:
    case fpcNm_E_RD_e:
    case fpcNm_E_RDB_e:
    case fpcNm_E_RDY_e:
    case fpcNm_E_S1_e:
    case fpcNm_E_SB_e:
    case fpcNm_E_SF_e:
    case fpcNm_E_SG_e:
    case fpcNm_E_SH_e:
    //case fpcNm_E_SM_e:
    case fpcNm_E_SM2_e:
    case fpcNm_E_ST_e:
    case fpcNm_E_SW_e:
    //case fpcNm_E_TH_e:
    case fpcNm_E_TH_BALL_e:
    case fpcNm_E_TK_e:
    case fpcNm_E_TK2_e:
    case fpcNm_E_TK_BALL_e:
    case fpcNm_E_TT_e:
    case fpcNm_E_VT_e:
    case fpcNm_E_WB_e:
    case fpcNm_E_WS_e:
    case fpcNm_E_WW_e:
    case fpcNm_E_YC_e:
    case fpcNm_E_YD_e:
    case fpcNm_E_YD_LEAF_e:
    case fpcNm_E_YG_e:
    case fpcNm_E_YH_e:
    case fpcNm_E_YK_e:
    case fpcNm_E_YM_e:
    case fpcNm_E_YMB_e:
    case fpcNm_E_YR_e:
    case fpcNm_E_ZM_e:
    case fpcNm_E_ZH_e:
    case fpcNm_E_ZS_e:
        return true;

    default:
        return false;
    }
}

/*
 * Per-enemy attribute limits live here, after the stable raw roll is read and
 * before any actor-local or host-side semantic consumer receives it. Add a
 * profile case and clamp only the fields that enemy needs:
 *
 *   attributes.size
 *   attributes.movementSpeed
 *   attributes.health
 *   attributes.attackDamage
 *   attributes.noticeRange
 *   attributes.playerKnockback
 *   attributes.stunDuration
 */
void apply_enemy_attribute_clamps(
    const s16 profile,
    enemy_randomizer::Attributes& attributes
) {
    switch (profile) {
    case fpcNm_B_BQ_e:
    case fpcNm_B_BH_e:
        attributes.size = std::clamp(attributes.size, 0.25f, 1.80f);
        break;

	case fpcNm_E_FM_e:
		attributes.size = std::clamp(attributes.size, 0.25f, 1.80f); // min is placeholder for now
		break;

	case fpcNm_B_DS_e:
		attributes.stunDuration = std::clamp(attributes.size, 1.00f, 4.00f); // max is placeholder for now
		break;

    case fpcNm_E_DT_e:
		attributes.size = std::min(attributes.size, 2.5f);
		break;

    //case fpcNm_E_TH_e:
		//attributes.size = std::min(attributes.size, 2.0f);
		//break;

    case fpcNm_E_PO_e:
		attributes.size = std::min(attributes.size, 3.0f);
		break;

    case fpcNm_E_VT_e:
		attributes.size = std::min(attributes.size, 3.0f);
		break;

    case fpcNm_B_MGN_e:
		attributes.size = std::min(attributes.size, 3.0f);
		break;

	default:
        break;
    }
}

fopAc_ac_c* owner_with_profile_or_self(
    fopAc_ac_c* actor,
    fopAc_ac_c* candidate,
    const s16 expectedProfile
) {
    if (candidate != nullptr && fopAcM_GetName(candidate) == expectedProfile) {
        return candidate;
    }

    return actor;
}

fopAc_ac_c* parent_owner_or_self(
    fopAc_ac_c* actor,
    const s16 expectedProfile
) {
    return owner_with_profile_or_self(
        actor,
        fopAcM_SearchByID(actor->parentActorID),
        expectedProfile
    );
}

fopAc_ac_c* linked_owner_or_self(
    fopAc_ac_c* actor,
    const s16 expectedProfile
) {
    return owner_with_profile_or_self(
        actor,
        fopAcM_SearchByID(fopAcM_GetLinkId(actor)),
        expectedProfile
    );
}

void* matching_king_rider_search(void* candidate, void* data) {
    if (!fopAcM_IsActor(candidate) ||
        fopAcM_GetName(candidate) != fpcNm_E_RD_e)
    {
        return nullptr;
    }

    auto* rider = static_cast<e_rd_class*>(candidate);
    auto* boar = static_cast<fopAc_ac_c*>(data);
    return rider->actor_set != 0 &&
                   rider->boar_id == fopAcM_GetID(boar)
               ? candidate
               : nullptr;
}

fopAc_ac_c* king_rider_owner_or_self(fopAc_ac_c* actor) {
    auto* boar = static_cast<e_wb_class*>(static_cast<void*>(actor));
    if (boar->leader == 0) {
        return actor;
    }

    auto* transientRider = fopAcM_SearchByID(boar->rd_id);
    if (transientRider != nullptr &&
        fopAcM_GetName(transientRider) == fpcNm_E_RD_e)
    {
        auto* rider = static_cast<e_rd_class*>(
            static_cast<void*>(transientRider));
        if (rider->actor_set != 0 &&
            rider->boar_id == fopAcM_GetID(actor))
        {
            return transientRider;
        }
    }

    auto* rider = static_cast<fopAc_ac_c*>(
        fpcM_Search(matching_king_rider_search, actor));
    return rider != nullptr ? rider : actor;
}

/*
 * Link's target collider exposes only the raw actor that owns the attacking
 * collider. Mirror the audited child/projectile ownership rules here so a
 * projectile uses its enemy owner's stable knockback value instead of rolling
 * a second value when it hits Link.
 */
fopAc_ac_c* player_knockback_owner(fopAc_ac_c* actor) {
    if (actor == nullptr) {
        return nullptr;
    }

    switch (fopAcM_GetName(actor)) {
    case fpcNm_L7ODR_e:
        return parent_owner_or_self(actor, fpcNm_B_DR_e);

    case fpcNm_B_BH_e:
    case fpcNm_E_MB_e:
        return parent_owner_or_self(actor, fpcNm_B_BQ_e);

    case fpcNm_B_DR_e:
    case fpcNm_B_DRE_e:
        return parent_owner_or_self(actor, fpcNm_B_DR_e);

    case fpcNm_B_DS_e:
        return parent_owner_or_self(actor, fpcNm_B_DS_e);

    case fpcNm_B_OH_e:
    case fpcNm_B_OH2_e:
        return parent_owner_or_self(actor, fpcNm_B_OB_e);

    case fpcNm_B_YOI_e:
        return linked_owner_or_self(actor, fpcNm_B_YO_e);

    case fpcNm_B_ZANTM_e:
        return linked_owner_or_self(actor, fpcNm_B_ZANT_e);

    case fpcNm_B_ZANTZ_e:
        return parent_owner_or_self(actor, fpcNm_B_ZANT_e);

    case fpcNm_E_ARROW_e: {
        fopAc_ac_c* parent = fopAcM_SearchByID(fopAcM_GetLinkId(actor));
        if (parent != nullptr &&
            (fopAcM_GetName(parent) == fpcNm_E_RD_e ||
             fopAcM_GetName(parent) == fpcNm_E_RDY_e))
        {
            return parent;
        }
        return actor;
    }

    case fpcNm_E_BEE_e:
        return parent_owner_or_self(actor, fpcNm_E_NEST_e);

    case fpcNm_E_BG_e:
        return linked_owner_or_self(actor, fpcNm_E_BG_e);

    case fpcNm_E_CR_EGG_e:
        return linked_owner_or_self(actor, fpcNm_E_CR_e);

    case fpcNm_E_FB_e:
        return linked_owner_or_self(actor, fpcNm_E_FB_e);

    case fpcNm_E_FK_e:
        return parent_owner_or_self(actor, fpcNm_B_GND_e);

    case fpcNm_E_HM_e:
        if ((fopAcM_GetParam(actor) & 0xFF) == 5) {
            return parent_owner_or_self(actor, fpcNm_E_HM_e);
        }
        return actor;

    case fpcNm_E_KK_e:
        if ((fopAcM_GetParam(actor) & 0xFF) == 1) {
            return parent_owner_or_self(actor, fpcNm_E_KK_e);
        }
        return actor;

    case fpcNm_E_MK_BO_e:
        return linked_owner_or_self(actor, fpcNm_E_MK_e);

    case fpcNm_E_MM_MT_e:
        return linked_owner_or_self(actor, fpcNm_E_MM_e);

    case fpcNm_E_PO_e:
        return linked_owner_or_self(actor, fpcNm_E_PO_e);

    case fpcNm_E_PZ_e: {
        fopAc_ac_c* root = actor;
        fopAc_ac_c* parent = fopAcM_SearchByID(root->parentActorID);
        for (int depth = 0;
             depth < 32 && parent != nullptr && parent != root &&
             parent != actor && fopAcM_GetName(parent) == fpcNm_E_PZ_e;
             ++depth)
        {
            root = parent;
            parent = fopAcM_SearchByID(root->parentActorID);
        }
        return root;
    }

    case fpcNm_E_SW_e:
        return linked_owner_or_self(actor, fpcNm_E_SW_e);

    //case fpcNm_E_TH_BALL_e:
        //return linked_owner_or_self(actor, fpcNm_E_TH_e);

    case fpcNm_E_TK_BALL_e: {
        fopAc_ac_c* parent = fopAcM_SearchByID(fopAcM_GetLinkId(actor));
        if (parent != nullptr &&
            (fopAcM_GetName(parent) == fpcNm_E_TK_e ||
             fopAcM_GetName(parent) == fpcNm_E_TK2_e))
        {
            return parent;
        }
        return actor;
    }

    case fpcNm_E_WW_e:
        return linked_owner_or_self(actor, fpcNm_E_WW_e);

    case fpcNm_E_WB_e:
        return king_rider_owner_or_self(actor);

    case fpcNm_E_YC_e: {
        auto* kargarok = static_cast<e_yc_class*>(actor);
        return owner_with_profile_or_self(
            actor,
            fopAcM_SearchByID(kargarok->mRiderID),
            fpcNm_E_RDY_e
        );
    }

    case fpcNm_E_ZM_e:
        if ((fopAcM_GetParam(actor) & 0xFF) == 20) {
            return linked_owner_or_self(actor, fpcNm_E_ZM_e);
        }
        return actor;

    default:
        return actor;
    }
}

bool resolve_enemy_attribute(
    ModContext*,
    const ActorAttributeInfo* info,
    float* outValue,
    void*
) {
    if (info == nullptr || outValue == nullptr || info->actor == nullptr) {
        return false;
    }

    auto* actor = const_cast<fopAc_ac_c*>(
        static_cast<const fopAc_ac_c*>(info->actor)
    );
    if (info->attribute == ACTOR_ATTRIBUTE_PLAYER_KNOCKBACK) {
        actor = player_knockback_owner(actor);
    }

    if (!is_supported_enemy(fopAcM_GetName(actor))) {
        return false;
    }

    enemy_randomizer::Attributes attributes =
        enemy_randomizer::attributes_for(actor);
    apply_enemy_attribute_clamps(fopAcM_GetName(actor), attributes);

    float multiplier = 1.0f;
    switch (info->attribute) {
    case ACTOR_ATTRIBUTE_MOVEMENT_SPEED:
        // Movement and ordinary animation/action speed are one attribute.
        multiplier = attributes.movementSpeed;
        break;

    case ACTOR_ATTRIBUTE_SIZE:
        multiplier = attributes.size;
        break;

    case ACTOR_ATTRIBUTE_HEALTH:
        multiplier = attributes.health;
        break;

    case ACTOR_ATTRIBUTE_ATTACK_DAMAGE:
        multiplier = attributes.attackDamage;
        break;

    case ACTOR_ATTRIBUTE_GRAVITY:
        if (!enemy_randomizer::scale_gravity_with_movement()) {
            return false;
        }

        /*
         * Actor code scales the paired launch/bounce velocity by S. Scaling
         * acceleration by S^2 preserves vanilla arc height while changing
         * the trajectory duration by approximately 1/S.
         */
        multiplier = attributes.movementSpeed * attributes.movementSpeed;
        break;

    case ACTOR_ATTRIBUTE_NOTICE_RANGE:
        multiplier = enemy_randomizer::notice_range_uses_size()
                         ? attributes.size
                         : attributes.noticeRange;
        break;

    case ACTOR_ATTRIBUTE_PLAYER_KNOCKBACK:
        multiplier = attributes.playerKnockback;
        break;

    case ACTOR_ATTRIBUTE_STUN_DURATION:
        multiplier = attributes.stunDuration;
        break;

    default:
        return false;
    }

    // Compose after previously loaded resolvers rather than restarting from
    // the original vanilla value.
    *outValue = info->current_value * multiplier;
    return true;
}

void on_actor_delete_post(ModContext*, void* args, void* retval, void*) {
    if (args == nullptr || retval == nullptr ||
        *static_cast<int*>(retval) != 1)
    {
        return;
    }

    auto* actor = mods::arg<fopAc_ac_c*>(args, 0);
    if (actor != nullptr) {
        enemy_randomizer::forget(actor);
    }
}

void shutdown_common_patch() {
    if (g_actorDeleteHookInstalled) {
        mods::hook::uninstall<ActorDelete>();
        g_actorDeleteHookInstalled = false;
    }

    if (g_resolverHandle != 0) {
        svc_actor_attribute->unregister_resolver(mod_ctx, g_resolverHandle);
        g_resolverHandle = 0;
    }
}

}  // namespace

namespace enemy_patches {

ModResult initialize_all(ModError* error) {
    g_resolverHandle = 0;
    g_actorDeleteHookInstalled = false;

    ModResult result = svc_actor_attribute->register_resolver(
        mod_ctx,
        resolve_enemy_attribute,
        nullptr,
        &g_resolverHandle
    );
    if (result != MOD_OK) {
        return mods::set_error(
            error,
            result,
            "failed to register the central enemy attribute resolver"
        );
    }

    result = mods::hook::add_post<ActorDelete>(on_actor_delete_post);
    if (result != MOD_OK) {
        shutdown_common_patch();
        return mods::set_error(
            error,
            result,
            "failed to hook the common actor Delete function"
        );
    }
    g_actorDeleteHookInstalled = true;

    /*
     * Initialize genuinely enemy-specific exception patches here.
     * If one fails, shut down any earlier exceptions in reverse order, call
     * shutdown_common_patch(), and return its error.
     *
     * result = unusual_enemy_patch::initialize(error);
     * if (result != MOD_OK) {
     *     shutdown_common_patch();
     *     return result;
     * }
     */

    return MOD_OK;
}

void update_all() {
    // unusual_enemy_patch::update();
}

void shutdown_all() {
    // Shut down exception patches in reverse initialization order first.
    // unusual_enemy_patch::shutdown();

    shutdown_common_patch();
}

}  // namespace enemy_patches
