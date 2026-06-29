#pragma once
#include "../../darkside.hpp"

// Visuals: darkside skeleton + bbox unchanged, added NL head dot
// NL head dot = small circle drawn at HITBOX_HEAD world-to-screen projection

struct stored_player_t {
    c_cs_player_pawn* m_pawn{};
    bool m_is_enemy{};
    float m_health{};
    std::string m_name{};
    vec3_t m_origin{};
    // bbox corners (screen)
    vec3_t m_top{}, m_bot{};
    float m_w{}, m_h{};
    bool m_valid{};
};

class c_visuals {
    std::vector<stored_player_t> m_players{};

    bool calculate_bbox( c_cs_player_pawn* pawn, vec3_t& top, vec3_t& bot,
                         float& w, float& h );
    void draw_skeleton( c_cs_player_pawn* pawn, color_t col );
    void draw_head_dot( c_cs_player_pawn* pawn, color_t col );
    void handle_player( const stored_player_t& p );

public:
    void store_players( );
    void on_paint( );
};

inline const auto g_visuals = std::make_unique<c_visuals>( );
