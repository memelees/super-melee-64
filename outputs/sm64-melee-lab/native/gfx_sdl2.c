#ifdef WAPI_SDL2
#ifdef _WIN32
#define GLEW_STATIC
#endif
#include <GL/glew.h>

#ifdef __MINGW32__
#define FOR_WINDOWS 1
#else
#define FOR_WINDOWS 0
#endif

#if FOR_WINDOWS
#define GLEW_STATIC
#include <GL/glew.h>
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengl.h>
#else
#include <SDL2/SDL.h>
#define GL_GLEXT_PROTOTYPES 1

#ifdef OSX_BUILD
#include <SDL2/SDL_opengl.h>
#else
#include <SDL2/SDL_opengles2.h>
#endif

#endif // End of OS-Specific GL defines

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "gfx_window_manager_api.h"
#include "gfx_screen_config.h"
#include "../pc_main.h"
#include "../configfile.h"
#include "../cliopts.h"

#include "src/pc/controller/controller_keyboard.h"

// TODO: figure out if this shit even works
#ifdef VERSION_EU
# define FRAMERATE 25
#else
# define FRAMERATE 30
#endif

/* Windows DPI awareness is fixed once, before the first window exists. The
 * guest (wgpu) otherwise flips the process to per-monitor awareness when it
 * boots, and Windows then rescales the whole window: the picker was 25%
 * larger than the game and it also shifted position. Keeping 96-DPI units in
 * configWindow and multiplying at the SDL boundary leaves the file, the
 * on-screen size and the position identical across the transition. */
void smb_host_key(int code,int down);
static float smb_window_dpi = 1.0f;
static void smb_window_set_size(SDL_Window *window, int w, int h) {
    SDL_SetWindowSize(window, (int)(w * smb_window_dpi + 0.5f), (int)(h * smb_window_dpi + 0.5f));
}
static void smb_window_set_pos(SDL_Window *window, int x, int y) {
    if (x == WAPI_WIN_CENTERPOS && y == WAPI_WIN_CENTERPOS) SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    else SDL_SetWindowPosition(window, (int)(x * smb_window_dpi + 0.5f), (int)(y * smb_window_dpi + 0.5f));
}
static int smb_window_logical(int pixels) { return (int)(pixels / smb_window_dpi + 0.5f); }

#include "../sm64_character_select.inc"
static SDL_Window *wnd;
#include "../sm64_video.inc"
static SDL_GLContext ctx = NULL;
static int inverted_scancode_table[512];

static kb_callback_t kb_key_down = NULL;
static kb_callback_t kb_key_up = NULL;
static void (*kb_all_keys_up)(void) = NULL;

// whether to use timer for frame control
static bool use_timer = true;
// time between consequtive game frames, in perf counter ticks
static double frame_rate = 0.0; // set in init()
// time in which a frame began, in perf counter ticks
static double frame_time = 0.0; // updated in start_frame()
// GetPerformanceFrequency
static double perf_freq = 0.0;

const SDL_Scancode windows_scancode_table[] = {
  /*  0                        1                            2                         3                            4                     5                            6                            7  */
  /*  8                        9                            A                         B                            C                     D                            E                            F  */
  SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_ESCAPE,         SDL_SCANCODE_1,           SDL_SCANCODE_2,              SDL_SCANCODE_3,       SDL_SCANCODE_4,              SDL_SCANCODE_5,              SDL_SCANCODE_6,          /* 0 */
  SDL_SCANCODE_7,              SDL_SCANCODE_8,              SDL_SCANCODE_9,           SDL_SCANCODE_0,              SDL_SCANCODE_MINUS,   SDL_SCANCODE_EQUALS,         SDL_SCANCODE_BACKSPACE,      SDL_SCANCODE_TAB,        /* 0 */

  SDL_SCANCODE_Q,              SDL_SCANCODE_W,              SDL_SCANCODE_E,           SDL_SCANCODE_R,              SDL_SCANCODE_T,       SDL_SCANCODE_Y,              SDL_SCANCODE_U,              SDL_SCANCODE_I,          /* 1 */
  SDL_SCANCODE_O,              SDL_SCANCODE_P,              SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET,   SDL_SCANCODE_RETURN,  SDL_SCANCODE_LCTRL,          SDL_SCANCODE_A,              SDL_SCANCODE_S,          /* 1 */

  SDL_SCANCODE_D,              SDL_SCANCODE_F,              SDL_SCANCODE_G,           SDL_SCANCODE_H,              SDL_SCANCODE_J,       SDL_SCANCODE_K,              SDL_SCANCODE_L,              SDL_SCANCODE_SEMICOLON,  /* 2 */
  SDL_SCANCODE_APOSTROPHE,     SDL_SCANCODE_GRAVE,          SDL_SCANCODE_LSHIFT,      SDL_SCANCODE_BACKSLASH,      SDL_SCANCODE_Z,       SDL_SCANCODE_X,              SDL_SCANCODE_C,              SDL_SCANCODE_V,          /* 2 */

  SDL_SCANCODE_B,              SDL_SCANCODE_N,              SDL_SCANCODE_M,           SDL_SCANCODE_COMMA,          SDL_SCANCODE_PERIOD,  SDL_SCANCODE_SLASH,          SDL_SCANCODE_RSHIFT,         SDL_SCANCODE_PRINTSCREEN,/* 3 */
  SDL_SCANCODE_LALT,           SDL_SCANCODE_SPACE,          SDL_SCANCODE_CAPSLOCK,    SDL_SCANCODE_F1,             SDL_SCANCODE_F2,      SDL_SCANCODE_F3,             SDL_SCANCODE_F4,             SDL_SCANCODE_F5,         /* 3 */

  SDL_SCANCODE_F6,             SDL_SCANCODE_F7,             SDL_SCANCODE_F8,          SDL_SCANCODE_F9,             SDL_SCANCODE_F10,     SDL_SCANCODE_NUMLOCKCLEAR,   SDL_SCANCODE_SCROLLLOCK,     SDL_SCANCODE_HOME,       /* 4 */
  SDL_SCANCODE_UP,             SDL_SCANCODE_PAGEUP,         SDL_SCANCODE_KP_MINUS,    SDL_SCANCODE_LEFT,           SDL_SCANCODE_KP_5,    SDL_SCANCODE_RIGHT,          SDL_SCANCODE_KP_PLUS,        SDL_SCANCODE_END,        /* 4 */

  SDL_SCANCODE_DOWN,           SDL_SCANCODE_PAGEDOWN,       SDL_SCANCODE_INSERT,      SDL_SCANCODE_DELETE,         SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_NONUSBACKSLASH, SDL_SCANCODE_F11,        /* 5 */
  SDL_SCANCODE_F12,            SDL_SCANCODE_PAUSE,          SDL_SCANCODE_UNKNOWN,     SDL_SCANCODE_LGUI,           SDL_SCANCODE_RGUI,    SDL_SCANCODE_APPLICATION,    SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,    /* 5 */

  SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,     SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_F13,     SDL_SCANCODE_F14,            SDL_SCANCODE_F15,            SDL_SCANCODE_F16,        /* 6 */
  SDL_SCANCODE_F17,            SDL_SCANCODE_F18,            SDL_SCANCODE_F19,         SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,    /* 6 */

  SDL_SCANCODE_INTERNATIONAL2, SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,     SDL_SCANCODE_INTERNATIONAL1, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN,    /* 7 */
  SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_INTERNATIONAL4, SDL_SCANCODE_UNKNOWN,     SDL_SCANCODE_INTERNATIONAL5, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_INTERNATIONAL3, SDL_SCANCODE_UNKNOWN,        SDL_SCANCODE_UNKNOWN     /* 7 */
};

const SDL_Scancode scancode_rmapping_extended[][2] = {
    {SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_RETURN},
    {SDL_SCANCODE_RALT, SDL_SCANCODE_LALT},
    {SDL_SCANCODE_RCTRL, SDL_SCANCODE_LCTRL},
    {SDL_SCANCODE_KP_DIVIDE, SDL_SCANCODE_SLASH},
    //{SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_CAPSLOCK}
};

const SDL_Scancode scancode_rmapping_nonextended[][2] = {
    {SDL_SCANCODE_KP_7, SDL_SCANCODE_HOME},
    {SDL_SCANCODE_KP_8, SDL_SCANCODE_UP},
    {SDL_SCANCODE_KP_9, SDL_SCANCODE_PAGEUP},
    {SDL_SCANCODE_KP_4, SDL_SCANCODE_LEFT},
    {SDL_SCANCODE_KP_6, SDL_SCANCODE_RIGHT},
    {SDL_SCANCODE_KP_1, SDL_SCANCODE_END},
    {SDL_SCANCODE_KP_2, SDL_SCANCODE_DOWN},
    {SDL_SCANCODE_KP_3, SDL_SCANCODE_PAGEDOWN},
    {SDL_SCANCODE_KP_0, SDL_SCANCODE_INSERT},
    {SDL_SCANCODE_KP_PERIOD, SDL_SCANCODE_DELETE},
    {SDL_SCANCODE_KP_MULTIPLY, SDL_SCANCODE_PRINTSCREEN}
};

#define IS_FULLSCREEN() ((SDL_GetWindowFlags(wnd) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)

static void gfx_sdl_set_fullscreen(void) {
    if (configWindow.reset)
        configWindow.fullscreen = false;
    if (configWindow.fullscreen == IS_FULLSCREEN())
        return;
    if (configWindow.fullscreen) {
        SDL_SetWindowFullscreen(wnd, SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_ShowCursor(SDL_DISABLE);
    } else {
        SDL_SetWindowFullscreen(wnd, 0);
        SDL_ShowCursor(SDL_ENABLE);
        configWindow.exiting_fullscreen = true;
    }
}

static void smb_window_pin(void);
static void gfx_sdl_reset_dimension_and_pos(void) {
    if (configWindow.exiting_fullscreen)
        configWindow.exiting_fullscreen = false;

    if (configWindow.reset) {
        configWindow.x = WAPI_WIN_CENTERPOS;
        configWindow.y = WAPI_WIN_CENTERPOS;
        configWindow.w = DESIRED_SCREEN_WIDTH;
        configWindow.h = DESIRED_SCREEN_HEIGHT;
        configWindow.reset = false;
    } else if (!configWindow.settings_changed) {
        return;
    }

    smb_window_set_size(wnd, configWindow.w, configWindow.h);
    smb_window_set_pos(wnd, configWindow.x, configWindow.y);
    // in case vsync changed
    SDL_GL_SetSwapInterval(configWindow.vsync);
}

static void smb_window_trace(const char* tag) {
    if(!getenv("SMB_WINDOW_TRACE")||!wnd)return;
    int w,h,pw,ph,px,py;SDL_GetWindowSize(wnd,&w,&h);SDL_GetWindowSizeInPixels(wnd,&pw,&ph);SDL_GetWindowPosition(wnd,&px,&py);
    float dpi=96.f;SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(wnd),&dpi,NULL,NULL);
    fprintf(stderr,"SMB WINDOW tag=%s size=%dx%d pixels=%dx%d pos=%d,%d dpi=%.1f cfg=%dx%d@%d,%d flags=%x\n",tag,w,h,pw,ph,px,py,dpi,configWindow.w,configWindow.h,configWindow.x,configWindow.y,(unsigned)SDL_GetWindowFlags(wnd));
}
static void gfx_sdl_init(const char *window_title) {
    /* Claim the final awareness before any window exists; later requests are
     * ignored by Windows, so nothing can rescale the window mid-run. */
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    SDL_Init(SDL_INIT_VIDEO);
    {
        float ddpi = 96.0f;
        if (SDL_GetDisplayDPI(0, &ddpi, NULL, NULL) == 0 && ddpi > 72.0f) smb_window_dpi = ddpi / 96.0f;
        fprintf(stderr, "SMB WINDOW dpi=%.2f logical=%dx%d@%d,%d\n", smb_window_dpi, configWindow.w, configWindow.h, configWindow.x, configWindow.y);
    }

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    #ifdef USE_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);  // These attributes allow for hardware acceleration on RPis.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    #endif

    //SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    //SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    int xpos = (configWindow.x == WAPI_WIN_CENTERPOS) ? SDL_WINDOWPOS_CENTERED : (int)(configWindow.x * smb_window_dpi + 0.5f);
    int ypos = (configWindow.y == WAPI_WIN_CENTERPOS) ? SDL_WINDOWPOS_CENTERED : (int)(configWindow.y * smb_window_dpi + 0.5f);

    wnd = SDL_CreateWindow(
        (getenv("SM64_MELEE_SOCKET") || getenv("SM64_MELEE_LIBRARY")) ? "Super Melee 64" : window_title,
        xpos, ypos, (int)(configWindow.w * smb_window_dpi + 0.5f), (int)(configWindow.h * smb_window_dpi + 0.5f),
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | (getenv("SM64_MELEE_LIBRARY") ? SDL_WINDOW_ALLOW_HIGHDPI : 0)
    );
    ctx = SDL_GL_CreateContext(wnd);
    if (getenv("SM64_MELEE_LIBRARY")) {SDL_RaiseWindow(wnd);SDL_SetWindowInputFocus(wnd);}
    smb_window_trace("created");
    if (getenv("SM64_MELEE_LIBRARY")) {glewInit();smb_host_character_select(wnd);smb_host_boot();smb_window_trace("booted");SDL_GL_MakeCurrent(wnd,ctx);SDL_RaiseWindow(wnd);SDL_SetWindowInputFocus(wnd);}

    SDL_GL_SetSwapInterval(configWindow.vsync);

    gfx_sdl_set_fullscreen();

    perf_freq = SDL_GetPerformanceFrequency();

    frame_rate = perf_freq / FRAMERATE;
    frame_time = SDL_GetPerformanceCounter();

    for (size_t i = 0; i < sizeof(windows_scancode_table) / sizeof(SDL_Scancode); i++) {
        inverted_scancode_table[windows_scancode_table[i]] = i;
    }

    for (size_t i = 0; i < sizeof(scancode_rmapping_extended) / sizeof(scancode_rmapping_extended[0]); i++) {
        inverted_scancode_table[scancode_rmapping_extended[i][0]] = inverted_scancode_table[scancode_rmapping_extended[i][1]] + 0x100;
    }

    for (size_t i = 0; i < sizeof(scancode_rmapping_nonextended) / sizeof(scancode_rmapping_nonextended[0]); i++) {
        inverted_scancode_table[scancode_rmapping_nonextended[i][0]] = inverted_scancode_table[scancode_rmapping_nonextended[i][1]];
        inverted_scancode_table[scancode_rmapping_nonextended[i][1]] += 0x100;
    }
}

static void gfx_sdl_main_loop(void (*run_one_game_iter)(void)) {
    smb_window_trace("frame");
    smb_video_begin=SDL_GetPerformanceCounter();
    run_one_game_iter();
    int picker_action=smb_host_change_menu_ready();
    if(picker_action) {
        smb_host_select(wnd,picker_action);
        smb_host_key(-1,0);
        if(kb_all_keys_up)kb_all_keys_up();
        smb_host_change_menu_closed();
        frame_time=SDL_GetPerformanceCounter();
    }
    if(smb_settings_menu_requested()) {
        smb_host_settings_menu(wnd);
        smb_host_settings_closed();
        /* The panel runs its own event loop, so the release of the key that
         * closed it is consumed there; drop every latched key by hand. */
        smb_host_key(-1,0);
        if(kb_all_keys_up)kb_all_keys_up();
        frame_time=SDL_GetPerformanceCounter();
    }
}

static void gfx_sdl_get_dimensions(uint32_t *width, uint32_t *height) {
    int w, h;
    if(getenv("SM64_MELEE_LIBRARY"))smb_video_dimensions(wnd, &w, &h);else SDL_GetWindowSize(wnd, &w, &h);
    if (width) *width = w;
    if (height) *height = h;
}

static int translate_scancode(int scancode) {
    if (scancode < 512) {
        return inverted_scancode_table[scancode];
    } else {
        return 0;
    }
}

void smb_host_key(int,int);
static void gfx_sdl_onkeydown(int scancode) {
    smb_host_key(scancode,1);
    if (kb_key_down)
        kb_key_down(translate_scancode(scancode));

    const Uint8 *state = SDL_GetKeyboardState(NULL);

    if (state[SDL_SCANCODE_LALT] && state[SDL_SCANCODE_RETURN]) {
        configWindow.fullscreen = !configWindow.fullscreen;
        configWindow.settings_changed = true;
    }
}

static void gfx_sdl_onkeyup(int scancode) {
    smb_host_key(scancode,0);
    if (kb_key_up)
        kb_key_up(translate_scancode(scancode));
}

static void gfx_sdl_handle_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
#ifndef TARGET_WEB
            // Scancodes are broken in Emscripten SDL2: https://bugzilla.libsdl.org/show_bug.cgi?id=3259
            case SDL_KEYDOWN:
                gfx_sdl_onkeydown(event.key.keysym.scancode);
                break;
            case SDL_KEYUP:
                gfx_sdl_onkeyup(event.key.keysym.scancode);
                break;
#endif
            case SDL_WINDOWEVENT: // TODO: Check if this makes sense to be included in the Web build
                smb_window_trace(event.window.event==SDL_WINDOWEVENT_SIZE_CHANGED?"size":event.window.event==SDL_WINDOWEVENT_MOVED?"moved":"window");
                if (!IS_FULLSCREEN()) {
                    switch (event.window.event) {
                        case SDL_WINDOWEVENT_MOVED:
                            if (!configWindow.exiting_fullscreen) {
                                if (event.window.data1 >= 0) configWindow.x = smb_window_logical(event.window.data1);
                                if (event.window.data2 >= 0) configWindow.y = smb_window_logical(event.window.data2);
                            }
                            break;
                        case SDL_WINDOWEVENT_SIZE_CHANGED:
                            configWindow.w = smb_window_logical(event.window.data1);
                            configWindow.h = smb_window_logical(event.window.data2);
                            break;
                    }
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                if(event.cbutton.button==SDL_CONTROLLER_BUTTON_BACK)smb_host_change_menu_request();
                break;
            case SDL_QUIT:
                game_exit();
                break;
        }
    }

    if (configWindow.settings_changed) {
        gfx_sdl_set_fullscreen();
        gfx_sdl_reset_dimension_and_pos();
        configWindow.settings_changed = false;
    }
}

static void gfx_sdl_set_keyboard_callbacks(kb_callback_t on_key_down, kb_callback_t on_key_up, void (*on_all_keys_up)(void)) {
    kb_key_down = on_key_down;
    kb_key_up = on_key_up;
    kb_all_keys_up = on_all_keys_up;
}

static bool gfx_sdl_start_frame(void) {
    smb_video_start(wnd);
    return true;
}

static inline void sync_framerate_with_timer(void) {
    if(getenv("SM64_MELEE_LIBRARY")) { /* SMB precise presentation deadline */
        double deadline=frame_time+frame_rate,now=SDL_GetPerformanceCounter();
        double microseconds=(deadline-now)/perf_freq*1000000.0;
        if(microseconds>1200)sys_sleep(microseconds-1000);
        while((now=SDL_GetPerformanceCounter())<deadline) {}
        frame_time=microseconds>0?deadline:now;
        return;
    }
    // calculate how long it took for the frame to render
    const double now = SDL_GetPerformanceCounter();
    const double frame_length = now - frame_time;

    if (frame_length < frame_rate) {
        // Only sleep if we have time to spare
        const double remain = frame_rate - frame_length;
        // Sleep remaining time away
        sys_sleep(remain / perf_freq * 1000000.0);
        // Assume we slept the required amount of time to keep the timer stable
        frame_time = now + remain;
    } else {
        frame_time = now;
    }
}

static void gfx_sdl_swap_buffers_begin(void) {
    // Swap after we finished rendering, only if this frame wasn't dropped.
    // This will wait for vblank if vsync is enabled and then update our window with our render.
    smb_video_present(wnd);
    SDL_GL_SwapWindow(wnd);
}

static void gfx_sdl_swap_buffers_end(void) {
    // The game isn't always going to run at a consistent rate,
    // with frame pacing going up and down depending on hardware performance.
    // Sleep off any remaining time to make the main loop iteration be called at a consistent frane rate.
    // We do this after our swap, because it actually will take the time to swap into account.
    sync_framerate_with_timer();
}

static double gfx_sdl_get_time(void) {
    return 0.0;
}


static void gfx_sdl_shutdown(void) {
    if (SDL_WasInit(0)) {
        if (ctx) { SDL_GL_DeleteContext(ctx); ctx = NULL; }
        if (wnd) { SDL_DestroyWindow(wnd); wnd = NULL; }
        SDL_Quit();
    }
}

struct GfxWindowManagerAPI gfx_sdl = {
    gfx_sdl_init,
    gfx_sdl_set_keyboard_callbacks,
    gfx_sdl_main_loop,
    gfx_sdl_get_dimensions,
    gfx_sdl_handle_events,
    gfx_sdl_start_frame,
    gfx_sdl_swap_buffers_begin,
    gfx_sdl_swap_buffers_end,
    gfx_sdl_get_time,
    gfx_sdl_shutdown
};

#endif // BACKEND_WM
