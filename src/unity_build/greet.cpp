#include <SDL3/SDL.h>

#include <array>
#include <gsl/gsl>

namespace unity_test::detail {

/// Show a two-button message box and return the chosen button id.
/// @pre  SDL video subsystem is initialized.
/// @return button id (0 = OK, 1 = Exit), or -1 on failure.
[[nodiscard]] int show_greeting()
{
    constexpr std::array buttons{
        SDL_MessageBoxButtonData{ .flags =
                                      SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,
                                  .buttonID = 0,
                                  .text     = "OK" },
        SDL_MessageBoxButtonData{ .flags =
                                      SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
                                  .buttonID = 1,
                                  .text     = "Exit" },
    };

    const SDL_MessageBoxData box{
        .flags       = SDL_MESSAGEBOX_INFORMATION,
        .window      = nullptr,
        .title       = "Unity Build Test",
        .message     = "If you see this, unity build works.",
        .numbuttons  = gsl::narrow_cast<int>(buttons.size()),
        .buttons     = buttons.data(),
        .colorScheme = nullptr,
    };

    int button_id{ -1 };
    if (!SDL_ShowMessageBox(&box, &button_id)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_ShowMessageBox failed: %s",
                     SDL_GetError());
        return -1;
    }
    return button_id;
}

} // namespace unity_test::detail
