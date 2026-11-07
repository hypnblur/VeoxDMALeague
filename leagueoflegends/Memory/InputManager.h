#pragma once
#include <cstdint>

namespace Keyboard {

bool InitKeyboard();
void UpdateKeys();
bool IsKeyDown(uint32_t virtual_key_code);
bool IsReady();

}
