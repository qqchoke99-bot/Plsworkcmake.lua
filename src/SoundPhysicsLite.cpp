#include "SoundPhysicsLite.hpp"
#include <ll/mod/RegisterHelper.h>
#include <pl/modmenu/ModuleBuilder.h>
#include <pl/memory/Hook.h>
#include <link.h>
#include <cstring>
#include <cmath>

SoundPhysicsLite& SoundPhysicsLite::getInstance() {
    static SoundPhysicsLite i;
    return i;
}

SoundPhysicsLite::SoundPhysicsLite()
    : mSelf(*ll::mod::NativeMod::current()),
      mConfigFile(mSelf.getDataDir() / "config.json", mConfig) {}

static uintptr_t getMinecraftPeBase() {
    static uintptr_t base = 0;
    if (base) return base;
    dl_iterate_phdr([](dl_phdr_info* info, size_t, void* data) -> int {
        if (info->dlpi_name && strstr(info->dlpi_name, "libminecraftpe.so")) {
            *reinterpret_cast<uintptr_t*>(data) = static_cast<uintptr_t>(info->dlpi_addr);
            return 1;
        }
        return 0;
    }, &base);
    return base;
}

// Original function signature unknown fully — use opaque call through saved pointer.
// ARM64 AAPCS: x0 = this (SoundInstance*)
using PlayPathFn = void (*)(void* self, ...);

static void* g_orig = nullptr;

// Detour: call original, then read Channel* from [self+0x78]
static void hook_PlayPath(void* self, ...) {
    // Call original with same x0; remaining args already in registers/stack per AAPCS.
    // For unknown arity, invoke via stored pointer carefully:
    using Fn = void (*)(void*);
    if (g_orig) {
        // Minimal: only pass this — if real fn needs more args they are still in
        // caller-saved regs from the game call frame when we only replace entry.
        // Prefer full signature when afi/args known.
        reinterpret_cast<Fn>(g_orig)(self);
    }
    if (!self) return;
    void* channel = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(self) + pe::kOffChannel);
    if (channel) {
        SoundPhysicsLite::getInstance().onChannelReady(self, channel);
    }
}

bool SoundPhysicsLite::load() {
    SPL_LOGI("Sound Physics Lite v0.8-MVP");
    SPL_LOGI("Hook target function body 0x%lx (NOT PLT)", (unsigned long)pe::kFnPlayPath);
    SPL_LOGI("Channel* field +0x%lx", (unsigned long)pe::kOffChannel);
    loadConfig();
    loadFmod();
    registerModMenu();
    return true;
}

bool SoundPhysicsLite::enable() {
    return installHook();
}

bool SoundPhysicsLite::disable() {
    if (mHooked) {
        void* target = reinterpret_cast<void*>(getMinecraftPeBase() + pe::kFnPlayPath);
        pl::memory::unhook(target, reinterpret_cast<void*>(&hook_PlayPath));
        mHooked = false;
        SPL_LOGI("unhooked");
    }
    return true;
}

bool SoundPhysicsLite::loadFmod() {
    mFmod = dlopen("libfmod.so", RTLD_NOW | RTLD_NOLOAD);
    if (!mFmod) mFmod = dlopen("libfmod.so", RTLD_NOW);
    if (!mFmod) {
        SPL_LOGW("libfmod.so not found");
        return false;
    }
    auto R = [&](const char* a, const char* b) -> void* {
        void* p = dlsym(mFmod, a);
        return p ? p : (b ? dlsym(mFmod, b) : nullptr);
    };
    mCreateDSPByType = (FN_CreateDSPByType)R("FMOD_System_CreateDSPByType", "FMOD5_System_CreateDSPByType");
    mAddDSP = (FN_AddDSP)R("FMOD_Channel_AddDSP", "FMOD5_Channel_AddDSP");
    mSetParamFloat = (FN_SetParamFloat)R("FMOD_DSP_SetParameterFloat", "FMOD5_DSP_SetParameterFloat");
    mSetActive = (FN_SetActive)R("FMOD_DSP_SetActive", "FMOD5_DSP_SetActive");
    mReleaseDSP = (FN_ReleaseDSP)R("FMOD_DSP_Release", "FMOD5_DSP_Release");
    mGetSystem = (FN_GetSystemObject)R("FMOD_Channel_GetSystemObject", "FMOD5_Channel_GetSystemObject");
    mSetVolume = (FN_SetVolume)R("FMOD_Channel_SetVolume", "FMOD5_Channel_SetVolume");
    mSetLPGain = (FN_SetLowPassGain)R("FMOD_Channel_SetLowPassGain", "FMOD5_Channel_SetLowPassGain");

    SPL_LOGI("FMOD CreateDSP=%p AddDSP=%p SetParam=%p GetSys=%p SetLPGain=%p",
             (void*)mCreateDSPByType, (void*)mAddDSP, (void*)mSetParamFloat,
             (void*)mGetSystem, (void*)mSetLPGain);
    return mAddDSP != nullptr || mSetLPGain != nullptr;
}

bool SoundPhysicsLite::resolveLowpassType(FMOD_SYSTEM* sys) {
    if (mLowpassType >= 0) return true;
    if (!mCreateDSPByType || !sys) return false;
    // Probe common FMOD_DSP_TYPE values for Lowpass (avoid hard wrong enum)
    // Official FMOD often: LOWPASS=7, LOWPASS_SIMPLE=8 (version-dependent)
    for (int t = 0; t <= 32; ++t) {
        FMOD_DSP* dsp = nullptr;
        if (mCreateDSPByType(sys, t, &dsp) == 0 && dsp) {
            // keep first that creates; refine with GetInfo if available later
            if (mReleaseDSP) mReleaseDSP(dsp);
            // Prefer types near documented lowpass range
            if (t == 7 || t == 8 || t == 9) {
                mLowpassType = t;
                SPL_LOGI("lowpass type candidate %d", t);
                return true;
            }
        }
    }
    mLowpassType = 7; // fallback; may fail create — then SetLowPassGain path used
    SPL_LOGW("using fallback lowpass type %d", mLowpassType);
    return true;
}

void SoundPhysicsLite::attachLowpass(FMOD_CHANNEL* ch) {
    if (!ch || !mConfig.enable_lowpass) return;

    // Path A: simple gain lowpass (no System* needed)
    if (mSetLPGain) {
        // map cutoff-ish to gain: lower cutoff intent -> lower gain
        float g = std::clamp(mConfig.lowpass_cutoff / 10000.f, 0.15f, 1.f);
        mSetLPGain(ch, g);
        if (mConfig.debug_logging) SPL_LOGI("SetLowPassGain ch=%p g=%.2f", ch, g);
        return;
    }

    // Path B: DSP lowpass
    if (!mCreateDSPByType || !mAddDSP) return;
    FMOD_SYSTEM* sys = nullptr;
    if (mGetSystem) mGetSystem(ch, &sys);
    if (!sys) {
        SPL_LOGW("no System* for channel");
        return;
    }
    resolveLowpassType(sys);
    FMOD_DSP* dsp = nullptr;
    if (mCreateDSPByType(sys, mLowpassType, &dsp) != 0 || !dsp) {
        SPL_LOGW("CreateDSPByType failed type=%d", mLowpassType);
        return;
    }
    if (mSetParamFloat) mSetParamFloat(dsp, 0, mConfig.lowpass_cutoff); // param0 often cutoff Hz
    if (mAddDSP) mAddDSP(ch, 0, dsp);
    if (mSetActive) mSetActive(dsp, 1);
    {
        std::lock_guard<std::mutex> lock(mMapMtx);
        mChannelDsp[ch] = dsp;
    }
    if (mConfig.debug_logging)
        SPL_LOGI("AddDSP lowpass ch=%p cutoff=%.0f", ch, mConfig.lowpass_cutoff);
}

bool SoundPhysicsLite::raycastOccluded(const Vec3Like& sourcePos) const {
#if SPL_HAVE_LEVI_RAYCAST
    if (!mConfig.enable_raycast) return false;

    auto ci = ll::service::bedrock::getClientInstance();
    if (!ci) return false;
    auto* player = ci->getLocalPlayer();
    if (!player) return false;

    const auto listener = player->getEyePos();
    const float dx = sourcePos.x - listener.x;
    const float dy = sourcePos.y - listener.y;
    const float dz = sourcePos.z - listener.z;
    const float dist2 = dx * dx + dy * dy + dz * dz;
    if (dist2 > mConfig.raycast_max_distance * mConfig.raycast_max_distance) return false;

    // Levi's Player::canSee performs the game's native block visibility test.
    // ShapeType value 0 is the default obstruction shape used by the engine.
    const bool visible = player->canSee(
        ::Vec3{sourcePos.x, sourcePos.y, sourcePos.z},
        static_cast<::ShapeType>(0)
    );
    return !visible;
#else
    (void)sourcePos;
    return false;
#endif
}

void SoundPhysicsLite::onChannelReady(void* soundInstance, void* channel) {
    if (!mConfig.enabled || !channel) return;

    attachLowpass(reinterpret_cast<FMOD_CHANNEL*>(channel));

    // The RE-confirmed object field +0x98 is the position data used immediately
    // before FMOD::ChannelControl::set3DAttributes at 0x110CB3CC.
    if (mConfig.enable_raycast && soundInstance) {
        const auto* p = reinterpret_cast<const float*>(
            reinterpret_cast<const uint8_t*>(soundInstance) + pe::kOffPosition
        );
        const Vec3Like sourcePos{p[0], p[1], p[2]};
        const bool blocked = raycastOccluded(sourcePos);

        if (mSetLPGain) {
            const float gain = blocked
                ? std::clamp(mConfig.occluded_lowpass_gain, 0.0f, 1.0f)
                : 1.0f;
            mSetLPGain(reinterpret_cast<FMOD_CHANNEL*>(channel), gain);
            if (mConfig.debug_logging)
                SPL_LOGI("raycast: %s source=(%.2f %.2f %.2f) lpGain=%.2f",
                         blocked ? "BLOCKED" : "CLEAR",
                         sourcePos.x, sourcePos.y, sourcePos.z, gain);
        }
    }
}

bool SoundPhysicsLite::installHook() {
    if (mHooked) return true;
    uintptr_t base = getMinecraftPeBase();
    if (!base) {
        SPL_LOGW("libminecraftpe base not found");
        return false;
    }
    void* target = reinterpret_cast<void*>(base + pe::kFnPlayPath);
    void* detour = reinterpret_cast<void*>(&hook_PlayPath);
    // HookPriority: CameraOverhaul used 0xc8 (200)
    int pri = 200;
    bool ok = pl::memory::hook(target, detour, &g_orig, static_cast<pl::memory::HookPriority>(pri));
    if (!ok) {
        SPL_LOGW("pl::memory::hook failed at %p", target);
        return false;
    }
    mOrigPlayFn = g_orig;
    mHooked = true;
    SPL_LOGI("hooked play-path fn @ %p (base+0x110CB1D0) orig=%p", target, g_orig);
    return true;
}

void SoundPhysicsLite::loadConfig() {
    try { mConfigFile.load(); } catch (...) {
        mConfig = SoundPhysicsConfig{};
        saveConfig();
    }
}
void SoundPhysicsLite::saveConfig() {
    try { mConfigFile.save(); } catch (...) {}
}

void SoundPhysicsLite::registerModMenu() {
    using namespace pl::modmenu;
    ModuleBuilder b("sound_physics_lite", "Sound Physics Lite");
    b.config("enabled", "Enable", ConfigType::Bool, "true", "");
    b.config("enable_lowpass", "Lowpass", ConfigType::Bool, "true", "MVP muffling");
    b.config("lowpass_cutoff", "Cutoff Hz", ConfigType::Float, "2000", "500-22000");
    b.config("debug_logging", "Debug log", ConfigType::Bool, "true", "logcat");
    b.config("enable_raycast", "Block raycast", ConfigType::Bool, "true", "raycast source -> listener");
    b.config("occluded_lowpass_gain", "Occluded gain", ConfigType::Float, "0.25", "0..1");
    b.config("raycast_max_distance", "Raycast distance", ConfigType::Float, "48", "blocks");
    b.onConfigChanged([this](const std::string& k, const std::string& v) {
        if (k == "enabled") mConfig.enabled = (v == "true");
        else if (k == "enable_lowpass") mConfig.enable_lowpass = (v == "true");
        else if (k == "lowpass_cutoff") mConfig.lowpass_cutoff = std::stof(v);
        else if (k == "debug_logging") mConfig.debug_logging = (v == "true");
        else if (k == "enable_raycast") mConfig.enable_raycast = (v == "true");
        else if (k == "occluded_lowpass_gain") mConfig.occluded_lowpass_gain = std::stof(v);
        else if (k == "raycast_max_distance") mConfig.raycast_max_distance = std::stof(v);
        saveConfig();
    });
    registerModule(b.build());
}

PL_REGISTER_MOD(SoundPhysicsLite)
