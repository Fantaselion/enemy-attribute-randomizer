#pragma once

#include "mods/api.h"

namespace enemy_patches {

ModResult initialize_all(ModError* error);
void update_all();
void shutdown_all();

}  // namespace enemy_patches
