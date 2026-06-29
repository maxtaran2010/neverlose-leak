#pragma once
#include "../../darkside.hpp"

// NL anti-aim reconstruction — sub_1A936EDA520 (1369 bytes, 0 callers)
// Identified by: heavy XMM register use, only unique callee sub_1A936DFB0B0 (jitter LUT),
// floating-point angle manipulation pattern, and NL feature magic 0x101 at +24.
//
// Jitter LUT (sub_1A936DFB0B0, 19 bytes):
//   new_index = (index + (int8_t)table[index]) & (count-1)
//   = delta-encoded circular table advance.
//
// Desync: input_history[0].view_angles = real yaw (server sees)
//         input_history[1+].view_angles = fake yaw (what client shows)

struct nl_jitter_entry_t {
    float yaw;    // absolute yaw offset for this LUT step
    int8_t delta; // signed delta to next index
};

class c_anti_hit {
    // Jitter LUT — sampled from sub_1A936DFB0B0 behavioural analysis.
    // Replace with real values from IDA once you dump them from the .data segment.
    static constexpr nl_jitter_entry_t JITTER_LUT[8] = {
        {  0.f,  2 }, { 58.f,  2 }, { -58.f, 2 }, { 29.f,  2 },
        { -29.f, 2 }, { 89.f,  1 }, { -89.f, -7 }, { 0.f, 1 }
    };
    int m_jitter_idx   = 0;
    float m_spin_accum = 0.f;

    // Advance LUT index: index = (index + delta) & (count-1)
    // Mirrors sub_1A936DFB0B0 (19 bytes exactly)
    void jitter_advance( ) {
        m_jitter_idx = ( m_jitter_idx
            + static_cast<int>( JITTER_LUT[m_jitter_idx].delta ) )
            & 7;
    }

    void apply_pitch( c_user_cmd* user_cmd );
    void apply_yaw( c_user_cmd* user_cmd );
    void apply_desync( c_user_cmd* user_cmd, float real_yaw, float fake_yaw );

public:
    void on_create_move( c_user_cmd* user_cmd );
};

inline const auto g_anti_hit = std::make_unique<c_anti_hit>( );
