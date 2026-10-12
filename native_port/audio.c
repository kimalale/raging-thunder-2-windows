#include "audio.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

static AudioMix_fn g_audio_mix = NULL;
static SDL_AudioDeviceID g_device = 0;
static volatile int g_playing = 0;

static void audio_callback(void *userdata, Uint8 *stream, int len) {
    if (g_playing && g_audio_mix) {
        /* 16-bit stereo = 4 bytes per frame */
        int frames = len / 4;
        g_audio_mix(&fake_jni, NULL, (jobject)stream, frames);
    } else {
        memset(stream, 0, len);
    }
}

void audio_init(AudioMix_fn mix_func) {
    g_audio_mix = mix_func;
    
    SDL_AudioSpec wanted, obtained;
    memset(&wanted, 0, sizeof(wanted));
    wanted.freq = 44100;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = 2;
    wanted.samples = 1024;
    wanted.callback = audio_callback;
    
    g_device = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
    if (!g_device) {
        printf("[AUDIO] Failed to open audio device: %s\n", SDL_GetError());
        return;
    }
    
    printf("[AUDIO] Audio initialized: %d Hz, %d channels\n", obtained.freq, obtained.channels);
}

void audio_start(void) {
    g_playing = 1;
    if (g_device) {
        SDL_PauseAudioDevice(g_device, 0);
    }
}

void audio_stop(void) {
    g_playing = 0;
    if (g_device) {
        SDL_PauseAudioDevice(g_device, 1);
    }
}

void audio_set_volume(int left, int right) {
    /* Volume setting placeholder */
}

void audio_shutdown(void) {
    g_playing = 0;
    if (g_device) {
        SDL_CloseAudioDevice(g_device);
        g_device = 0;
    }
}
