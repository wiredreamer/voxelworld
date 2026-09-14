export module vw.asset:serial;

// Собирает форматы ассетов: ссылку на ассет, разбор .vox, чтение и запись .voxm
// и .voxa, библиотеку объёмов и хранилище ассетов.
export import :serial.ref;
export import :serial.vox;
export import :serial.voxm;
export import :serial.library;
export import :serial.voxa;
export import :serial.storage;
