#pragma once
#include <ll/mod/NativeMod.h>
#include <pl/config/ConfigFile.h>
#include <pl/modmenu/ModMenu.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>
#include <dlfcn.h>
#include <cmath>
#include <algorithm>
#include <android/log.h>

// Optional LeviLamina client API. The public Android preloader SDK does not
// ship these headers, so ray-cast support is enabled automatically only when
// the client API headers are supplied by the build environment.
#if __has_include(<ll/api/service/TargetedBedrock.h>) && __has_include(<mc/world/actor/player/Player.h>)
    #define SPL_HAVE_LEVI_RAYCAST 1
    #include <ll/api/service/TargetedBedrock.h>
    #include <mc/world/actor/player/Player.h>
#else
    #define SPL_HAVE_LEVI_RAYCAST 0
#endif

#define SPL_TAG "SoundPhysicsLite"
#define SPL_LOGI(...) __android_log_print(ANDROID_LOG_INFO, SPL_TAG, __VA_ARGS__)
#define SPL_LOGW(...) __android_log_print(ANDROID_LOG_WARN, SPL_TAG, __VA_ARGS__)

// Offsets in libminecraftpe.so (function body / fields from asm)
namespace pe {
    constexpr uintptr_t kFnPlayPath   = 0x110CB1D0; // function start
    constexpr uintptr_t kCallPlaySound = 0x110CB390; // bl playSound (info only)
    constexpr uintptr_t kOffChannel   = 0x78;        // [x19+0x78] = Channel*
    constexpr uintptr_t kOffPosition  = 0x98;        // [x19+0x98] = position Vec3 used by set3DAttributes
    // PLT (do not hook as body)
    constexpr uintptr_t kPltPlaySound = 0x1298F5A0;
}

struct SoundPhysicsConfig {
    bool  enabled = true;
    bool  debug_logging = true;
    bool  enable_lowpass = true;
    float lowpass_cutoff = 2000.f;   // Hz, lower = more muffled
    float attenuation_factor = 1.f;
    float max_distance = 48.f;
    float min_volume = 0.05f;
    bool  enable_volume = true;      // also scale volume by distance if we have pos later
    bool  enable_raycast = true;     // block line-of-sight test
    float occluded_lowpass_gain = 0.25f; // gain when a solid block blocks the ray
    float raycast_max_distance = 48.f;
};

inline void to_json(nlohmann::json& j, const SoundPhysicsConfig& c) {
    j = {
        {"enabled", c.enabled},
        {"debug_logging", c.debug_logging},
        {"enable_lowpass", c.enable_lowpass},
        {"lowpass_cutoff", c.lowpass_cutoff},
        {"attenuation_factor", c.attenuation_factor},
        {"max_distance", c.max_distance},
        {"min_volume", c.min_volume},
        {"enable_volume", c.enable_volume},
        {"enable_raycast", c.enable_raycast},
        {"occluded_lowpass_gain", c.occluded_lowpass_gain},
        {"raycast_max_distance", c.raycast_max_distance}
    };
}
inline void from_json(const nlohmann::json& j, SoundPhysicsConfig& c) {
    if (j.contains("enabled")) j.at("enabled").get_to(c.enabled);
    if (j.contains("debug_logging")) j.at("debug_logging").get_to(c.debug_logging);
    if (j.contains("enable_lowpass")) j.at("enable_lowpass").get_to(c.enable_lowpass);
    if (j.contains("lowpass_cutoff")) j.at("lowpass_cutoff").get_to(c.lowpass_cutoff);
    if (j.contains("attenuation_factor")) j.at("attenuation_factor").get_to(c.attenuation_factor);
    if (j.contains("max_distance")) j.at("max_distance").get_to(c.max_distance);
    if (j.contains("min_volume")) j.at("min_volume").get_to(c.min_volume);
    if (j.contains("enable_volume")) j.at("enable_volume").get_to(c.enable_volume);
    if (j.contains("enable_raycast")) j.at("enable_raycast").get_to(c.enable_raycast);
    if (j.contains("occluded_lowpass_gain")) j.at("occluded_lowpass_gain").get_to(c.occluded_lowpass_gain);
    if (j.contains("raycast_max_distance")) j.at("raycast_max_distance").get_to(c.raycast_max_distance);
}

// Minimal FMOD C API typedefs (resolved via dlsym)
using FMOD_SYSTEM = void;
using FMOD_CHANNEL = void;
using FMOD_DSP = void;
using FMOD_RESULT = int;

using FN_CreateDSPByType = FMOD_RESULT (*)(FMOD_SYSTEM*, int, FMOD_DSP**);
using FN_AddDSP          = FMOD_RESULT (*)(FMOD_CHANNEL*, int, FMOD_DSP*);
using FN_SetParamFloat   = FMOD_RESULT (*)(FMOD_DSP*, int, float);
using FN_SetActive       = FMOD_RESULT (*)(FMOD_DSP*, int);
using FN_ReleaseDSP      = FMOD_RESULT (*)(FMOD_DSP*);
using FN_GetSystemObject = FMOD_RESULT (*)(FMOD_CHANNEL*, FMOD_SYSTEM**);
using FN_SetVolume       = FMOD_RESULT (*)(FMOD_CHANNEL*, float);
using FN_SetLowPassGain  = FMOD_RESULT (*)(FMOD_CHANNEL*, float);

struct Vec3Like { float x; float y; float z; };

class SoundPhysicsLite {
public:
    explicit SoundPhysicsLite();
    bool load();
    bool enable();
    bool disable();
    static SoundPhysicsLite& getInstance();
    void saveConfig();
    void loadConfig();

    // Called after game play path; channel from [this+0x78]
    void onChannelReady(void* soundInstance /* x19 */, void* channel);

private:
    ll::mod::NativeMod& mSelf;
    SoundPhysicsConfig mConfig;
    pl::config::ConfigFile<SoundPhysicsConfig> mConfigFile;

    void* mFmod = nullptr;
    FN_CreateDSPByType mCreateDSPByType = nullptr;
    FN_AddDSP mAddDSP = nullptr;
    FN_SetParamFloat mSetParamFloat = nullptr;
    FN_SetActive mSetActive = nullptr;
    FN_ReleaseDSP mReleaseDSP = nullptr;
    FN_GetSystemObject mGetSystem = nullptr;
    FN_SetVolume mSetVolume = nullptr;
    FN_SetLowPassGain mSetLPGain = nullptr;

    int mLowpassType = -1; // resolved at runtime if possible
    bool mHooked = false;
    void* mOrigPlayFn = nullptr;

    std::mutex mMapMtx;
    std::unordered_map<void*, FMOD_DSP*> mChannelDsp;

    void registerModMenu();
    bool loadFmod();
    bool resolveLowpassType(FMOD_SYSTEM* sys);
    bool installHook();
    void attachLowpass(FMOD_CHANNEL* ch);
    bool raycastOccluded(const Vec3Like& sourcePos) const;
};
