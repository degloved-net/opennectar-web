#pragma once

#include <functional>

#ifdef __EMSCRIPTEN__
void pc_main_loop_run(std::function<bool()> tick, std::function<void()> stop);
#else
inline void pc_main_loop_run(std::function<bool()> tick, std::function<void()> stop)
{
    while (tick()) {}
    stop();
}
#endif
