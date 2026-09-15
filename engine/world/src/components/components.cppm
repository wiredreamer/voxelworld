export module vw.world:components;

// Собирает компоненты мира. Поля закрыты, менять их вправе только дружественная
// система — правка идёт через её modifier, который поднимает нужные флаги.
export import :components.transform;
export import :components.model;
export import :components.spatial;
export import :components.light;
export import :components.physics;
export import :components.socket;
export import :components.animation;
export import :components.variant;
export import :components.world_view;
