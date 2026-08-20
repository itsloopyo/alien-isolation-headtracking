#pragma once

namespace camera {

// The device context's vtable, from a throwaway device of our own: the game's
// context is not reachable from a static initialiser, and every D3D11 context
// shares the layout, so a context we make ourselves names the same functions.
bool GetContextVTable(void**& vtable);

}  // namespace camera
