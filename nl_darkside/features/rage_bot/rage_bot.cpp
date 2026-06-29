#include "rage_bot.hpp"
#include "entity_system/entity.hpp"
#include "../movement/movement.hpp"

// ── lag_record_t ──────────────────────────────────────────────────────────────

void lag_record_t::store( c_cs_player_pawn* pawn ) {
    if ( !pawn || !pawn->is_player_pawn( ) || !pawn->is_alive( ) ) return;
    auto scene = pawn->m_scene_node( );
    if ( !scene ) return;
    auto skel = scene->get_skeleton_instance( );
    if ( !skel ) return;
    if ( !pawn->m_collision( ) ) return;

    static auto setup_bones = reinterpret_cast<void( __fastcall* )( void*, int )>(
        g_opcodes->scan_absolute( g_modules->m_modules.client_dll.get_name( ),
            "E8 ? ? ? ? 49 8B 94 24 ? ? ? ? 48 8B CF", 0x1 ) );

    m_pawn            = pawn;
    m_skeleton        = skel;
    m_origin          = scene->m_abs_origin( );
    setup_bones( skel, 128 );
    std::memcpy( m_bone_data, skel->m_bone_cache,
                 sizeof( matrix2x4_t ) * skel->m_bone_count );
    m_simulation_time = pawn->m_sim_time( );
    m_vec_mins        = pawn->m_collision( )->m_mins( );
    m_vec_maxs        = pawn->m_collision( )->m_maxs( );
    m_vec_velocity    = pawn->m_vec_velocity( );
    m_throwing        = pawn->is_throwing( );
}

void lag_record_t::apply( c_cs_player_pawn* pawn ) {
    if ( !pawn || !pawn->is_player_pawn( ) || !pawn->is_alive( ) ) return;
    auto skel = pawn->m_scene_node( )
                    ? pawn->m_scene_node( )->get_skeleton_instance( ) : nullptr;
    if ( !skel ) return;
    std::memcpy( m_bone_data_backup, skel->m_bone_cache,
                 sizeof( matrix2x4_t ) * skel->m_bone_count );
    std::memcpy( skel->m_bone_cache, m_bone_data,
                 sizeof( matrix2x4_t ) * skel->m_bone_count );
}

void lag_record_t::reset( c_cs_player_pawn* pawn ) {
    if ( !pawn || !pawn->is_player_pawn( ) || !pawn->is_alive( ) ) return;
    auto skel = pawn->m_scene_node( )
                    ? pawn->m_scene_node( )->get_skeleton_instance( ) : nullptr;
    if ( !skel ) return;
    std::memcpy( skel->m_bone_cache, m_bone_data_backup,
                 sizeof( matrix2x4_t ) * skel->m_bone_count );
}

bool lag_record_t::is_valid( ) {
    auto ld = g_prediction->get_local_data( );
    if ( !ld ) return false;
    float dead_time = ( ld->m_tick_base * INTERVAL_PER_TICK ) - m_simulation_time;
    return dead_time < .2f;
}

// ── store_records (push to ring every tick) ───────────────────────────────────

void c_rage_bot::store_records( ) {
    if ( !g_interfaces->m_engine->is_in_game( ) || !g_ctx->m_local_pawn ) return;

    for ( auto entity : g_entity_system->get( "CCSPlayerController" ) ) {
        auto ctrl = reinterpret_cast<c_cs_player_controller*>( entity );
        if ( !ctrl || ctrl == g_ctx->m_local_controller ) continue;

        int handle = ctrl->get_handle( ).to_int( );

        if ( !ctrl->m_pawn_is_alive( ) ) {
            m_rings.erase( handle );
            continue;
        }

        auto pawn = reinterpret_cast<c_cs_player_pawn*>(
            g_interfaces->m_entity_system->get_base_entity(
                ctrl->m_pawn( ).get_entry_index( ) ) );
        if ( !pawn )                                                    { m_rings.erase( handle ); continue; }
        if ( pawn->m_team_num( ) == g_ctx->m_local_pawn->m_team_num( ) ) continue;
        if ( pawn == g_ctx->m_local_pawn )                              continue;

        m_rings[handle].push( pawn );
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────

int c_rage_bot::get_hitbox_from_menu( int hitbox ) {
    switch ( hitbox ) {
    case HITBOX_HEAD:                                                       return 0;
    case HITBOX_CHEST: case HITBOX_LOWER_CHEST: case HITBOX_UPPER_CHEST:    return 1;
    case HITBOX_PELVIS: case HITBOX_STOMACH:                                return 2;
    case HITBOX_RIGHT_HAND: case HITBOX_LEFT_HAND:
    case HITBOX_RIGHT_UPPER_ARM: case HITBOX_RIGHT_FOREARM:
    case HITBOX_LEFT_UPPER_ARM:  case HITBOX_LEFT_FOREARM:                  return 3;
    case HITBOX_RIGHT_THIGH: case HITBOX_LEFT_THIGH:
    case HITBOX_RIGHT_CALF:  case HITBOX_LEFT_CALF:                         return 4;
    case HITBOX_RIGHT_FOOT:  case HITBOX_LEFT_FOOT:                         return 5;
    }
    return -1;
}

hitbox_data_t c_rage_bot::get_hitbox_data( c_cs_player_pawn* pawn, int hitbox_id ) {
    hitbox_data_t out;
    if ( !pawn ) return out;
    auto scene = pawn->m_scene_node( );   if ( !scene ) return out;
    auto skel  = scene->get_skeleton_instance( ); if ( !skel ) return out;
    auto& ms   = skel->m_model_state( );
    auto& mdl  = ms.m_model( );          if ( !mdl ) return out;
    auto hbox  = mdl->get_hitbox( hitbox_id ); if ( !hbox ) return out;
    int bidx   = pawn->get_bone_index( hbox->m_bone_name );
    out.m_num_hitbox   = hitbox_id;
    out.m_num_bone     = bidx;
    out.m_hitbox_data  = hbox;
    out.m_radius       = hbox->m_shape_radius;
    out.m_num_hitgroup = hbox->m_hitgroup;
    out.m_matrix       = g_math->transform_to_matrix( ms.get_bone_data( )[bidx] );
    out.m_mins         = hbox->m_vec_min;
    out.m_maxs         = hbox->m_vec_maxs;
    out.m_invalid_data = false;
    return out;
}

vec3_t c_rage_bot::get_removed_aim_punch_angle( c_cs_player_pawn* local_player ) {
    using fn_t = void( __fastcall* )( void*, vec3_t*, bool );
    static fn_t fn = reinterpret_cast<fn_t>(
        g_opcodes->scan_absolute( g_modules->m_modules.client_dll.get_name( ),
            xorstr_( "E8 ? ? ? ? 4C 8B C0 48 8D 55 ? 48 8B CB E8 ? ? ? ? 48 8D 0D" ), 0x1 ) );
    vec3_t out{};
    fn( local_player, &out, true );
    return out;
}

// ── NL multipoint ─────────────────────────────────────────────────────────────
// Points generated in bone-local space, transformed by the record's bone matrix.
// Radius shrinks with distance: base/sqrt(dist+1)  (NL_GetScaledValue mode 1)

float c_rage_bot::nl_point_radius( const vec3_t& eye_pos, const vec3_t& center,
                                   float hitbox_radius ) {
    float dist = center.dist( eye_pos );
    float r    = nl_cfg.rage_bot.m_multipoint_base / std::sqrtf( dist + 1.f );
    if ( r < 0.f )                     r = 0.f;
    if ( r > hitbox_radius * 0.95f )   r = hitbox_radius * 0.95f;
    return r;
}

bool c_rage_bot::multi_points( lag_record_t* rec, int hitbox,
                               std::vector<aim_point_t>& out ) {
    if ( !rec ) return false;
    auto pawn = rec->m_pawn;
    if ( !pawn || !pawn->is_alive( ) ) return false;

    hitbox_data_t hd = get_hitbox_data( pawn, hitbox );
    if ( hd.m_invalid_data ) return false;

    int menu_idx = get_hitbox_from_menu( hitbox );
    if ( menu_idx == -1 ) return false;

    auto ld = g_prediction->get_local_data( );
    if ( !ld ) return false;

    // Use stored bone matrix for the record, not the live skeleton
    matrix3x4_t mat = g_math->transform_to_matrix( rec->m_bone_data[hd.m_num_bone] );

    vec3_t local_center = ( hd.m_mins + hd.m_maxs ) * 0.5f;
    vec3_t world_center;
    g_math->vector_transform( local_center, mat, world_center );

    out.emplace_back( world_center, hitbox, true );   // center — preferred

    if ( !g_cfg->rage_bot.m_multi_points[menu_idx] )
        return true;

    float radius = nl_point_radius( ld->m_eye_pos, world_center, hd.m_radius );
    if ( radius <= 0.f ) return true;

    // Raw bone origin (backup)
    out.emplace_back( vec3_t{ mat[0][3], mat[1][3], mat[2][3] }, hitbox );

    // Cardinal ring — bone-local offsets, then transform
    const vec3_t offsets[4] = {
        { radius, 0.f, 0.f }, { -radius, 0.f, 0.f },
        { 0.f, radius, 0.f }, { 0.f, -radius, 0.f }
    };
    for ( const auto& off : offsets ) {
        vec3_t local_pt = local_center + off;
        vec3_t world_pt;
        g_math->vector_transform( local_pt, mat, world_pt );
        out.emplace_back( world_pt, hitbox );
    }

    // Head gets top/bottom pair
    if ( hitbox == HITBOX_HEAD ) {
        for ( float sign : { 1.f, -1.f } ) {
            vec3_t local_pt = local_center + vec3_t{ 0.f, 0.f, sign * radius };
            vec3_t world_pt;
            g_math->vector_transform( local_pt, mat, world_pt );
            out.emplace_back( world_pt, hitbox );
        }
    }

    return true;
}

// ── Spread correction ─────────────────────────────────────────────────────────
// NL_ApplySpreadCorr @ 0x1a936DFAB80:
//   corrected_forward = normalize(forward - right*sx - up*sy)

vec3_t c_rage_bot::nl_spread_correction( vec3_t aim_rcs, float spread_x, float spread_y ) {
    if ( spread_x == 0.f && spread_y == 0.f )
        return aim_rcs;

    vec3_t fwd, right, up;
    g_math->angle_vectors( aim_rcs, fwd, right, up );

    vec3_t corrected = ( fwd - right * spread_x - up * spread_y ).normalize( );

    vec3_t result{};
    g_math->vector_angles( corrected, result );   // void(vec3_t, vec3_t&)
    return result;
}

// ── select_points ─────────────────────────────────────────────────────────────
// NL tie-break: strictly_better OR (same damage AND center AND current_best_is_edge)
// Hitgroup filter: reject if pen_data.hitgroup != expected

aim_point_t c_rage_bot::select_points( lag_record_t* rec, float& out_damage ) {
    aim_point_t best{ vec3_t{}, -1 };
    out_damage = 0.f;
    bool best_is_center = false;

    if ( !g_ctx->m_local_pawn->is_alive( ) ) return best;
    auto weapon  = g_ctx->m_local_pawn->get_active_weapon( );
    auto wdata   = weapon ? weapon->get_weapon_data( ) : nullptr;
    if ( !wdata ) return best;
    auto ld      = g_prediction->get_local_data( );
    if ( !ld ) return best;
    bool is_taser = wdata->m_weapon_type( ) == WEAPONTYPE_TASER;

    for ( int hbox : m_hitboxes ) {
        std::vector<aim_point_t> pts;
        pts.reserve( 12 );
        if ( !multi_points( rec, hbox, pts ) ) continue;

        hitbox_data_t hd = get_hitbox_data( rec->m_pawn, hbox );

        for ( auto& pt : pts ) {
            penetration_data_t pen{};
            if ( !g_auto_wall->fire_bullet( ld->m_eye_pos, pt.m_point,
                                            rec->m_pawn, wdata, pen, is_taser ) )
                continue;

            if ( !hd.m_invalid_data && pen.m_hitgroup != hd.m_num_hitgroup )
                continue;

            float dmg            = pen.m_damage;
            bool strictly_better = dmg > out_damage + 0.5f;
            bool prefer_center   = ( dmg >= out_damage - 0.5f )
                                   && pt.m_center && !best_is_center;

            if ( strictly_better || prefer_center ) {
                best           = pt;
                out_damage     = dmg;
                best_is_center = pt.m_center;
            }
        }
    }

    return best;
}

// ── Resolver (NL_Resolver @ 0x1a936F48C70) ───────────────────────────────────
// Tests 3 or 7 yaw candidates anchored at LBY, picks max fire_bullet score.
// If best_score < 30: refinement ±8° in 2° steps.
// Auto-flip: branch_idx++ every 3 misses.

void c_rage_bot::nl_resolve( int handle, c_cs_player_pawn* enemy, lag_record_t* rec,
                              const vec3_t& eye_pos, c_cs_weapon_base_v_data* wdata ) {
    if ( !nl_cfg.rage_bot.m_resolver ) return;
    if ( !enemy || !enemy->is_alive( ) ) return;

    nl_resolver_state_t& rs = m_resolver_states[handle];

    float lby = get_lby_approx( rec );

    bool use_7 = nl_cfg.rage_bot.m_spread_correct;
    const float* branches    = use_7 ? NL_BRANCHES_7 : NL_BRANCHES_3;
    int          branch_count = use_7 ? 7 : 3;

    int   best_branch = rs.branch_idx % branch_count;
    float best_score  = 0.f;

    hitbox_data_t hd = get_hitbox_data( enemy, HITBOX_HEAD );
    if ( hd.m_invalid_data ) return;

    bool is_taser = wdata->m_weapon_type( ) == WEAPONTYPE_TASER;

    for ( int i = 0; i < branch_count; i++ ) {
        float cy = std::cosf( ( lby + branches[i] ) * ( (float)_pi / 180.f ) );
        float sy = std::sinf( ( lby + branches[i] ) * ( (float)_pi / 180.f ) );

        matrix3x4_t mat = g_math->transform_to_matrix( rec->m_bone_data[hd.m_num_bone] );
        vec3_t head{ mat[0][3] + cy * 2.f, mat[1][3] + sy * 2.f, mat[2][3] };

        penetration_data_t pen{};
        if ( !g_auto_wall->fire_bullet( eye_pos, head, enemy, wdata, pen, is_taser ) )
            continue;
        if ( pen.m_damage > best_score ) {
            best_score  = pen.m_damage;
            best_branch = i;
        }
    }

    rs.branch_idx = best_branch;

    // Refinement: sweep ±8° in 2° steps if best_score < 30
    float base_offset = branches[best_branch];
    if ( best_score < 30.f ) {
        for ( int deg = -8; deg <= 8; deg += 2 ) {
            float angle  = lby + base_offset + static_cast<float>( deg );
            float cy     = std::cosf( angle * ( (float)_pi / 180.f ) );
            float sy     = std::sinf( angle * ( (float)_pi / 180.f ) );

            matrix3x4_t mat = g_math->transform_to_matrix( rec->m_bone_data[hd.m_num_bone] );
            vec3_t head{ mat[0][3] + cy * 2.f, mat[1][3] + sy * 2.f, mat[2][3] };

            penetration_data_t pen{};
            if ( !g_auto_wall->fire_bullet( eye_pos, head, enemy, wdata, pen, is_taser ) )
                continue;
            if ( pen.m_damage > best_score ) {
                best_score = pen.m_damage;
                rs.resolved_yaw = angle;
            }
        }
    } else {
        rs.resolved_yaw = lby + base_offset;
    }
}

// ── Strided candidate evaluation ──────────────────────────────────────────────
// NL strides across all valid ring slots instead of taking the N newest.
// Live lead is evaluated first and wins on ties.

lag_record_t* c_rage_bot::evaluate_candidates( int handle, c_cs_player_pawn* pawn ) {
    if ( !pawn ) return nullptr;

    static auto sv_maxunlag = g_interfaces->m_var->get_by_name( "sv_maxunlag" );
    const int max_ticks = TIME_TO_TICKS( sv_maxunlag->get_float( ) );

    auto ring_it = m_rings.find( handle );
    if ( ring_it == m_rings.end( ) ) return nullptr;
    nl_ring_buffer_t& ring = ring_it->second;
    if ( ring.valid_count == 0 ) return nullptr;

    auto weapon = g_ctx->m_local_pawn->get_active_weapon( );
    auto wdata  = weapon ? weapon->get_weapon_data( ) : nullptr;
    if ( !wdata ) return nullptr;

    int max_cands = nl_cfg.rage_bot.m_bt_max_cands;
    int stride    = ring.valid_count / max_cands;
    if ( stride < 1 ) stride = 1;

    lag_record_t* best_rec = nullptr;
    float         best_dmg = 0.f;

    for ( int offset = 0; offset < ring.valid_count; offset += stride ) {
        lag_record_t* rec = ring.get( offset, nl_cfg.rage_bot.m_wrap_mode, max_ticks );
        if ( !rec || !rec->m_pawn || !rec->is_valid( ) ) continue;

        rec->apply( pawn );
        float dmg = 0.f;
        aim_point_t pt = select_points( rec, dmg );
        rec->reset( pawn );

        if ( dmg > best_dmg && pt.m_hitbox != -1 ) {
            best_dmg = dmg;
            best_rec = rec;
        }
    }

    return best_rec;
}

// ── find_targets / select_target ──────────────────────────────────────────────

void c_rage_bot::store_hitboxes( ) {
    if ( g_cfg->rage_bot.m_hitboxes[0] ) m_hitboxes.push_back( HITBOX_HEAD );
    if ( g_cfg->rage_bot.m_hitboxes[1] ) {
        m_hitboxes.push_back( HITBOX_CHEST );
        m_hitboxes.push_back( HITBOX_LOWER_CHEST );
        m_hitboxes.push_back( HITBOX_UPPER_CHEST );
    }
    if ( g_cfg->rage_bot.m_hitboxes[2] ) {
        m_hitboxes.push_back( HITBOX_PELVIS );
        m_hitboxes.push_back( HITBOX_STOMACH );
    }
    if ( g_cfg->rage_bot.m_hitboxes[3] ) {
        m_hitboxes.push_back( HITBOX_RIGHT_HAND );
        m_hitboxes.push_back( HITBOX_LEFT_HAND );
        m_hitboxes.push_back( HITBOX_RIGHT_UPPER_ARM );
        m_hitboxes.push_back( HITBOX_RIGHT_FOREARM );
        m_hitboxes.push_back( HITBOX_LEFT_UPPER_ARM );
        m_hitboxes.push_back( HITBOX_LEFT_FOREARM );
    }
    if ( g_cfg->rage_bot.m_hitboxes[4] ) {
        m_hitboxes.push_back( HITBOX_RIGHT_THIGH );
        m_hitboxes.push_back( HITBOX_LEFT_THIGH );
        m_hitboxes.push_back( HITBOX_RIGHT_CALF );
        m_hitboxes.push_back( HITBOX_LEFT_CALF );
    }
    if ( g_cfg->rage_bot.m_hitboxes[5] ) {
        m_hitboxes.push_back( HITBOX_RIGHT_FOOT );
        m_hitboxes.push_back( HITBOX_LEFT_FOOT );
    }
}

void c_rage_bot::find_targets( ) {
    if ( !g_ctx->m_local_pawn->is_alive( ) ) return;
    for ( auto entity : g_entity_system->get( "CCSPlayerController" ) ) {
        auto ctrl = reinterpret_cast<c_cs_player_controller*>( entity );
        if ( !ctrl || ctrl == g_ctx->m_local_controller ) continue;

        int handle = ctrl->get_handle( ).to_int( );
        auto pawn  = reinterpret_cast<c_cs_player_pawn*>(
            g_interfaces->m_entity_system->get_base_entity(
                ctrl->m_pawn( ).get_entry_index( ) ) );
        if ( !pawn ) continue;

        lag_record_t* rec = evaluate_candidates( handle, pawn );
        if ( !rec ) continue;
        m_aim_targets[handle] = aim_target_t( rec );
    }
}

aim_target_t* c_rage_bot::get_nearest_target( ) {
    auto ld = g_prediction->get_local_data( );
    if ( !ld ) return nullptr;
    vec3_t shoot_pos = ld->m_eye_pos;
    aim_target_t* best = nullptr;
    float best_dist    = FLT_MAX;
    for ( auto& kv : m_aim_targets ) {
        aim_target_t* t = &kv.second;
        if ( !t->m_lag_record || !t->m_pawn || !t->m_pawn->is_alive( ) ) continue;
        float d = t->m_pawn->m_scene_node( )
                      ? t->m_pawn->m_scene_node( )->m_abs_origin( ).dist( shoot_pos )
                      : FLT_MAX;
        if ( d < best_dist ) { best_dist = d; best = t; }
    }
    return best;
}

void c_rage_bot::select_target( ) {
    if ( !g_ctx->m_local_pawn->is_alive( ) || m_aim_targets.empty( ) ) return;
    aim_target_t* target = get_nearest_target( );
    if ( !target || !target->m_lag_record || !target->m_pawn->is_alive( ) ) return;

    auto weapon = g_ctx->m_local_pawn->get_active_weapon( );
    auto wdata  = weapon ? weapon->get_weapon_data( ) : nullptr;
    if ( !wdata ) return;
    auto ld     = g_prediction->get_local_data( );
    if ( !ld ) return;

    int handle = target->m_pawn->get_handle( ).to_int( );
    nl_resolve( handle, target->m_pawn, target->m_lag_record, ld->m_eye_pos, wdata );

    int min_dmg = target->m_pawn->m_health( );
    {
        int cfg_min = g_cfg->rage_bot.m_minimum_damage;
        if ( cfg_min < min_dmg ) min_dmg = cfg_min;
    }
    if ( g_key_handler->is_pressed( g_cfg->rage_bot.m_override_damage_key_bind,
                                    g_cfg->rage_bot.m_override_damage_key_bind_style ) ) {
        int ov = g_cfg->rage_bot.m_minimum_damage_override;
        int hp = target->m_pawn->m_health( );
        min_dmg = ov < hp ? ov : hp;
    }

    target->m_lag_record->apply( target->m_pawn );
    float best_dmg = 0.f;
    aim_point_t best_pt = select_points( target->m_lag_record, best_dmg );
    target->m_lag_record->reset( target->m_pawn );

    if ( best_pt.m_hitbox == -1 || best_dmg < (float)min_dmg ) return;

    target->m_best_point = std::make_unique<aim_point_t>( best_pt );
    m_best_target = target;
}

// ── Spread / hit-chance ───────────────────────────────────────────────────────

bool c_rage_bot::weapon_is_at_max_accuracy( c_cs_weapon_base_v_data* wdata, float inaccuracy ) {
    auto ld = g_prediction->get_local_data( );
    if ( !ld ) return false;
    auto round_acc  = []( float a ) { return floorf( a * 170.f ) / 170.f; };
    auto round_duck = []( float a ) { return floorf( a * 300.f ) / 300.f; };
    float speed  = g_ctx->m_local_pawn->m_vec_abs_velocity( ).length( );
    bool scoped  = ( wdata->m_weapon_type( ) == WEAPONTYPE_SNIPER_RIFLE )
                && !( g_ctx->m_user_cmd->m_button_state.m_button_state & IN_ZOOM )
                && !g_ctx->m_local_pawn->m_scoped( );
    bool ducking = ( g_ctx->m_local_pawn->m_flags( ) & FL_DUCKING )
               || ( g_ctx->m_user_cmd->m_button_state.m_button_state & IN_DUCK );
    if ( ducking && scoped && ( g_ctx->m_local_pawn->m_flags( ) & FL_ONGROUND )
         && round_duck( inaccuracy ) < ld->m_inaccuracy ) return true;
    if ( speed <= 0 && scoped && ( g_ctx->m_local_pawn->m_flags( ) & FL_ONGROUND )
         && round_acc( inaccuracy ) < ld->m_inaccuracy ) return true;
    return false;
}

vec3_t c_rage_bot::calculate_spread_angles( vec3_t angle, int seed,
                                            float inaccuracy, float spread ) {
    float r1, r2, r3, r4;
    g_interfaces->m_random_seed( seed + 1 );
    r1 = g_interfaces->m_random_float( 0.f, 1.f );
    r2 = g_interfaces->m_random_float( 0.f, (float)_pi2 );
    r3 = g_interfaces->m_random_float( 0.f, 1.f );
    r4 = g_interfaces->m_random_float( 0.f, (float)_pi2 );
    vec3_t sp{ std::cosf( r2 ) * r1 * inaccuracy + std::cosf( r4 ) * r3 * spread,
               std::sinf( r2 ) * r1 * inaccuracy + std::sinf( r4 ) * r3 * spread };
    vec3_t fwd, right, up;
    g_math->angle_vectors( angle, fwd, right, up );
    return ( fwd + right * sp.x + up * sp.y ).normalize( );
}

int c_rage_bot::calculate_hit_chance( c_cs_player_pawn* pawn, vec3_t angles,
                                      c_base_player_weapon* weapon,
                                      c_cs_weapon_base_v_data* wdata, bool no_spread ) {
    if ( no_spread ) return 100;
    if ( !g_ctx->m_local_pawn->is_alive( ) || !pawn ) return 0;
    auto ld = g_prediction->get_local_data( );
    if ( !ld ) return 0;

    weapon->update_accuracy_penality( );
    if ( weapon_is_at_max_accuracy( wdata, weapon->get_inaccuracy( ) ) ) return 100;

    const float spread     = ld->m_spread;
    const float inaccuracy = ld->m_inaccuracy;
    const int   SEEDS      = 128;

    int hits   = 0, misses = 0;
    int need     = static_cast<int>( std::ceilf( g_cfg->rage_bot.m_hit_chance / 100.f * SEEDS ) );
    int max_miss = SEEDS - need;

    for ( int s = 0; s < SEEDS; s++ ) {
        vec3_t spread_dir = calculate_spread_angles( angles, s, inaccuracy, spread );
        vec3_t result     = spread_dir * wdata->m_range( ) + ld->m_eye_pos;

        ray_t        ray{};
        game_trace_t trace{};
        trace_filter_t filter{};
        g_interfaces->m_trace->init_trace( filter, g_ctx->m_local_pawn, MASK_SHOT, 0x3, 0x7 );
        g_interfaces->m_trace->trace_shape( &ray, ld->m_eye_pos, result, &filter, &trace );
        g_interfaces->m_trace->clip_ray_entity( &ray, ld->m_eye_pos, result,
                                                 pawn, &filter, &trace );

        bool hit = trace.m_hit_entity
            && trace.m_hit_entity->is_player_pawn( )
            && trace.m_hit_entity->get_handle( ).get_entry_index( )
               == pawn->get_handle( ).get_entry_index( );

        if ( hit ) { if ( ++hits   >= need )     return g_cfg->rage_bot.m_hit_chance; }
        else        { if ( ++misses > max_miss )  return 0; }
    }

    return static_cast<int>( ( hits / (float)SEEDS ) * 100.f );
}

// ── Backtrack protobuf rewind ─────────────────────────────────────────────────

void c_rage_bot::process_backtrack( lag_record_t* rec ) {
    if ( !rec ) return;
    const int tick = TIME_TO_TICKS( rec->m_simulation_time ) + 2;

    for ( int i = 0; i < g_ctx->m_user_cmd->pb.input_history_size( ); i++ ) {
        auto ih = g_ctx->m_user_cmd->pb.mutable_input_history( i );
        if ( !ih ) continue;

        if ( g_ctx->m_user_cmd->pb.attack1_start_history_index( ) == -1 ) continue;

        if ( ih->has_cl_interp( ) )   ih->mutable_cl_interp( )->set_frac( 0.f );
        if ( ih->has_sv_interp0( ) ) {
            auto sv0 = ih->mutable_sv_interp0( );
            sv0->set_src_tick( tick ); sv0->set_dst_tick( tick ); sv0->set_frac( 0.f );
        }
        if ( ih->has_sv_interp1( ) ) {
            auto sv1 = ih->mutable_sv_interp1( );
            sv1->set_src_tick( tick ); sv1->set_dst_tick( tick ); sv1->set_frac( 0.f );
        }
        ih->set_render_tick_count( tick );
        g_ctx->m_user_cmd->pb.mutable_base( )->set_client_tick( tick );
        ih->set_player_tick_count( g_ctx->m_local_controller->m_tick_base( ) );
        ih->set_player_tick_fraction(
            g_prediction->get_local_data( )->m_player_tick_fraction );
    }
}

bool c_rage_bot::can_shoot( c_cs_player_pawn* pawn, c_base_player_weapon* weapon ) {
    return weapon->m_clip1( ) > 0
        && weapon->m_next_primary_attack( )
           <= g_ctx->m_local_controller->m_tick_base( ) + 2;
}

void c_rage_bot::process_attack( c_user_cmd* user_cmd, vec3_t angle ) {
    user_cmd->pb.mutable_base( )->mutable_viewangles( )->set_x( 179.f );
    for ( int i = 0; i < user_cmd->pb.input_history_size( ); i++ ) {
        auto c = user_cmd->pb.mutable_input_history( i );
        if ( c ) c->set_player_tick_count(
            g_prediction->get_local_data( )->m_shoot_tick );
    }
    user_cmd->pb.set_attack1_start_history_index( 0 );
    process_backtrack( m_best_target->m_lag_record );
    if ( g_cfg->rage_bot.m_auto_fire )
        user_cmd->m_button_state.m_button_state |= IN_ATTACK;
}

// ── on_create_move ────────────────────────────────────────────────────────────
// Pipeline: nospread (g_no_spread runs first, before this)
//   → store_hitboxes → find_targets → select_target (includes resolver)
//   → aim_direction → RCS → spread_correction → hit_chance → attack

void c_rage_bot::on_create_move( ) {
    if ( !g_cfg->rage_bot.m_enabled ) return;
    if ( !g_interfaces->m_engine->is_in_game( ) ) return;

    m_hitboxes.clear( );
    if ( m_best_target ) m_best_target->reset( );

    auto user_cmd = g_ctx->m_user_cmd;
    auto local    = g_ctx->m_local_pawn;
    if ( !user_cmd || !local || !local->is_alive( ) ) return;

    auto weapon = local->get_active_weapon( );
    if ( !weapon ) return;
    auto wdata = weapon->get_weapon_data( );
    if ( !wdata
         || wdata->m_weapon_type( ) == WEAPONTYPE_KNIFE
         || wdata->m_weapon_type( ) == WEAPONTYPE_GRENADE )
        return;

    store_hitboxes( );
    if ( m_hitboxes.empty( ) ) return;

    find_targets( );
    select_target( );

    if ( !m_best_target || !m_best_target->m_best_point ) return;

    auto ld = g_prediction->get_local_data( );
    if ( !ld ) return;
    if ( !can_shoot( local, weapon ) ) return;

    bool no_spread = g_interfaces->m_var->get_by_name(
        xorstr_( "weapon_accuracy_nospread" ) )->get_bool( );

    // Scope-to-shoot (snipers)
    if ( wdata->m_weapon_type( ) == WEAPONTYPE_SNIPER_RIFLE
         && !( user_cmd->m_button_state.m_button_state & IN_ZOOM )
         && !local->m_scoped( )
         && ( local->m_flags( ) & FL_ONGROUND ) && !no_spread )
        user_cmd->m_button_state.m_button_state |= IN_ATTACK2;

    g_movement->auto_stop( user_cmd, local, weapon, no_spread );

    m_best_target->m_lag_record->apply( m_best_target->m_pawn );

    // Step 1: raw aim direction
    vec3_t aim = g_math->aim_direction( ld->m_eye_pos,
                                        m_best_target->m_best_point->m_point );

    // Step 2: RCS 100% — NL_ApplyRCS @ 0x1a936ED7940 — no scale
    vec3_t aim_rcs = aim - get_removed_aim_punch_angle( local );

    // Step 3: spread correction (when not already using no_spread cvar)
    vec3_t final_angle = aim_rcs;
    if ( nl_cfg.rage_bot.m_spread_correct && !no_spread ) {
        float sx = ld->m_spread    * 0.5f;
        float sy = ld->m_inaccuracy * 0.5f;
        final_angle = nl_spread_correction( aim_rcs, sx, sy );
    }

    int hit_chance = calculate_hit_chance( m_best_target->m_pawn, final_angle,
                                           weapon, wdata, no_spread );

    bool is_taser = wdata->m_weapon_type( ) == WEAPONTYPE_TASER;
    if ( hit_chance >= ( is_taser ? 70 : g_cfg->rage_bot.m_hit_chance ) ) {
        for ( int i = 0; i < user_cmd->pb.input_history_size( ); i++ ) {
            auto tick = user_cmd->pb.mutable_input_history( i );
            if ( tick ) {
                tick->mutable_view_angles( )->set_x( final_angle.x );
                tick->mutable_view_angles( )->set_y( final_angle.y );
                tick->mutable_view_angles( )->set_z( final_angle.z );
            }
        }
        if ( !g_cfg->rage_bot.m_silent )
            g_interfaces->m_csgo_input->set_view_angles( final_angle );
        process_attack( user_cmd, final_angle );
    }

    m_best_target->m_lag_record->reset( m_best_target->m_pawn );
}
