export module vw.asset:model;

// Собирает воксельную модель: идентичность, страницы, занятость граней, связи
// полостей, поле света, сам объём, его правку и чанк, которым он стоит в мире.
export import :model.identity;
export import :model.occupancy;
export import :model.links;
export import :model.light_field;
export import :model.volume;
export import :model.edit;
export import :model.chunk;
