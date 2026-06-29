#include "movement.hpp"

// NL bhop reconstruction:
// - While in air: clear IN_JUMP each tick (no auto-bunny from hold)
// - Landing tick (was_airborne, now FL_ONGROUND + jump held): keep IN_JUMP
// - Skip if FL_DUCKING to avoid crouch-jump conflict

void c_movement::bunnyhop( c_user_cmd* user_cmd ) {
    if ( !g_cfg->misc.m_bunny_hop ) return;

    static bool was_on_ground = false;
    bool on_ground = ( g_ctx->m_local_pawn->m_flags( ) & FL_ONGROUND ) != 0;
    bool jump_held = ( user_cmd->m_button_state.m_button_state & IN_JUMP ) != 0;
    bool ducking   = ( g_ctx->m_local_pawn->m_flags( ) & FL_DUCKING ) != 0;

    if ( !on_ground ) {
        // In air: suppress jump to prevent bhop stutter
        user_cmd->m_button_state.m_button_state &= ~IN_JUMP;
    } else if ( !was_on_ground && on_ground && jump_held && !ducking ) {
        // Landing tick with jump held: let jump through
        user_cmd->m_button_state.m_button_state |= IN_JUMP;
    } else {
        // On ground and not landing: suppress (player is holding jump)
        if ( on_ground )
            user_cmd->m_button_state.m_button_state &= ~IN_JUMP;
    }

    was_on_ground = on_ground;
}

void c_movement::auto_strafe( c_user_cmd* user_cmd, float old_yaw ) {
    if ( !g_cfg->misc.m_auto_strafe ) return;
    if ( g_ctx->m_local_pawn->m_flags( ) & FL_ONGROUND ) return;
    if ( !user_cmd || !user_cmd->pb.mutable_base( )->mutable_viewangles( ) ) return;

    vec3_t velocity = g_ctx->m_local_pawn->m_vec_abs_velocity( );
    float speed_2d  = velocity.length_2d( );
    if ( speed_2d < 2.f
         && !user_cmd->pb.mutable_base( )->forwardmove( )
         && !user_cmd->pb.mutable_base( )->leftmove( ) )
        return;

    float ideal_rotation = std::min( rad2deg( std::asinf( 15.f / speed_2d ) ), 90.f );
    float sign = user_cmd->pb.mutable_base( )->legacy_command_number( ) % 2 ? 1.f : -1.f;

    bool move_forward  = ( user_cmd->m_button_state.m_button_state & IN_FORWARD )   != 0;
    bool move_backward = ( user_cmd->m_button_state.m_button_state & IN_BACK )      != 0;
    bool move_left     = ( user_cmd->m_button_state.m_button_state & IN_MOVELEFT )  != 0;
    bool move_right    = ( user_cmd->m_button_state.m_button_state & IN_MOVERIGHT ) != 0;

    user_cmd->pb.mutable_base( )->set_forwardmove( speed_2d > 0.1f ? 0.f : 1.f );

    vec3_t movement_angle{
        user_cmd->pb.mutable_base( )->viewangles( ).x( ),
        user_cmd->pb.mutable_base( )->viewangles( ).y( ),
        user_cmd->pb.mutable_base( )->viewangles( ).z( )
    };

    if ( move_forward )
        movement_angle.y += move_left ? 45.f : move_right ? -45.f : 0.f;
    else if ( move_backward )
        movement_angle.y += move_left ? 135.f : move_right ? -135.f : 180.f;
    else if ( move_left || move_right )
        movement_angle.y += move_left ? 90.f : -90.f;

    float yaw_delta     = std::remainder( movement_angle.y - old_yaw, 360.f );
    float abs_yaw_delta = std::fabsf( yaw_delta );

    if ( yaw_delta > 0.f )
        user_cmd->pb.mutable_base( )->set_leftmove( -1.f );
    else if ( yaw_delta < 0.f )
        user_cmd->pb.mutable_base( )->set_leftmove( 1.f );

    if ( abs_yaw_delta <= ideal_rotation || abs_yaw_delta >= 30.f ) {
        float vel_ang    = rad2deg( std::atan2f( velocity.y, velocity.x ) );
        float vel_delta  = std::remainder( movement_angle.y - vel_ang, 360.f );
        float retrack    = ideal_rotation * ( ( g_cfg->misc.m_strafe_smooth / 100.f ) * 3 );

        if ( vel_delta <= retrack || speed_2d <= 15.f ) {
            if ( -retrack <= vel_delta || speed_2d <= 15.f ) {
                movement_angle.y += ideal_rotation * sign;
                user_cmd->pb.mutable_base( )->set_leftmove( sign );
            } else {
                movement_angle.y = vel_ang - retrack;
                user_cmd->pb.mutable_base( )->set_leftmove( 1.f );
            }
        } else {
            movement_angle.y = vel_ang + retrack;
            user_cmd->pb.mutable_base( )->set_leftmove( -1.f );
        }
    }

    const float rotation = rad2deg(
        user_cmd->pb.mutable_base( )->viewangles( ).y( ) - movement_angle.y );
    const float new_fwd  = std::cosf( rotation ) * user_cmd->pb.mutable_base( )->forwardmove( )
                         - std::sinf( rotation ) * user_cmd->pb.mutable_base( )->leftmove( );
    const float new_side = std::sinf( rotation ) * user_cmd->pb.mutable_base( )->forwardmove( )
                         + std::cosf( rotation ) * user_cmd->pb.mutable_base( )->leftmove( );

    auto clamp1 = []( float v ) { return v < -1.f ? -1.f : v > 1.f ? 1.f : v; };
    user_cmd->pb.mutable_base( )->set_leftmove( clamp1( new_side * -1.f ) );
    user_cmd->pb.mutable_base( )->set_forwardmove( clamp1( new_fwd ) );
}

void c_movement::limit_speed( c_user_cmd* user_cmd, c_cs_player_pawn* local_player,
                               c_base_player_weapon* active_weapon, float max_speed ) {
    auto mv = local_player->m_movement_services( );
    if ( !mv ) return;

    vec3_t velocity = g_ctx->m_local_pawn->m_vec_abs_velocity( );
    float fl = user_cmd->pb.mutable_base( )->leftmove( );
    float ff = user_cmd->pb.mutable_base( )->forwardmove( );
    float cmd_speed = std::sqrtf( fl * fl + ff * ff );
    float speed_2d  = velocity.length_2d( );

    if ( cmd_speed <= 50.f && speed_2d <= 50.f ) return;

    float accelerate = g_interfaces->m_var->get_by_name( xorstr_( "sv_accelerate" ) )->get_float( );
    vec3_t forward{}, right{}, up{};
    vec3_t view_angles{
        (float)user_cmd->pb.mutable_base( )->viewangles( ).x( ),
        (float)user_cmd->pb.mutable_base( )->viewangles( ).y( ),
        (float)user_cmd->pb.mutable_base( )->viewangles( ).z( )
    };
    g_math->angle_vectors( view_angles, forward, right, up );

    float max_accel = accelerate * INTERVAL_PER_TICK
        * std::max( 250.f, mv->m_max_speed( ) * mv->m_surface_friction( ) );
    float diff      = speed_2d - max_speed;
    float wish_speed = ( diff - max_accel <= 0.f || speed_2d - max_accel - 3.f <= 0.f )
                       ? max_speed : -1.f;

    if ( ff > 0 )       user_cmd->pb.mutable_base( )->set_forwardmove(  wish_speed );
    else if ( ff < 0 )  user_cmd->pb.mutable_base( )->set_forwardmove( -wish_speed );
    if ( fl > 0 )       user_cmd->pb.mutable_base( )->set_leftmove(  wish_speed );
    else if ( fl < 0 )  user_cmd->pb.mutable_base( )->set_leftmove( -wish_speed );
}

void c_movement::auto_stop( c_user_cmd* user_cmd, c_cs_player_pawn* local_player,
                             c_base_player_weapon* active_weapon, bool no_spread ) {
    if ( !g_cfg->rage_bot.m_auto_stop ) return;
    if ( no_spread ) return;
    if ( !( local_player->m_flags( ) & FL_ONGROUND ) ) return;

    auto remove_button = [&]( int button ) {
        user_cmd->m_button_state.m_button_state  &= ~button;
        user_cmd->m_button_state.m_button_state2 &= ~button;
        user_cmd->m_button_state.m_button_state3 &= ~button;
    };
    remove_button( IN_SPEED );
    limit_speed( user_cmd, local_player, active_weapon,
                 active_weapon->get_max_speed( ) * 0.25f );
}

void c_movement::movement_fix( c_user_cmd* user_cmd, vec3_t angle ) {
    vec3_t wish_angle{
        (float)user_cmd->pb.mutable_base( )->viewangles( ).x( ),
        (float)user_cmd->pb.mutable_base( )->viewangles( ).y( ),
        (float)user_cmd->pb.mutable_base( )->viewangles( ).z( )
    };
    int sign = wish_angle.x > 89.f ? -1 : 1;
    wish_angle.clamp( );

    vec3_t fwd, right, up, old_fwd, old_right, old_up;
    g_math->angle_vectors( wish_angle, fwd, right, up );
    fwd.z = right.z = up.x = up.y = 0.f;
    fwd.normalize_in_place( );
    right.normalize_in_place( );
    up.normalize_in_place( );

    g_math->angle_vectors( angle, old_fwd, old_right, old_up );
    old_fwd.z = old_right.z = old_up.x = old_up.y = 0.f;
    old_fwd.normalize_in_place( );
    old_right.normalize_in_place( );
    old_up.normalize_in_place( );

    float fm = (float)user_cmd->pb.mutable_base( )->forwardmove( );
    float lm = (float)user_cmd->pb.mutable_base( )->leftmove( );
    float um = (float)user_cmd->pb.mutable_base( )->upmove( );

    fwd   *= fm; right *= lm; up *= um;

    auto clamp1 = []( float v ) { return v < -1.f ? -1.f : v > 1.f ? 1.f : v; };

    float fixed_fwd  = old_fwd.dot( right ) + old_fwd.dot( fwd ) + old_fwd.dot( up, true );
    float fixed_side = old_right.dot( right ) + old_right.dot( fwd ) + old_right.dot( up, true );

    user_cmd->pb.mutable_base( )->set_forwardmove( clamp1( (float)sign * fixed_fwd ) );
    user_cmd->pb.mutable_base( )->set_leftmove( clamp1( fixed_side ) );
}

void c_movement::on_create_move( c_user_cmd* user_cmd, float old_yaw ) {
    if ( g_ctx->m_local_pawn->m_move_type( ) == movetype_ladder
         || g_ctx->m_local_pawn->m_move_type( ) == movetype_noclip )
        return;
    bunnyhop( user_cmd );
    auto_strafe( user_cmd, old_yaw );
}
