#ifndef AUDIO_H
#define AUDIO_H

#include "fake_jni.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*AudioMix_fn)(JNIEnv *env, jclass clazz, jobject buf, jint frames);

void audio_init(AudioMix_fn mix_func);
void audio_start(void);
void audio_stop(void);
void audio_set_volume(int left, int right);
void audio_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
