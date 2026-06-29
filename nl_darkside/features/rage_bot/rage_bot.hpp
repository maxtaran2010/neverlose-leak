#pragma once
#include "../../darkside.hpp"
#include "features/auto_wall/auto_wall.hpp"
#include "features/eng_pred/eng_pred.hpp"

// ── NL ring buffer (NL_AimbotMath @ 0x1a936ECC4A0) ───────────────────────────
// 1024 fixed slots, wrap via nl_bt_wrap(), unconditional push every tick.

static constexpr int NL_BT_SLOTS = 1024;

struct lag_record_t {
    vec3_t            m_origin{};
    c_cs_player_pawn* m_pawn{};
    c_skeleton_instace* m_skeleton = nullptr;
    c_bone_data       m_bone_data[128];
    c_bone_data       m_bone_data_backup[128];
    float             m_simulation_time{};
    vec3_t            m_vec_mins{};
    vec3_t            m_vec_maxs{};
    vec3_t            m_vec_velocity{};
    bool              m_throwing{};

    lag_record_t() = default;
    void store( c_cs_player_pawn* pawn );
    void apply( c_cs_player_pawn* pawn );
    void reset( c_cs_player_pawn* pawn );
    bool is_valid( );
};

// NL_BacktrackWrap @ 0x1a936ECBF90 (113 bytes)
// mode 0=clamp, 1=ping-pong, 2=circular (default MM)
inline int nl_bt_wrap( int tick_offset, int mode ) {
    switch ( mode ) {
    case 2: {
        int idx = tick_offset % NL_BT_SLOTS;
        return idx >= 0 ? idx : idx + NL_BT_SLOTS;
    }
    case 1: {
        int idx = tick_offset % ( NL_BT_SLOTS * 2 );
        if ( idx < 0 ) idx += NL_BT_SLOTS * 2;
        return idx < NL_BT_SLOTS ? idx : ( NL_BT_SLOTS * 2 - 1 - idx );
    }
    default:
        if ( tick_offset < 0 )         return 0;
        if ( tick_offset >= NL_BT_SLOTS ) return NL_BT_SLOTS - 1;
        return tick_offset;
    }
}

struct nl_ring_buffer_t {
    lag_record_t slots[NL_BT_SLOTS]{};
    int          write_head  = 0;
    int          valid_count = 0;

    void push( c_cs_player_pawn* pawn ) {
        slots[write_head % NL_BT_SLOTS].store( pawn );
        write_head++;
        if ( valid_count < NL_BT_SLOTS ) valid_count++;
    }

    lag_record_t* get( int n_ago, int wrap_mode, int max_ticks ) {
        int clamped = ( n_ago > max_ticks ) ? max_ticks : n_ago;
        return &slots[ nl_bt_wrap( write_head - 1 - clamped, wrap_mode ) % NL_BT_SLOTS ];
    }

    void clear( ) { write_head = 0; valid_count = 0; }
};

// ── Resolver state (NL_Resolver @ 0x1a936F48C70) ─────────────────────────────
// 3-branch: LBY+{0, +58, -58}  /  7-branch: LBY+{0,+58,-58,+29,-29,+89,-89}

static constexpr float NL_BRANCHES_3[3] = {  0.f,  58.f, -58.f };
static constexpr float NL_BRANCHES_7[7] = {  0.f,  58.f, -58.f, 29.f, -29.f, 89.f, -89.f };

struct nl_resolver_state_t {
    float resolved_yaw = 0.f;
    int   miss_count   = 0;
    int   branch_idx   = 0;

    void on_miss( ) { if ( ++miss_count >= 3 ) { branch_idx++; miss_count = 0; } }
    void on_hit( )  { miss_count = 0; }
};

// ── Hitbox / aim helpers ──────────────────────────────────────────────────────

struct hitbox_data_t {
    int         m_num_hitbox;
    int         m_num_bone;
    int         m_num_hitgroup;
    c_hitbox*   m_hitbox_data;
    float       m_radius;
    matrix3x4_t m_matrix;
    vec3_t      m_mins;
    vec3_t      m_maxs;
    bool        m_invalid_data{ true };
};

struct aim_point_t {
    aim_point_t() = default;
    aim_point_t( vec3_t p, int hbox, bool center = false )
        : m_point( p ), m_hitbox( hbox ), m_center( center ) {}
    vec3_t m_point{};
    int    m_hitbox{};
    bool   m_center{};
};

struct aim_target_t {
    lag_record_t*             m_lag_record{};
    std::unique_ptr<aim_point_t> m_best_point{};
    c_cs_player_pawn*         m_pawn{ nullptr };

    aim_target_t() = default;
    aim_target_t( lag_record_t* r ) { store( r ); }

    void store( lag_record_t* r ) {
        if ( !r || !r->m_pawn ) return;
        m_lag_record = r;
        m_pawn       = r->m_pawn;
    }
    void reset( ) {
        m_lag_record = nullptr;
        m_pawn       = nullptr;
        m_best_point.reset( );
    }
};

// ── Main class ────────────────────────────────────────────────────────────────

class c_rage_bot {
    std::unordered_map<int, nl_ring_buffer_t>    m_rings{};
    std::unordered_map<int, nl_resolver_state_t> m_resolver_states{};
    std::unordered_map<int, aim_target_t>        m_aim_targets{};
    aim_target_t*                                m_best_target{};
    std::vector<int>                             m_hitboxes;

    // LBY approximation: use velocity angle when moving, else 0
    // CS2 real LBY is in animstate->m_footyaw (+0x118), but animstate accessor
    // isn't in darkside SDK; velocity angle is a safe runtime substitute.
    float get_lby_approx( lag_record_t* rec ) {
        vec3_t vel = rec->m_vec_velocity;
        if ( vel.length_2d( ) > 5.f )
            return rad2deg( std::atan2f( vel.y, vel.x ) );
        return 0.f;
    }

    int           get_hitbox_from_menu( int hitbox );
    hitbox_data_t get_hitbox_data( c_cs_player_pawn* pawn, int hitbox_id );
    vec3_t        get_removed_aim_punch_angle( c_cs_player_pawn* local_player );

    // NL_GetScaledValue @ 0x1a936EC5D10 mode 1: radius = base / sqrt(dist+1)
    float nl_point_radius( const vec3_t& eye_pos, const vec3_t& center, float hitbox_radius );
    bool  multi_points( lag_record_t* rec, int hitbox, std::vector<aim_point_t>& out );

    // NL_ApplySpreadCorr @ 0x1a936DFAB80: pre-rotate aim to cancel engine spread
    vec3_t nl_spread_correction( vec3_t aim_rcs, float spread_x, float spread_y );

    // Strided backtrack candidate selection (NL_AimbotMath stride pattern)
    lag_record_t* evaluate_candidates( int handle, c_cs_player_pawn* pawn );

    aim_point_t select_points( lag_record_t* rec, float& out_damage );

    // NL_Resolver @ 0x1a936F48C70: 3/7 branch yaw search + refinement pass
    void nl_resolve( int handle, c_cs_player_pawn* enemy, lag_record_t* rec,
                     const vec3_t& eye_pos, c_cs_weapon_base_v_data* wdata );

    void store_hitboxes( );
    void find_targets( );
    aim_target_t* get_nearest_target( );
    void select_target( );

    bool   weapon_is_at_max_accuracy( c_cs_weapon_base_v_data* wdata, float inaccuracy );
    vec3_t calculate_spread_angles( vec3_t angle, int seed, float inaccuracy, float spread );
    int    calculate_hit_chance( c_cs_player_pawn* pawn, vec3_t angles,
                                 c_base_player_weapon* weapon, c_cs_weapon_base_v_data* wdata,
                                 bool no_spread );
    void   process_backtrack( lag_record_t* rec );
    bool   can_shoot( c_cs_player_pawn* pawn, c_base_player_weapon* weapon );
    void   process_attack( c_user_cmd* user_cmd, vec3_t angle );

public:
    void store_records( );
    void on_create_move( );
};

inline const auto g_rage_bot = std::make_unique<c_rage_bot>( );
