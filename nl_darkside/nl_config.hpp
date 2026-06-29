#pragma once

// NL-specific config overlay — lives next to darkside's g_cfg
// Hook these up to imgui menu when ready

struct nl_config_t {
    struct {
        bool  m_resolver        = true;
        bool  m_spread_correct  = true;
        bool  m_strided_bt      = true;   // stride across ring buffer candidates
        int   m_wrap_mode       = 2;      // 0=clamp, 1=ping-pong, 2=circular (default MM)
        float m_multipoint_base = 4.f;    // NL base radius, scaled by 1/sqrt(dist+1)
        int   m_bt_max_cands    = 8;      // how many ring slots to evaluate
    } rage_bot;

    struct {
        int   m_pitch_mode     = 0;       // 0=down(-89), 1=up(+89), 2=zero
        int   m_yaw_mode       = 0;       // 0=jitter, 1=spin, 2=static, 3=lby_flip
        float m_static_yaw     = 180.f;   // offset added to real yaw (mode 2)
        float m_spin_speed     = 360.f;   // degrees/second (mode 1)
        bool  m_desync         = true;
        float m_desync_amount  = 58.f;    // flip distance in degrees
    } anti_hit;

    struct {
        bool m_zero_penalty = true;       // set m_fAccuracyPenalty = 0
        bool m_zero_seed    = true;       // set random_seed = 0
    } no_spread;
} inline nl_cfg{};
