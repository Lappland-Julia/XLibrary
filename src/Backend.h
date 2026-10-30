#ifndef XLIBRARY_BACKEND_H
#define XLIBRARY_BACKEND_H

#pragma once

// Совместимый общий заголовок прежнего API. GUI работает через CatalogStore и
// desktop_platform; этот заголовок не задаёт отдельные правила каталога или запуска.

#include "Application.h"
#include "Database.h"
#include "Heartbeat.h"
#include "Manager.h"
#ifdef _WIN32
#include "Windows/RegistryScan.h"
#endif
#include "WindowManager.h"

#endif //XLIBRARY_BACKEND_H
