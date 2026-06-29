#include "anti_hit.hpp"
#include "nl_config.hpp"

// NL pitch modes (stored as config byte in feature object +28):
// 0 = down  (-89°)
// 1 = up    (+89°)
// 2 = zero  ( 0°)
void c_anti_hit::apply_pitch( c_user_cmd* user_cmd ) {
    if ( !g_cfg->anti_hit.m_override_pitch ) return;

    float pitch;
    switch ( nl_cfg.anti_hit.m_pitch_mode ) {
    case 1:  pitch =  89.f; break;
    case 2:  pitch =   0.f; break;
    default: pitch = -89.f; break;
    }

    for ( int i = 0; i < user_cmd->pb.input_history_size( ); i++ ) {
        auto ih = user_cmd->pb.mutable_input_history( i );
        if ( ih ) ih->mutable_view_angles( )->set_x( pitch );
    }
    user_cmd->pb.mutable_base( )->mutable_viewangles( )->set_x( pitch );
}

// NL yaw modes:
// 0 = jitter   (delta-encoded LUT, sub_1A936DFB0B0)
// 1 = spin     (accumulate degrees/second)
// 2 = static   (fixed offset from real view)
// 3 = LBY-flip (invert desync direction every update)
void c_anti_hit::apply_yaw( c_user_cmd* user_cmd ) {
    if ( !g_cfg->anti_hit.m_override_yaw ) return;

    float base_yaw = static_cast<float>(
        user_cmd->pb.mutable_base( )->viewangles( ).y( ) );
    float real_yaw = base_yaw;
    float fake_yaw = base_yaw;

    switch ( nl_cfg.anti_hit.m_yaw_mode ) {
    case 0: // jitter: advance LUT, apply offset to fake yaw
        jitter_advance( );
        fake_yaw = real_yaw + JITTER_LUT[m_jitter_idx].yaw;
        break;

    case 1: // spin: accumulate speed * INTERVAL_PER_TICK
        m_spin_accum += nl_cfg.anti_hit.m_spin_speed * INTERVAL_PER_TICK;
        if ( m_spin_accum > 360.f ) m_spin_accum -= 360.f;
        fake_yaw = real_yaw + m_spin_accum;
        break;

    case 2: // static: fixed offset
        fake_yaw = real_yaw + nl_cfg.anti_hit.m_static_yaw;
        break;

    case 3: { // LBY-flip: alternate desync amount sign every tick
        static bool flip = false;
        flip = !flip;
        fake_yaw = real_yaw + ( flip ? nl_cfg.anti_hit.m_desync_amount
                                     : -nl_cfg.anti_hit.m_desync_amount );
        break;
    }
    }

    apply_desync( user_cmd, real_yaw, fake_yaw );
}

// Desync protocol (NL analysis):
//   input_history[0].view_angles.y = real_yaw  → server's lag-comp position
//   input_history[1+].view_angles.y = fake_yaw → what other clients render
//   pb.base.viewangles.y = real_yaw             → used for movement
void c_anti_hit::apply_desync( c_user_cmd* user_cmd, float real_yaw, float fake_yaw ) {
    if ( !nl_cfg.anti_hit.m_desync ) {
        // No desync: set all entries to fake_yaw only
        for ( int i = 0; i < user_cmd->pb.input_history_size( ); i++ ) {
            auto ih = user_cmd->pb.mutable_input_history( i );
            if ( ih ) ih->mutable_view_angles( )->set_y( fake_yaw );
        }
        user_cmd->pb.mutable_base( )->mutable_viewangles( )->set_y( fake_yaw );
        return;
    }

    // Desync ON: first entry = real, rest = fake
    if ( user_cmd->pb.input_history_size( ) > 0 ) {
        auto ih0 = user_cmd->pb.mutable_input_history( 0 );
        if ( ih0 ) ih0->mutable_view_angles( )->set_y( real_yaw );
    }
    for ( int i = 1; i < user_cmd->pb.input_history_size( ); i++ ) {
        auto ih = user_cmd->pb.mutable_input_history( i );
        if ( ih ) ih->mutable_view_angles( )->set_y( fake_yaw );
    }
    // Keep base viewangle = real so movement works correctly
    user_cmd->pb.mutable_base( )->mutable_viewangles( )->set_y( real_yaw );
}

void c_anti_hit::on_create_move( c_user_cmd* user_cmd ) {
    if ( !g_cfg->anti_hit.m_enabled ) return;
    if ( !g_interfaces->m_engine->is_in_game( ) ) return;

    if ( !g_ctx->m_local_pawn
         || !g_ctx->m_local_pawn->is_alive( )
         || g_ctx->m_local_pawn->m_move_type( ) == 1794   // noclip
         || g_ctx->m_local_pawn->m_move_type( ) == 2313   // ladder
         || g_ctx->m_local_pawn->is_throwing( ) )
        return;

    apply_pitch( user_cmd );
    apply_yaw( user_cmd );
}
