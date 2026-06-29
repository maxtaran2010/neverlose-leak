#pragma once
#include "darkside.hpp"

// NL no-spread reconstruction
// "ts_accuracy_set_m" @ 0x1a9371a6758 — zeros m_fAccuracyPenalty
// "ts_accuracy_set_s" @ 0x1a9371a8438 — zeros random_seed

class c_no_spread {
public:
    void on_create_move( c_user_cmd* user_cmd );
};

inline const auto g_no_spread = std::make_unique<c_no_spread>( );
