export module vw.world:systems;

// Собирает системы мира: каждая живёт в своей партиции и правит только те
// компоненты, с которыми дружит.
export import :systems.hooks;
export import :systems.hierarchy;
export import :systems.transform;
export import :systems.model;
export import :systems.light;
export import :systems.spatial;
export import :systems.physics;
export import :systems.character_controller;
export import :systems.socket;
export import :systems.animation;
export import :systems.animation_fsm;
export import :systems.variant;
export import :systems.world_grid;
