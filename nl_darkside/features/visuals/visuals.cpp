#include "visuals.hpp"

// Bone pairs for skeleton (same as darkside reference)
static constexpr std::pair<int, int> BONE_PAIRS[] = {
    { 0,  1  }, // head → neck_0
    { 1,  2  }, // neck_0 → spine_1
    { 2,  3  }, // spine_1 → spine_2
    { 3,  4  }, // spine_2 → spine_3
    { 4,  5  }, // spine_3 → clavicle_l
    { 4,  8  }, // spine_3 → clavicle_r
    { 5,  6  }, // clavicle_l → arm_upper_l
    { 6,  7  }, // arm_upper_l → arm_lower_l
    { 8,  9  }, // clavicle_r → arm_upper_r
    { 9,  10 }, // arm_upper_r → arm_lower_r
    { 4,  12 }, // spine_3 → hip
    { 12, 13 }, // hip → knee_l
    { 13, 14 }, // knee_l → ankle_l
    { 12, 16 }, // hip → knee_r
    { 16, 17 }, // knee_r → ankle_r
};

bool c_visuals::calculate_bbox( c_cs_player_pawn* pawn, vec3_t& top, vec3_t& bot,
                                 float& w, float& h ) {
    if ( !pawn ) return false;
    auto col = pawn->m_collision( );
    if ( !col ) return false;

    auto scene = pawn->m_scene_node( );
    if ( !scene ) return false;

    vec3_t origin    = scene->m_abs_origin( );
    vec3_t world_top = { origin.x, origin.y, origin.z + col->m_maxs( ).z };
    vec3_t world_bot = { origin.x, origin.y, origin.z + col->m_mins( ).z };

    vec3_t screen_top{}, screen_bot{};
    if ( !g_render->world_to_screen( world_top, screen_top ) ) return false;
    if ( !g_render->world_to_screen( world_bot, screen_bot ) ) return false;

    h = screen_bot.y - screen_top.y;
    w = h / 3.f;

    top = screen_top;
    bot = screen_bot;

    return h > 4.f;
}

void c_visuals::draw_skeleton( c_cs_player_pawn* pawn, color_t col ) {
    if ( !pawn ) return;
    auto scene = pawn->m_scene_node( );
    if ( !scene ) return;
    auto skel = scene->get_skeleton_instance( );
    if ( !skel ) return;

    for ( const auto& [a, b] : BONE_PAIRS ) {
        auto& bd = skel->m_bone_cache;
        matrix3x4_t mat_a = g_math->transform_to_matrix( bd[a] );
        matrix3x4_t mat_b = g_math->transform_to_matrix( bd[b] );
        vec3_t wa{ mat_a[0][3], mat_a[1][3], mat_a[2][3] };
        vec3_t wb{ mat_b[0][3], mat_b[1][3], mat_b[2][3] };
        vec3_t sa{}, sb{};
        if ( !g_render->world_to_screen( wa, sa ) ) continue;
        if ( !g_render->world_to_screen( wb, sb ) ) continue;
        g_render->line( (int)sa.x, (int)sa.y, (int)sb.x, (int)sb.y, col );
    }
}

// NL head dot: project HITBOX_HEAD center and draw small filled circle
void c_visuals::draw_head_dot( c_cs_player_pawn* pawn, color_t col ) {
    if ( !pawn ) return;
    auto scene = pawn->m_scene_node( );
    if ( !scene ) return;
    auto skel = scene->get_skeleton_instance( );
    if ( !skel ) return;
    auto& ms = skel->m_model_state( );
    auto  mdl = ms.m_model( );
    if ( !mdl ) return;

    auto hbox = mdl->get_hitbox( HITBOX_HEAD );
    if ( !hbox ) return;

    int bone_idx = pawn->get_bone_index( hbox->m_bone_name );
    if ( bone_idx < 0 ) return;

    matrix3x4_t mat = g_math->transform_to_matrix( skel->m_bone_cache[bone_idx] );
    vec3_t world_center{};
    vec3_t local_center = ( hbox->m_vec_min + hbox->m_vec_maxs ) * 0.5f;
    g_math->vector_transform( local_center, mat, world_center );

    vec3_t screen{};
    if ( !g_render->world_to_screen( world_center, screen ) ) return;
    g_render->circle_filled( (int)screen.x, (int)screen.y, 4, col );
}

void c_visuals::handle_player( const stored_player_t& p ) {
    if ( !p.m_valid || !p.m_pawn ) return;
    if ( !g_cfg->visuals.m_enabled )                         return;

    bool enemy = p.m_is_enemy;
    color_t skeleton_col = enemy ? g_cfg->visuals.m_enemy_skeleton_color
                                 : g_cfg->visuals.m_friendly_skeleton_color;
    color_t bbox_col     = enemy ? g_cfg->visuals.m_enemy_box_color
                                 : g_cfg->visuals.m_friendly_box_color;
    color_t head_col     = enemy ? color_t{ 255, 100, 100, 220 }
                                 : color_t{ 100, 220, 100, 220 };

    if ( g_cfg->visuals.m_skeleton )
        draw_skeleton( p.m_pawn, skeleton_col );

    if ( g_cfg->visuals.m_box ) {
        float lx = p.m_top.x - p.m_w;
        float ty = p.m_top.y;
        float bx = p.m_top.x + p.m_w;
        float by = p.m_bot.y;
        g_render->rect( (int)lx, (int)ty, (int)( bx - lx ), (int)( by - ty ), bbox_col );
    }

    if ( g_cfg->visuals.m_health_bar ) {
        float lx = p.m_top.x - p.m_w;
        float bar_h = p.m_bot.y - p.m_top.y;
        float filled = bar_h * ( p.m_health / 100.f );
        g_render->rect_filled( (int)lx - 6, (int)p.m_top.y, 4, (int)bar_h,
                               color_t{ 0, 0, 0, 120 } );
        uint8_t r = (uint8_t)( 255.f * ( 1.f - p.m_health / 100.f ) );
        uint8_t g = (uint8_t)( 255.f * ( p.m_health / 100.f ) );
        g_render->rect_filled( (int)lx - 6,
                               (int)( p.m_top.y + bar_h - filled ),
                               4, (int)filled, color_t{ r, g, 0, 220 } );
    }

    if ( g_cfg->visuals.m_name && !p.m_name.empty( ) )
        g_render->text( p.m_name.c_str( ), (int)p.m_top.x, (int)p.m_top.y - 14,
                        true, color_t{ 255, 255, 255, 200 } );

    // NL head dot always on when enemy visuals enabled
    if ( enemy )
        draw_head_dot( p.m_pawn, head_col );
}

void c_visuals::store_players( ) {
    m_players.clear( );
    if ( !g_ctx->m_local_pawn || !g_interfaces->m_engine->is_in_game( ) ) return;

    for ( auto entity : g_entity_system->get( "CCSPlayerController" ) ) {
        auto ctrl = reinterpret_cast<c_cs_player_controller*>( entity );
        if ( !ctrl || ctrl == g_ctx->m_local_controller ) continue;

        auto pawn = reinterpret_cast<c_cs_player_pawn*>(
            g_interfaces->m_entity_system->get_base_entity(
                ctrl->m_pawn( ).get_entry_index( ) ) );
        if ( !pawn || !pawn->is_alive( ) ) continue;

        stored_player_t sp{};
        sp.m_pawn     = pawn;
        sp.m_is_enemy = ( pawn->m_team_num( ) != g_ctx->m_local_pawn->m_team_num( ) );
        sp.m_health   = (float)pawn->m_health( );
        sp.m_valid    = calculate_bbox( pawn, sp.m_top, sp.m_bot, sp.m_w, sp.m_h );
        m_players.push_back( sp );
    }
}

void c_visuals::on_paint( ) {
    if ( !g_cfg->visuals.m_enabled ) return;
    for ( const auto& p : m_players )
        handle_player( p );
}
