set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

cpmaddpackage(
    NAME
    SDL3_mixer
    GITHUB_REPOSITORY
    libsdl-org/SDL_mixer
    GIT_TAG
    main
    SYSTEM
    ON
    GIT_SHALLOW
    ON
    EXCLUDE_FROM_ALL
    ON
    OPTIONS
    "BUILD_SHARED_LIBS OFF"
    "SDLMIXER_DEPS_SHARED OFF" # Disable dynamic loading of deps
    "SDLMIXER_VENDORED ON" # Prefer vendored code (ensure STB usage)
    # Allowed Formats
    "SDLMIXER_WAVE ON" # .wav
    "SDLMIXER_VORBIS_STB ON" # .ogg (minimal, header-only)
    # Blocked OGG variants (too complex)
    "SDLMIXER_VORBIS_VORBISFILE OFF" # Requires libvorbis
    "SDLMIXER_VORBIS_TREMOR OFF"
    # Blocked Formats
    "SDLMIXER_AIFF OFF"
    "SDLMIXER_VOC OFF"
    "SDLMIXER_AU OFF"
    "SDLMIXER_FLAC_LIBFLAC OFF"
    "SDLMIXER_FLAC_DRFLAC OFF"
    "SDLMIXER_GME OFF" # Game Music Emu
    "SDLMIXER_MOD_XMP OFF" # Tracker formats
    "SDLMIXER_MP3_DRMP3 OFF"
    "SDLMIXER_MP3_MPG123 OFF"
    "SDLMIXER_MIDI_FLUIDSYNTH OFF"
    "SDLMIXER_MIDI_TIMIDITY OFF"
    "SDLMIXER_OPUS OFF"
    "SDLMIXER_WAVPACK OFF"
    # Bloat removal
    "SDLMIXER_SAMPLES OFF"
    "SDLMIXER_TESTS OFF")
