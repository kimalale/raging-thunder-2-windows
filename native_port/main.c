#include "elf_loader.h"
#include "fake_jni.h"
#include "gl_wrap.h"
#include "shims.h"
#include "audio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

#define WINDOW_WIDTH  1024
#define WINDOW_HEIGHT 600

/* Android Keycodes used by Polarbit Fuse */
#define KEY_BACK        4
#define KEY_DPAD_UP     19
#define KEY_DPAD_DOWN   20
#define KEY_DPAD_LEFT   21
#define KEY_DPAD_RIGHT  22
#define KEY_DPAD_CENTER 23
#define KEY_BUTTON_X    99
#define KEY_BUTTON_Y    100
#define KEY_BUTTON_L1   102
#define KEY_BUTTON_R1   103

/* JNI function signatures */
typedef jint (*JNI_OnLoad_fn)(JavaVM *vm, void *reserved);
typedef void (*FuseOnInit_fn)(JNIEnv *, jclass, jobject, jobject, jobject, jobject, jobject, jobject);
typedef jint (*OnCreate_fn)(JNIEnv *, jclass, jstring, jstring);
typedef jint (*OnEvent_fn)(JNIEnv *, jclass, jint, jint, jint, jint, jint);
typedef jint (*OnEventMessage_fn)(JNIEnv *, jclass, jint, jint, jint, jint, jstring);
typedef void (*OnDestroy_fn)(JNIEnv *, jclass);

static SDL_Window *g_window = NULL;
static SDL_GLContext g_gl_ctx = NULL;

static void swap_buffers_cb(void) {
    if (g_window) {
        SDL_GL_SwapWindow(g_window);
    }
}

static LONG WINAPI crash_handler(EXCEPTION_POINTERS *ep) {
    /* Only catch fatal error exceptions (severity 0xC0000000, like Access Violation 0xC0000005) */
    if ((ep->ExceptionRecord->ExceptionCode & 0xC0000000) != 0xC0000000) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    fprintf(stderr, "\n[CRASH] Exception Code: 0x%08lX at Address: %p (EIP: 0x%08lX)\n",
            ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress,
            ep->ContextRecord->Eip);
    fprintf(stderr, "EAX: 0x%08lX  EBX: 0x%08lX  ECX: 0x%08lX  EDX: 0x%08lX\n",
            ep->ContextRecord->Eax, ep->ContextRecord->Ebx,
            ep->ContextRecord->Ecx, ep->ContextRecord->Edx);
    fprintf(stderr, "ESI: 0x%08lX  EDI: 0x%08lX  EBP: 0x%08lX  ESP: 0x%08lX\n",
            ep->ContextRecord->Esi, ep->ContextRecord->Edi,
            ep->ContextRecord->Ebp, ep->ContextRecord->Esp);
    uint32_t *stack = (uint32_t *)ep->ContextRecord->Esp;
    fprintf(stderr, "Call stack:\n");
    for (int i = 0; i < 16; i++) {
        fprintf(stderr, "  [%02d] 0x%08lX\n", i, stack[i]);
    }
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    AddVectoredExceptionHandler(1, crash_handler);
    
    printf("====================================================\n");
    printf("   Raging Thunder 2 — Native Windows Recomp/Port    \n");
    printf("====================================================\n\n");
    
    /* Ensure saves folder exists */
    CreateDirectoryA("saves", NULL);
    
    /* Initialize SDL2 */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "Failed to initialize SDL2: %s\n", SDL_GetError());
        return 1;
    }
    
    SDL_GameController *controller = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) {
                printf("[INPUT] Connected Game Controller: %s\n", SDL_GameControllerName(controller));
                break;
            }
        }
    }
    
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    
    g_window = SDL_CreateWindow(
        "Raging Thunder 2",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!g_window) {
        fprintf(stderr, "Failed to create SDL window: %s\n", SDL_GetError());
        return 1;
    }
    
    g_gl_ctx = SDL_GL_CreateContext(g_window);
    if (!g_gl_ctx) {
        fprintf(stderr, "Failed to create OpenGL context: %s\n", SDL_GetError());
        return 1;
    }
    
    SDL_GL_SetSwapInterval(1); /* VSync */
    
    /* Initialize subsystems */
    gl_wrap_init();
    fake_jni_init(swap_buffers_cb);
    shims_init();
    
    /* Load the original x86 ELF shared object */
    elf_module_t mod;
    const char *so_path = "apk/lib/x86/librthunder2lite.so";
    if (elf_load(so_path, &mod) != 0) {
        fprintf(stderr, "Failed to load %s\n", so_path);
        return 1;
    }
    
    size_t host_sym_count = 0;
    const host_symbol_t *host_syms = shims_get_table(&host_sym_count);
    if (elf_relocate(&mod, host_syms, host_sym_count) != 0) {
        fprintf(stderr, "Failed to relocate %s\n", so_path);
        return 1;
    }
    
    elf_init(&mod);
    
    /* Resolve native JNI symbols */
    JNI_OnLoad_fn JNI_OnLoad = (JNI_OnLoad_fn)elf_sym(&mod, "JNI_OnLoad");
    FuseOnInit_fn FuseOnInit = (FuseOnInit_fn)elf_sym(&mod, "Java_com_polarbit_fuse_MainTask_FuseOnInit");
    OnCreate_fn   OnCreate   = (OnCreate_fn)elf_sym(&mod, "Java_com_polarbit_fuse_Jni_OnCreate");
    OnEvent_fn    OnEvent    = (OnEvent_fn)elf_sym(&mod, "Java_com_polarbit_fuse_Jni_OnEvent");
    OnDestroy_fn  OnDestroy  = (OnDestroy_fn)elf_sym(&mod, "Java_com_polarbit_fuse_Jni_OnDestroy");
    AudioMix_fn   AudioMix   = (AudioMix_fn)elf_sym(&mod, "Java_com_polarbit_fuse_Jni_AudioMix");
    
    if (!OnEvent || !OnCreate || !FuseOnInit) {
        fprintf(stderr, "Error: Required engine functions not found in library!\n");
        return 1;
    }
    
    if (JNI_OnLoad) {
        printf("[MAIN] Calling JNI_OnLoad...\n");
        JNI_OnLoad(&fake_jvm, NULL);
    }
    
    if (AudioMix) {
        audio_init(AudioMix);
    }
    
    /* Engine lifecycle initialization */
    static int activity_placeholder;
    static int maintask_placeholder;
    static int sensor_placeholder;
    static int utils_placeholder;
    static int audio_placeholder;
    static int egl_placeholder;
    
    printf("[MAIN] Bootstrapping Polarbit Fuse engine...\n");
    OnEvent(&fake_jni, NULL, 1, 1, 0, 0, 0x10000);
    OnEvent(&fake_jni, NULL, 4, 0, 0, 0, 0);
    
    FuseOnInit(&fake_jni, NULL,
               (jobject)&activity_placeholder, (jobject)&maintask_placeholder,
               (jobject)&sensor_placeholder,   (jobject)&utils_placeholder,
               (jobject)&audio_placeholder,    (jobject)&egl_placeholder);
    
    /* Set surface dimensions */
    OnEvent(&fake_jni, NULL, 3, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    
    /* Call OnCreate with path to APK and save directory */
    const char *apk_path = "raging-thunder-21-0-16.apk";
    const char *save_dir = "saves/";
    jstring j_apk = fake_jni->NewStringUTF(&fake_jni, apk_path);
    jstring j_sav = fake_jni->NewStringUTF(&fake_jni, save_dir);
    
    printf("[MAIN] Calling OnCreate(\"%s\", \"%s\")...\n", apk_path, save_dir);
    if (!OnCreate(&fake_jni, NULL, j_apk, j_sav)) {
        fprintf(stderr, "Engine OnCreate returned failure!\n");
        return 1;
    }
    printf("[MAIN] OnCreate succeeded!\n");
    
    /* Activate physical keypad layout (Xperia Play mode) */
    OnEvent(&fake_jni, NULL, 1, 4, 1, 0, 0);
    
    audio_start();
    
    printf("\n>>> GAME STARTED RUNNING NATIVELY ON WINDOWS <<<\n");
    printf("Controls:\n");
    printf("  [ENTER] / [SPACE] / [X] / Gamepad A : Accelerate / Select\n");
    printf("  [DOWN]  / [S]     / [Z] / Gamepad B : Brake / Reverse\n");
    printf("  [UP]    / [W]     / [C] / Gamepad Y : Nitro\n");
    printf("  [LEFT]  / [A]           / Stick Left: Steer Left\n");
    printf("  [RIGHT] / [D]          / Stick Right: Steer Right\n");
    printf("  [ESC]                   / Gamepad Start: Back / Pause\n");
    printf("  Mouse Click / Drag                  : Touch Screen Menu / HUD\n\n");
    
    int running = 1;
    int mouse_down = 0;
    float steer_tilt = 0.0f;
    uint64_t frame_count = 0;
    
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            }
            else if (ev.type == SDL_WINDOWEVENT) {
                if (ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                    int w = ev.window.data1;
                    int h = ev.window.data2;
                    glViewport(0, 0, w, h);
                    OnEvent(&fake_jni, NULL, 3, 0, w, h, 0);
                }
            }
            else if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                if (!controller) {
                    controller = SDL_GameControllerOpen(ev.cdevice.which);
                    if (controller) {
                        printf("[INPUT] Connected Game Controller: %s\n", SDL_GameControllerName(controller));
                    }
                }
            }
            else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
                if (controller && ev.cdevice.which == (Sint32)SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))) {
                    SDL_GameControllerClose(controller);
                    controller = NULL;
                    printf("[INPUT] Game Controller disconnected\n");
                }
            }
            else if (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP) {
                int pressed = (ev.type == SDL_CONTROLLERBUTTONDOWN) ? 1 : 0;
                int keycode = 0;
                switch (ev.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_A:
                        keycode = KEY_DPAD_CENTER;
                        break;
                    case SDL_CONTROLLER_BUTTON_B:
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                        keycode = KEY_BUTTON_X;
                        break;
                    case SDL_CONTROLLER_BUTTON_X:
                    case SDL_CONTROLLER_BUTTON_Y:
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                        keycode = KEY_BUTTON_Y;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                        keycode = KEY_DPAD_LEFT;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                        keycode = KEY_DPAD_RIGHT;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:
                        keycode = KEY_DPAD_UP;
                        break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                        keycode = KEY_DPAD_DOWN;
                        break;
                    case SDL_CONTROLLER_BUTTON_START:
                    case SDL_CONTROLLER_BUTTON_BACK:
                        keycode = KEY_BACK;
                        break;
                    default:
                        break;
                }
                if (keycode) {
                    OnEvent(&fake_jni, NULL, 1, 0, keycode, 0, pressed);
                }
            }
            else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
                int pressed = (ev.type == SDL_KEYDOWN) ? 1 : 0;
                int keycode = 0;
                
                switch (ev.key.keysym.sym) {
                    case SDLK_UP:
                    case SDLK_w:
                        keycode = KEY_DPAD_UP;
                        break;
                    case SDLK_DOWN:
                    case SDLK_s:
                        keycode = KEY_DPAD_DOWN;
                        break;
                    case SDLK_LEFT:
                    case SDLK_a:
                        keycode = KEY_DPAD_LEFT;
                        break;
                    case SDLK_RIGHT:
                    case SDLK_d:
                        keycode = KEY_DPAD_RIGHT;
                        break;
                    case SDLK_RETURN:
                    case SDLK_SPACE:
                    case SDLK_x:
                        keycode = KEY_DPAD_CENTER;
                        break;
                    case SDLK_z:
                        keycode = KEY_BUTTON_X;
                        break;
                    case SDLK_c:
                    case SDLK_LSHIFT:
                    case SDLK_RSHIFT:
                        keycode = KEY_BUTTON_Y;
                        break;
                    case SDLK_ESCAPE:
                    case SDLK_BACKSPACE:
                        keycode = KEY_BACK;
                        break;
                    default:
                        break;
                }
                
                if (keycode) {
                    OnEvent(&fake_jni, NULL, 1, 0, keycode, 0, pressed);
                }
            }
            else if (ev.type == SDL_MOUSEBUTTONDOWN) {
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    mouse_down = 1;
                    int x = ev.button.x;
                    int y = ev.button.y;
                    OnEvent(&fake_jni, NULL, 1, 1, x, y, 0 | (1 << 16)); /* ACTION_DOWN */
                }
            }
            else if (ev.type == SDL_MOUSEBUTTONUP) {
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    mouse_down = 0;
                    int x = ev.button.x;
                    int y = ev.button.y;
                    OnEvent(&fake_jni, NULL, 1, 1, x, y, 1 | (1 << 16)); /* ACTION_UP */
                }
            }
            else if (ev.type == SDL_MOUSEMOTION) {
                if (mouse_down) {
                    int x = ev.motion.x;
                    int y = ev.motion.y;
                    OnEvent(&fake_jni, NULL, 1, 1, x, y, 2 | (1 << 16)); /* ACTION_MOVE */
                }
            }
        }
        
        /* Calculate steering from keyboard and analog stick */
        const Uint8 *keystates = SDL_GetKeyboardState(NULL);
        if (keystates[SDL_SCANCODE_LEFT] || keystates[SDL_SCANCODE_A]) {
            steer_tilt = -7.0f;
        } else if (keystates[SDL_SCANCODE_RIGHT] || keystates[SDL_SCANCODE_D]) {
            steer_tilt = 7.0f;
        } else {
            steer_tilt = 0.0f;
        }
        
        if (controller) {
            Sint16 stick_x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
            if (abs(stick_x) > 3200) {
                steer_tilt = (stick_x / 32767.0f) * 7.0f;
            }
        }
        
        /* Send accelerometer sensor update (landscape orientation: y is lateral tilt) */
        int tilt_y = (int)(steer_tilt * 6553.6f);
        int grav_z = (int)(9.81f * 6553.6f);
        OnEvent(&fake_jni, NULL, 4, 0, 0, tilt_y, grav_z);
        
        /* Render frame */
        int keep = OnEvent(&fake_jni, NULL, 0, 1, 0, 0, 0);
        if (keep == 0) {
            printf("[MAIN] Engine requested termination.\n");
            running = 0;
        }
        
        SDL_GL_SwapWindow(g_window);
        frame_count++;
        if (frame_count % 600 == 0) {
            printf("[MAIN] Frame %llu rendered smoothly (~60 FPS)\n", (unsigned long long)frame_count);
        }
        SDL_Delay(16); /* ~60 FPS */
    }
    
    printf("[MAIN] Shutting down game...\n");
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = NULL;
    }
    audio_shutdown();
    OnEvent(&fake_jni, NULL, 0, 0, 0, 0, 0);
    if (OnDestroy) {
        OnDestroy(&fake_jni, NULL);
    }
    
    SDL_GL_DeleteContext(g_gl_ctx);
    SDL_DestroyWindow(g_window);
    SDL_Quit();
    
    printf("[MAIN] Exited cleanly.\n");
    return 0;
}
