#include "fake_jni.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void (*g_swap_buffers)(void) = NULL;

/* Generic fallback stub so unpopulated slots never crash */
static void *JNICALL jni_generic_stub(void) {
    return NULL;
}

/* Array representation */
typedef struct {
    jsize len;
    jsize elem_size;
    uint8_t data[];
} fake_array_t;

static jarray create_fake_array(jsize len, jsize elem_size) {
    if (len < 0) len = 0;
    fake_array_t *arr = (fake_array_t *)calloc(1, sizeof(fake_array_t) + len * elem_size + 8);
    if (arr) {
        arr->len = len;
        arr->elem_size = elem_size;
    }
    return (jarray)arr;
}

/* Method identifiers */
enum {
    M_NONE = 0,
    M_EglCreate,
    M_EglUpdateDisplay,
    M_Activate,
    M_GetIMEI,
    M_GetPhoneNr,
    M_GetCountryCode,
    M_GetOperatorName,
    M_GetModelName,
    M_GetPlatformVersion,
    M_LaunchUrl,
    M_AudioStop,
    M_AudioStart,
    M_AudioCreate,
    M_AudioGetVolume,
    M_AudioSetVolume,
    M_ActivateAccelerometer,
    M_Drm_init,
    M_doLicenseCheck,
    M_InputDialog_init,
    M_Show,
    M_GetStatus,
    M_GetMessage,
    M_FuseFaceBook_init,
    M_Login,
    M_Logout,
    M_PostOnWall,
    M_Like,
    M_GetFriends,
    M_AdViewMain_init,
    M_getWidth,
    M_getHeight,
    M_createAdView,
    M_showAdView,
    M_hideAdView,
    M_refreshAdView,
    M_switchStream,
    M_postDisableAdsMessage,
    M_InterstitialManager_init,
    M_initInterstitialManager,
    M_showInterstitial,
    M_InAppBilling_init,
    M_initiatePurchase,
    M_stopPurchase,
    M_refreshPurchases,
    M_infoPurchase,
    M_Media_init,
    M_play,
    M_stop,
    M_pause,
    M_resume,
    M_FuseWebView_init,
    M_open,
    M_close,
    M_TelemetrySubmit
};

typedef struct {
    int id;
    const char *name;
} method_mapping_t;

static const method_mapping_t g_methods[] = {
    { M_EglCreate,             "EglCreate" },
    { M_EglUpdateDisplay,      "EglUpdateDisplay" },
    { M_Activate,              "Activate" },
    { M_GetIMEI,               "GetIMEI" },
    { M_GetPhoneNr,            "GetPhoneNr" },
    { M_GetCountryCode,        "GetCountryCode" },
    { M_GetOperatorName,       "GetOperatorName" },
    { M_GetModelName,          "GetModelName" },
    { M_GetPlatformVersion,    "GetPlatformVersion" },
    { M_LaunchUrl,             "LaunchUrl" },
    { M_AudioStop,             "AudioStop" },
    { M_AudioStart,            "AudioStart" },
    { M_AudioCreate,           "AudioCreate" },
    { M_AudioGetVolume,        "AudioGetVolume" },
    { M_AudioSetVolume,        "AudioSetVolume" },
    { M_ActivateAccelerometer, "ActivateAccelerometer" },
    { M_Drm_init,              "com/polarbit/fuse/license/Drm/<init>" },
    { M_doLicenseCheck,        "doLicenseCheck" },
    { M_InputDialog_init,      "com/polarbit/fuse/input/InputDialog/<init>" },
    { M_Show,                  "Show" },
    { M_GetStatus,             "GetStatus" },
    { M_GetMessage,            "GetMessage" },
    { M_FuseFaceBook_init,     "com/polarbit/fuse/facebook/FuseFaceBook/<init>" },
    { M_Login,                 "Login" },
    { M_Logout,                "Logout" },
    { M_PostOnWall,            "PostOnWall" },
    { M_Like,                  "Like" },
    { M_GetFriends,            "GetFriends" },
    { M_AdViewMain_init,       "com/polarbit/fuse/ads/AdViewMain/<init>" },
    { M_getWidth,              "getWidth" },
    { M_getHeight,             "getHeight" },
    { M_createAdView,          "createAdView" },
    { M_showAdView,            "showAdView" },
    { M_hideAdView,            "hideAdView" },
    { M_refreshAdView,         "refreshAdView" },
    { M_switchStream,          "switchStream" },
    { M_postDisableAdsMessage, "postDisableAdsMessage" },
    { M_InterstitialManager_init, "com/polarbit/fuse/ads/InterstitialManager/<init>" },
    { M_initInterstitialManager, "initInterstitialManager" },
    { M_showInterstitial,      "showInterstitial" },
    { M_InAppBilling_init,     "com/polarbit/fuse/billing/InAppBilling/<init>" },
    { M_initiatePurchase,      "initiatePurchase" },
    { M_stopPurchase,          "stopPurchase" },
    { M_refreshPurchases,      "refreshPurchases" },
    { M_infoPurchase,          "infoPurchase" },
    { M_Media_init,            "com/polarbit/fuse/media/Media/<init>" },
    { M_play,                  "play" },
    { M_stop,                  "stop" },
    { M_pause,                 "pause" },
    { M_resume,                "resume" },
    { M_FuseWebView_init,      "com/polarbit/fuse/FuseWebView/<init>" },
    { M_open,                  "open" },
    { M_close,                 "close" },
    { M_TelemetrySubmit,       "TelemetrySubmit" }
};

static jmethodID find_method_id(const char *name) {
    if (!name) return (jmethodID)(uintptr_t)M_NONE;
    for (size_t i = 0; i < sizeof(g_methods)/sizeof(g_methods[0]); i++) {
        if (strcmp(g_methods[i].name, name) == 0 ||
            strstr(name, g_methods[i].name) != NULL) {
            return (jmethodID)(uintptr_t)g_methods[i].id;
        }
    }
    printf("[JNI] Queried method: %s\n", name);
    return (jmethodID)(uintptr_t)M_NONE;
}

/* JNIEnv implementation functions */
static jint JNICALL jni_GetVersion(JNIEnv *env) {
    return JNI_VERSION_1_6;
}

static jclass JNICALL jni_FindClass(JNIEnv *env, const char *name) {
    return (jclass)strdup(name ? name : "unknown");
}

static jclass JNICALL jni_GetObjectClass(JNIEnv *env, jobject obj) {
    return (jclass)"unknownClass";
}

static jboolean JNICALL jni_IsInstanceOf(JNIEnv *env, jobject obj, jclass clazz) {
    return JNI_TRUE;
}

static jboolean JNICALL jni_IsAssignableFrom(JNIEnv *env, jclass sub, jclass sup) {
    return JNI_TRUE;
}

static jobject JNICALL jni_AllocObject(JNIEnv *env, jclass clazz) {
    static int dummy_obj = 1;
    return (jobject)&dummy_obj;
}

static jobject JNICALL jni_NewObject(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    static int dummy_obj = 1;
    return (jobject)&dummy_obj;
}

static jobject JNICALL jni_NewObjectV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    static int dummy_obj = 1;
    return (jobject)&dummy_obj;
}

static jmethodID JNICALL jni_GetMethodID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
    const char *cls = (const char *)clazz;
    if (name && strcmp(name, "<init>") == 0) {
        if (cls) {
            if (strstr(cls, "Media")) return (jmethodID)(uintptr_t)M_Media_init;
            if (strstr(cls, "Drm")) return (jmethodID)(uintptr_t)M_Drm_init;
            if (strstr(cls, "InputDialog")) return (jmethodID)(uintptr_t)M_InputDialog_init;
            if (strstr(cls, "FuseFaceBook")) return (jmethodID)(uintptr_t)M_FuseFaceBook_init;
            if (strstr(cls, "AdViewMain")) return (jmethodID)(uintptr_t)M_AdViewMain_init;
            if (strstr(cls, "InterstitialManager")) return (jmethodID)(uintptr_t)M_InterstitialManager_init;
            if (strstr(cls, "InAppBilling")) return (jmethodID)(uintptr_t)M_InAppBilling_init;
            if (strstr(cls, "FuseWebView")) return (jmethodID)(uintptr_t)M_FuseWebView_init;
        }
        return (jmethodID)1;
    }
    return find_method_id(name);
}

static jmethodID JNICALL jni_GetStaticMethodID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
    if (name && strcmp(name, "<init>") == 0) {
        return (jmethodID)1;
    }
    return find_method_id(name);
}

static jfieldID JNICALL jni_GetFieldID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
    return (jfieldID)1;
}

static jfieldID JNICALL jni_GetStaticFieldID(JNIEnv *env, jclass clazz, const char *name, const char *sig) {
    return (jfieldID)1;
}

static jobject JNICALL jni_NewGlobalRef(JNIEnv *env, jobject obj) {
    return obj;
}

static void JNICALL jni_DeleteGlobalRef(JNIEnv *env, jobject obj) {}

static jobject JNICALL jni_NewLocalRef(JNIEnv *env, jobject obj) {
    return obj;
}

static void JNICALL jni_DeleteLocalRef(JNIEnv *env, jobject obj) {}

static jboolean JNICALL jni_IsSameObject(JNIEnv *env, jobject obj1, jobject obj2) {
    return (obj1 == obj2) ? JNI_TRUE : JNI_FALSE;
}

static jstring JNICALL jni_NewStringUTF(JNIEnv *env, const char *bytes) {
    return (jstring)strdup(bytes ? bytes : "");
}

static const char *JNICALL jni_GetStringUTFChars(JNIEnv *env, jstring string, jboolean *isCopy) {
    if (isCopy) *isCopy = JNI_FALSE;
    return (const char *)string;
}

static void JNICALL jni_ReleaseStringUTFChars(JNIEnv *env, jstring string, const char *utf) {}

static jsize JNICALL jni_GetStringUTFLength(JNIEnv *env, jstring string) {
    return string ? (jsize)strlen((const char *)string) : 0;
}

static jsize JNICALL jni_GetStringLength(JNIEnv *env, jstring string) {
    return string ? (jsize)strlen((const char *)string) : 0;
}

static jsize JNICALL jni_GetArrayLength(JNIEnv *env, jarray array) {
    if (!array) return 0;
    fake_array_t *arr = (fake_array_t *)array;
    return arr->len;
}

/* Primitive Arrays */
static jintArray JNICALL jni_NewIntArray(JNIEnv *env, jsize len) {
    return (jintArray)create_fake_array(len, sizeof(jint));
}

static jbyteArray JNICALL jni_NewByteArray(JNIEnv *env, jsize len) {
    return (jbyteArray)create_fake_array(len, sizeof(jbyte));
}

static jbooleanArray JNICALL jni_NewBooleanArray(JNIEnv *env, jsize len) {
    return (jbooleanArray)create_fake_array(len, sizeof(jboolean));
}

static jcharArray JNICALL jni_NewCharArray(JNIEnv *env, jsize len) {
    return (jcharArray)create_fake_array(len, sizeof(jchar));
}

static jshortArray JNICALL jni_NewShortArray(JNIEnv *env, jsize len) {
    return (jshortArray)create_fake_array(len, sizeof(jshort));
}

static jlongArray JNICALL jni_NewLongArray(JNIEnv *env, jsize len) {
    return (jlongArray)create_fake_array(len, sizeof(jlong));
}

static jfloatArray JNICALL jni_NewFloatArray(JNIEnv *env, jsize len) {
    return (jfloatArray)create_fake_array(len, sizeof(jfloat));
}

static jdoubleArray JNICALL jni_NewDoubleArray(JNIEnv *env, jsize len) {
    return (jdoubleArray)create_fake_array(len, sizeof(jdouble));
}

static jobjectArray JNICALL jni_NewObjectArray(JNIEnv *env, jsize len, jclass clazz, jobject init) {
    return (jobjectArray)create_fake_array(len, sizeof(jobject));
}

static jobject JNICALL jni_GetObjectArrayElement(JNIEnv *env, jobjectArray array, jsize index) {
    if (!array) return NULL;
    fake_array_t *arr = (fake_array_t *)array;
    if (index < 0 || index >= arr->len) return NULL;
    jobject *objs = (jobject *)arr->data;
    return objs[index];
}

static void JNICALL jni_SetObjectArrayElement(JNIEnv *env, jobjectArray array, jsize index, jobject val) {
    if (!array) return;
    fake_array_t *arr = (fake_array_t *)array;
    if (index < 0 || index >= arr->len) return;
    jobject *objs = (jobject *)arr->data;
    objs[index] = val;
}

static jint *JNICALL jni_GetIntArrayElements(JNIEnv *env, jintArray array, jboolean *isCopy) {
    if (isCopy) *isCopy = JNI_FALSE;
    if (!array) return NULL;
    fake_array_t *arr = (fake_array_t *)array;
    return (jint *)arr->data;
}

static void JNICALL jni_ReleaseIntArrayElements(JNIEnv *env, jintArray array, jint *elems, jint mode) {}

static void JNICALL jni_GetIntArrayRegion(JNIEnv *env, jintArray array, jsize start, jsize len, jint *buf) {
    if (!array || !buf) return;
    fake_array_t *arr = (fake_array_t *)array;
    if (start < 0 || len < 0 || start + len > arr->len) return;
    memcpy(buf, arr->data + start * sizeof(jint), len * sizeof(jint));
}

static void JNICALL jni_SetIntArrayRegion(JNIEnv *env, jintArray array, jsize start, jsize len, const jint *buf) {
    if (!array || !buf) return;
    fake_array_t *arr = (fake_array_t *)array;
    if (start < 0 || len < 0 || start + len > arr->len) return;
    memcpy(arr->data + start * sizeof(jint), buf, len * sizeof(jint));
}

static jbyte *JNICALL jni_GetByteArrayElements(JNIEnv *env, jbyteArray array, jboolean *isCopy) {
    if (isCopy) *isCopy = JNI_FALSE;
    if (!array) return NULL;
    fake_array_t *arr = (fake_array_t *)array;
    return (jbyte *)arr->data;
}

static void JNICALL jni_ReleaseByteArrayElements(JNIEnv *env, jbyteArray array, jbyte *elems, jint mode) {}

static void JNICALL jni_GetByteArrayRegion(JNIEnv *env, jbyteArray array, jsize start, jsize len, jbyte *buf) {
    if (!array || !buf) return;
    fake_array_t *arr = (fake_array_t *)array;
    if (start < 0 || len < 0 || start + len > arr->len) return;
    memcpy(buf, arr->data + start, len);
}

static void JNICALL jni_SetByteArrayRegion(JNIEnv *env, jbyteArray array, jsize start, jsize len, const jbyte *buf) {
    if (!array || !buf) return;
    fake_array_t *arr = (fake_array_t *)array;
    if (start < 0 || len < 0 || start + len > arr->len) return;
    memcpy(arr->data + start, buf, len);
}

static void *JNICALL jni_GetPrimitiveArrayCritical(JNIEnv *env, jarray array, jboolean *isCopy) {
    if (isCopy) *isCopy = JNI_FALSE;
    if (!array) return NULL;
    return ((fake_array_t *)array)->data;
}

static void JNICALL jni_ReleasePrimitiveArrayCritical(JNIEnv *env, jarray array, void *carray, jint mode) {}

/* Direct Buffers */
static void *JNICALL jni_GetDirectBufferAddress(JNIEnv *env, jobject buf) {
    return buf;
}

static jlong JNICALL jni_GetDirectBufferCapacity(JNIEnv *env, jobject buf) {
    return 1024 * 1024;
}

static jobject JNICALL jni_NewDirectByteBuffer(JNIEnv *env, void *address, jlong capacity) {
    return address;
}

/* Method Invocation */
static jboolean JNICALL jni_CallBooleanMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {
    int id = (int)(uintptr_t)methodID;
    if (id == M_EglCreate || id == M_Activate || id == M_ActivateAccelerometer) {
        return JNI_TRUE;
    }
    if (id == M_EglUpdateDisplay) {
        if (g_swap_buffers) g_swap_buffers();
        return JNI_TRUE;
    }
    return JNI_TRUE;
}

static jboolean JNICALL jni_CallBooleanMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    va_list args;
    va_start(args, methodID);
    jboolean res = jni_CallBooleanMethodV(env, obj, methodID, args);
    va_end(args);
    return res;
}

static jboolean JNICALL jni_CallStaticBooleanMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    return jni_CallBooleanMethodV(env, (jobject)clazz, methodID, args);
}

static jboolean JNICALL jni_CallStaticBooleanMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    va_list args;
    va_start(args, methodID);
    jboolean res = jni_CallStaticBooleanMethodV(env, clazz, methodID, args);
    va_end(args);
    return res;
}

static jint JNICALL jni_CallIntMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {
    int id = (int)(uintptr_t)methodID;
    if (id == M_AudioStart || id == M_AudioCreate) {
        return 1;
    }
    if (id == M_AudioGetVolume) {
        return 1;
    }
    if (id == M_doLicenseCheck) {
        return 1;
    }
    return 0;
}

static jint JNICALL jni_CallIntMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    va_list args;
    va_start(args, methodID);
    jint res = jni_CallIntMethodV(env, obj, methodID, args);
    va_end(args);
    return res;
}

static jint JNICALL jni_CallStaticIntMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    return jni_CallIntMethodV(env, (jobject)clazz, methodID, args);
}

static jint JNICALL jni_CallStaticIntMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    va_list args;
    va_start(args, methodID);
    jint res = jni_CallStaticIntMethodV(env, clazz, methodID, args);
    va_end(args);
    return res;
}

static void JNICALL jni_CallVoidMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {}

static void JNICALL jni_CallVoidMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    va_list args;
    va_start(args, methodID);
    jni_CallVoidMethodV(env, obj, methodID, args);
    va_end(args);
}

static void JNICALL jni_CallStaticVoidMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {}

static void JNICALL jni_CallStaticVoidMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    va_list args;
    va_start(args, methodID);
    jni_CallStaticVoidMethodV(env, clazz, methodID, args);
    va_end(args);
}

static jobject JNICALL jni_CallObjectMethodV(JNIEnv *env, jobject obj, jmethodID methodID, va_list args) {
    int id = (int)(uintptr_t)methodID;
    if (id == M_GetIMEI) return jni_NewStringUTF(env, "WINDOWS00000000");
    if (id == M_GetPhoneNr) return jni_NewStringUTF(env, "null");
    if (id == M_GetCountryCode) return jni_NewStringUTF(env, "us");
    if (id == M_GetOperatorName) return jni_NewStringUTF(env, "null");
    if (id == M_GetModelName) return jni_NewStringUTF(env, "PC");
    if (id == M_GetPlatformVersion) return jni_NewStringUTF(env, "19");
    if (id == M_GetMessage) return jni_NewStringUTF(env, "Player");
    
    static int placeholder;
    return (jobject)&placeholder;
}

static jobject JNICALL jni_CallObjectMethod(JNIEnv *env, jobject obj, jmethodID methodID, ...) {
    va_list args;
    va_start(args, methodID);
    jobject res = jni_CallObjectMethodV(env, obj, methodID, args);
    va_end(args);
    return res;
}

static jobject JNICALL jni_CallStaticObjectMethodV(JNIEnv *env, jclass clazz, jmethodID methodID, va_list args) {
    return jni_CallObjectMethodV(env, (jobject)clazz, methodID, args);
}

static jobject JNICALL jni_CallStaticObjectMethod(JNIEnv *env, jclass clazz, jmethodID methodID, ...) {
    va_list args;
    va_start(args, methodID);
    jobject res = jni_CallStaticObjectMethodV(env, clazz, methodID, args);
    va_end(args);
    return res;
}

static jint JNICALL jni_GetJavaVM(JNIEnv *env, JavaVM **vm) {
    *vm = &fake_jvm;
    return JNI_OK;
}

static jboolean JNICALL jni_ExceptionCheck(JNIEnv *env) {
    return JNI_FALSE;
}

static void JNICALL jni_ExceptionClear(JNIEnv *env) {}

static jthrowable JNICALL jni_ExceptionOccurred(JNIEnv *env) {
    return NULL;
}

static jint JNICALL jni_RegisterNatives(JNIEnv *env, jclass clazz, const JNINativeMethod *methods, jint nMethods) {
    return JNI_OK;
}

static jint JNICALL jni_UnregisterNatives(JNIEnv *env, jclass clazz) {
    return JNI_OK;
}

static jint JNICALL jni_MonitorEnter(JNIEnv *env, jobject obj) {
    return JNI_OK;
}

static jint JNICALL jni_MonitorExit(JNIEnv *env, jobject obj) {
    return JNI_OK;
}

static jint JNICALL jni_PushLocalFrame(JNIEnv *env, jint capacity) {
    return JNI_OK;
}

static jobject JNICALL jni_PopLocalFrame(JNIEnv *env, jobject result) {
    return result;
}

static jint JNICALL jni_EnsureLocalCapacity(JNIEnv *env, jint capacity) {
    return JNI_OK;
}

/* JVM implementation */
static jint JNICALL jvm_AttachCurrentThread(JavaVM *vm, void **penv, void *args) {
    *penv = (void*)&fake_jni;
    return JNI_OK;
}

static jint JNICALL jvm_DetachCurrentThread(JavaVM *vm) {
    return JNI_OK;
}

static jint JNICALL jvm_GetEnv(JavaVM *vm, void **penv, jint version) {
    *penv = (void*)&fake_jni;
    return JNI_OK;
}

static jint JNICALL jvm_DestroyJavaVM(JavaVM *vm) {
    return JNI_OK;
}

static struct JNINativeInterface g_jni_table;
static struct JNIInvokeInterface g_jvm_table;

JavaVM fake_jvm = &g_jvm_table;
JNIEnv fake_jni = &g_jni_table;

void fake_jni_init(void (*swap_cb)(void)) {
    g_swap_buffers = swap_cb;
    
    /* Populate table with safe fallback generic stubs so uninitialized slots never jump to NULL */
    void **ptr = (void **)&g_jni_table;
    for (size_t i = 0; i < sizeof(g_jni_table)/sizeof(void *); i++) {
        ptr[i] = (void *)jni_generic_stub;
    }
    memset(&g_jvm_table, 0, sizeof(g_jvm_table));
    
    g_jni_table.GetVersion = jni_GetVersion;
    g_jni_table.FindClass = jni_FindClass;
    g_jni_table.GetObjectClass = jni_GetObjectClass;
    g_jni_table.IsInstanceOf = jni_IsInstanceOf;
    g_jni_table.IsAssignableFrom = jni_IsAssignableFrom;
    
    g_jni_table.GetMethodID = jni_GetMethodID;
    g_jni_table.GetStaticMethodID = jni_GetStaticMethodID;
    g_jni_table.GetFieldID = jni_GetFieldID;
    g_jni_table.GetStaticFieldID = jni_GetStaticFieldID;
    g_jni_table.AllocObject = jni_AllocObject;
    g_jni_table.NewObject = jni_NewObject;
    g_jni_table.NewObjectV = jni_NewObjectV;
    
    g_jni_table.NewGlobalRef = jni_NewGlobalRef;
    g_jni_table.DeleteGlobalRef = jni_DeleteGlobalRef;
    g_jni_table.NewLocalRef = jni_NewLocalRef;
    g_jni_table.DeleteLocalRef = jni_DeleteLocalRef;
    g_jni_table.IsSameObject = jni_IsSameObject;
    
    g_jni_table.PushLocalFrame = jni_PushLocalFrame;
    g_jni_table.PopLocalFrame = jni_PopLocalFrame;
    g_jni_table.EnsureLocalCapacity = jni_EnsureLocalCapacity;
    
    g_jni_table.NewStringUTF = jni_NewStringUTF;
    g_jni_table.GetStringUTFChars = jni_GetStringUTFChars;
    g_jni_table.ReleaseStringUTFChars = jni_ReleaseStringUTFChars;
    g_jni_table.GetStringUTFLength = jni_GetStringUTFLength;
    g_jni_table.GetStringLength = jni_GetStringLength;
    
    g_jni_table.GetArrayLength = jni_GetArrayLength;
    g_jni_table.NewObjectArray = jni_NewObjectArray;
    g_jni_table.GetObjectArrayElement = jni_GetObjectArrayElement;
    g_jni_table.SetObjectArrayElement = jni_SetObjectArrayElement;
    
    g_jni_table.NewIntArray = jni_NewIntArray;
    g_jni_table.GetIntArrayElements = jni_GetIntArrayElements;
    g_jni_table.ReleaseIntArrayElements = jni_ReleaseIntArrayElements;
    g_jni_table.GetIntArrayRegion = jni_GetIntArrayRegion;
    g_jni_table.SetIntArrayRegion = jni_SetIntArrayRegion;
    
    g_jni_table.NewByteArray = jni_NewByteArray;
    g_jni_table.GetByteArrayElements = jni_GetByteArrayElements;
    g_jni_table.ReleaseByteArrayElements = jni_ReleaseByteArrayElements;
    g_jni_table.GetByteArrayRegion = jni_GetByteArrayRegion;
    g_jni_table.SetByteArrayRegion = jni_SetByteArrayRegion;
    
    g_jni_table.NewBooleanArray = jni_NewBooleanArray;
    g_jni_table.NewCharArray = jni_NewCharArray;
    g_jni_table.NewShortArray = jni_NewShortArray;
    g_jni_table.NewLongArray = jni_NewLongArray;
    g_jni_table.NewFloatArray = jni_NewFloatArray;
    g_jni_table.NewDoubleArray = jni_NewDoubleArray;
    
    g_jni_table.GetPrimitiveArrayCritical = jni_GetPrimitiveArrayCritical;
    g_jni_table.ReleasePrimitiveArrayCritical = jni_ReleasePrimitiveArrayCritical;
    
    g_jni_table.GetDirectBufferAddress = jni_GetDirectBufferAddress;
    g_jni_table.GetDirectBufferCapacity = jni_GetDirectBufferCapacity;
    g_jni_table.NewDirectByteBuffer = jni_NewDirectByteBuffer;
    
    g_jni_table.CallBooleanMethod = jni_CallBooleanMethod;
    g_jni_table.CallBooleanMethodV = jni_CallBooleanMethodV;
    g_jni_table.CallStaticBooleanMethod = jni_CallStaticBooleanMethod;
    g_jni_table.CallStaticBooleanMethodV = jni_CallStaticBooleanMethodV;
    
    g_jni_table.CallIntMethod = jni_CallIntMethod;
    g_jni_table.CallIntMethodV = jni_CallIntMethodV;
    g_jni_table.CallStaticIntMethod = jni_CallStaticIntMethod;
    g_jni_table.CallStaticIntMethodV = jni_CallStaticIntMethodV;
    
    g_jni_table.CallVoidMethod = jni_CallVoidMethod;
    g_jni_table.CallVoidMethodV = jni_CallVoidMethodV;
    g_jni_table.CallStaticVoidMethod = jni_CallStaticVoidMethod;
    g_jni_table.CallStaticVoidMethodV = jni_CallStaticVoidMethodV;
    
    g_jni_table.CallObjectMethod = jni_CallObjectMethod;
    g_jni_table.CallObjectMethodV = jni_CallObjectMethodV;
    g_jni_table.CallStaticObjectMethod = jni_CallStaticObjectMethod;
    g_jni_table.CallStaticObjectMethodV = jni_CallStaticObjectMethodV;
    
    g_jni_table.GetJavaVM = jni_GetJavaVM;
    g_jni_table.ExceptionCheck = jni_ExceptionCheck;
    g_jni_table.ExceptionClear = jni_ExceptionClear;
    g_jni_table.ExceptionOccurred = jni_ExceptionOccurred;
    
    g_jni_table.RegisterNatives = jni_RegisterNatives;
    g_jni_table.UnregisterNatives = jni_UnregisterNatives;
    g_jni_table.MonitorEnter = jni_MonitorEnter;
    g_jni_table.MonitorExit = jni_MonitorExit;
    
    g_jvm_table.AttachCurrentThread = jvm_AttachCurrentThread;
    g_jvm_table.DetachCurrentThread = jvm_DetachCurrentThread;
    g_jvm_table.GetEnv = jvm_GetEnv;
    g_jvm_table.DestroyJavaVM = jvm_DestroyJavaVM;
}
