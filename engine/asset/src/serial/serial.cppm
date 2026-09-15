export module vw.asset:serial;

// Собирает форматы ассетов: ссылку на ассет, чтение и запись .vox, .voxm,
// .voxa и .voxf, библиотеку объёмов и хранилище ассетов.
export import :serial.ref;
export import :serial.vox;
export import :serial.voxm;
export import :serial.library;
export import :serial.voxa;
export import :serial.voxf;
export import :serial.storage;
