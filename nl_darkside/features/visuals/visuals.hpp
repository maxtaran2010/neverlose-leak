#pragma once

#include "darkside.hpp"

struct bbox_t {
	bool m_found = false;

	float x;
	float y;
	float width;
	float height;
};

class c_visuals {
	struct player_info_t {
		bool m_valid = false;

		int m_handle;
		int m_health;
		int m_ammo;
		int m_max_ammo;
		int m_armor;
		int m_money;

		bbox_t m_bbox;
		std::array<vec3_t, 28> m_bone_positions;

		std::string m_name;
		std::string m_weapon_name;

		bool m_has_helmet;
		bool m_has_defuser;
		bool m_is_scoped;
	};

	bbox_t calculate_bbox( c_cs_player_pawn* entity );

	std::mutex m_player_mutex;
	std::unordered_map<int, player_info_t> m_player_map;

	void handle_players( );
public:
	void store_players( );
	void on_present( );
};

inline const auto g_visuals = std::make_unique<c_visuals>( );
