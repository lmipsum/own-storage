// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/Config.h"
#include "json/Json.h"

#include <string>

extern const std::string localResourceFile;
extern Config config;
extern Json localResourceRoot;

bool pathExists(std::string pathToCheck);
void buildLocalResourceTree();
