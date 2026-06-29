#pragma once
#include "darkside.hpp"

// movement.hpp — NL bhop + original darkside auto_stop / auto_strafe / movement_fix

class c_movement {
    bool m_stopped{};

    // NL bhop (from NL feature flag analysis):
    // - Clear IN_JUMP while airborne (not FL_ONGROUND)
    // - Keep IN_JUMP on landing tick (was_airborne=true, now FL_ONGROUND=true)
    // - Skip if FL_DUCKING (avoid crouch-jump interference)
    void bunnyhop( c_user_cmd* user_cmd );

    void auto_strafe( c_user_cmd* user_cmd, float old_yaw );
    void limit_speed( c_user_cmd* user_cmd, c_cs_player_pawn* local_player,
                      c_base_player_weapon* active_weapon, float max_speed );

public:
    void auto_stop( c_user_cmd* user_cmd, c_cs_player_pawn* local_player,
                    c_base_player_weapon* active_weapon, bool no_spread );
    void movement_fix( c_user_cmd* user_cmd, vec3_t angle );
    void on_create_move( c_user_cmd* user_cmd, float old_yaw );
};

inline const auto g_movement = std::make_unique<c_movement>( );
