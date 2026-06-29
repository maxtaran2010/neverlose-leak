#include "no_spread.hpp"
#include "valve/schema/schema.hpp"

// ts_accuracy_set_m @ NL rdata 0x1a9371a6758
// Returns pointer to m_fAccuracyPenalty inside the weapon, or nullptr if schema not found.
static float* accuracy_penalty_ptr( c_base_player_weapon* weapon ) {
    static const short offset = schema_get_offset( "C_CSWeaponBase", "m_fAccuracyPenalty" );
    if ( offset <= 0 ) return nullptr;
    return reinterpret_cast<float*>( reinterpret_cast<uintptr_t>( weapon ) + offset );
}

void c_no_spread::on_create_move( c_user_cmd* user_cmd ) {
    if ( !g_cfg->rage_bot.m_enabled ) return;
    if ( !g_interfaces->m_engine->is_in_game( ) ) return;
    if ( !g_ctx->m_local_pawn || !g_ctx->m_local_pawn->is_alive( ) ) return;

    auto weapon = g_ctx->m_local_pawn->get_active_weapon( );
    if ( !weapon ) return;

    // ts_accuracy_set_m: zero accuracy penalty so the inaccuracy ring collapses
    if ( nl_cfg.no_spread.m_zero_penalty ) {
        float* pen = accuracy_penalty_ptr( weapon );
        if ( pen ) *pen = 0.f;
    }

    // ts_accuracy_set_s: zero random_seed so spread vector = forward (seed 0 → cos(0)/sin(0))
    // Combined with nl_spread_correction() in ragebot, this makes spread a no-op
    if ( nl_cfg.no_spread.m_zero_seed )
        user_cmd->pb.mutable_base( )->set_random_seed( 0 );
}
