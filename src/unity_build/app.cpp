#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <new>

// ── Forward-declare from greet.cpp ───────────────────────────
// In a unity build both TUs are merged, so this resolves either way.
namespace unity_test::detail {
[[nodiscard]] int show_greeting();
} // namespace unity_test::detail

struct app_state final
{
    bool done{ false };
};

SDL_AppResult SDL_AppInit(void**                 appstate,
                          [[maybe_unused]] int   argc,
                          [[maybe_unused]] char* argv[])
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_Init failed: %s",
                     SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto* state = new (std::nothrow) app_state{};
    if (!state) {
        return SDL_APP_FAILURE;
    }
    *appstate = state;

    const int id{ unity_test::detail::show_greeting() };
    SDL_Log("button_id == %d", id);

    state->done = true;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    return static_cast<const app_state*>(appstate)->done ? SDL_APP_SUCCESS
                                                         : SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* /*appstate*/, SDL_Event* event)
{
    return (event->type == SDL_EVENT_QUIT) ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, [[maybe_unused]] SDL_AppResult result)
{
    delete static_cast<app_state*>(appstate);
}
