/*
 * Copyright (C) 2021 The Android Open Source Project
 * Copyright (C) 2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <android-base/logging.h>
#include <android/binder_interface_utils.h>
#include <health-impl/Health.h>
#include <health/utils.h>

#include <algorithm>

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/strings.h>

#ifndef CHARGER_FORCE_NO_UI
#define CHARGER_FORCE_NO_UI 0
#endif

#if !CHARGER_FORCE_NO_UI
#include <health-impl/ChargerUtils.h>
#endif

using aidl::android::hardware::health::HalHealthLoop;
using aidl::android::hardware::health::Health;

#if !CHARGER_FORCE_NO_UI
using aidl::android::hardware::health::charger::ChargerCallback;
using aidl::android::hardware::health::charger::ChargerModeMain;
#endif

static constexpr const char* gInstanceName = "default";

namespace aidl::android::hardware::health {
// Some MTK fuel gauge drivers report POWER_SUPPLY_PROP_CHARGE_COUNTER in mAh instead of the
// µAh Android expects (e.g. 2821 at 71% of a 4053 mAh battery), which leaves BatteryStats and
// the Settings battery usage screen with a near-zero charge. Opt in per device with
// ro.vendor.health.charge_counter_in_mah=true to scale it back to µAh.
class MediatekHealth : public Health {
  public:
    using Health::Health;

    // Settings' battery health screen shows batteryStateOfHealth, which the default implementation
    // only reads from a power_supply state_of_health node. MTK fuel gauges don't have one (-> "0 %
    // of original capacity") but do report the full charge and design capacities, so derive it.
    ndk::ScopedAStatus getBatteryHealthData(BatteryHealthData* out) override {
        auto status = Health::getBatteryHealthData(out);
        if (!status.isOk() || out->batteryStateOfHealth > 0) return status;
        HealthInfo info;
        if (getHealthInfo(&info).isOk() && info.batteryFullChargeUah > 0 &&
            info.batteryFullChargeDesignCapacityUah > 0) {
            int64_t soh = static_cast<int64_t>(info.batteryFullChargeUah) * 100 /
                          info.batteryFullChargeDesignCapacityUah;
            out->batteryStateOfHealth = std::clamp<int64_t>(soh, 1, 100);
        }
        return status;
    }

  protected:
    void UpdateHealthInfo(HealthInfo* health_info) override {
        Health::UpdateHealthInfo(health_info);
        if (mScaleChargeCounter && health_info->batteryChargeCounterUah > 0 &&
            health_info->batteryChargeCounterUah < kMaxPlausibleMah) {
            health_info->batteryChargeCounterUah *= 1000;
        }
        if (mStaticUsbMax && health_info->chargerUsbOnline && IsStandardUsbPort()) {
            health_info->maxChargingCurrentMicroamps = kStandardUsbCurrentUa;
            health_info->maxChargingVoltageMicrovolts = kStandardUsbVoltageUv;
        }
    }

  private:
    // Some charger drivers (e.g. Xiaomi hq_chg) report constant current_max/voltage_max for the
    // usb supply whatever is plugged in, so a PC port looks like a 100+ W charger and the lock
    // screen says "Charging rapidly". Opt in with ro.vendor.health.usb_max_is_static=true to
    // report a standard USB port (type USB = SDP/CDP) as 500 mA at 5 V instead.
    static bool IsStandardUsbPort() {
        std::string type;
        if (!::android::base::ReadFileToString(kUsbTypePath, &type)) return false;
        return ::android::base::Trim(type) == "USB";
    }

    // No phone battery holds 100 Ah; anything below this is a mAh value.
    static constexpr int32_t kMaxPlausibleMah = 100000;
    static constexpr const char* kUsbTypePath = "/sys/class/power_supply/usb/type";
    static constexpr int32_t kStandardUsbCurrentUa = 500000;
    static constexpr int32_t kStandardUsbVoltageUv = 5000000;
    const bool mScaleChargeCounter =
            ::android::base::GetBoolProperty("ro.vendor.health.charge_counter_in_mah", false);
    const bool mStaticUsbMax =
            ::android::base::GetBoolProperty("ro.vendor.health.usb_max_is_static", false);
};
}  // namespace aidl::android::hardware::health
static constexpr std::string_view gChargerArg{"--charger"};

#if !CHARGER_FORCE_NO_UI
namespace aidl::android::hardware::health {
class ChargerCallbackImpl : public ChargerCallback {
public:
    using ChargerCallback::ChargerCallback;
    bool ChargerEnableSuspend() override { return true; }
};
} // namespace aidl::android::hardware::health
#endif

int main(int argc, char** argv) {
#ifdef __ANDROID_RECOVERY__
    android::base::InitLogging(argv, android::base::KernelLogger);
#endif

    // make a default health service
    auto config = std::make_unique<healthd_config>();
    ::android::hardware::health::InitHealthdConfig(config.get());
    auto binder = ndk::SharedRefBase::make<aidl::android::hardware::health::MediatekHealth>(
            gInstanceName, std::move(config));

    if (argc >= 2 && argv[1] == gChargerArg) {
#if !CHARGER_FORCE_NO_UI
        // If charger shouldn't have UI for your device, simply drop the line below
        // for your service implementation. This corresponds to
        // ro.charger.no_ui=true
        return ChargerModeMain(binder, std::make_shared<aidl::android::hardware::health::ChargerCallbackImpl>(binder));
#endif

        LOG(INFO) << "Starting charger mode without UI.";
    } else {
        LOG(INFO) << "Starting health HAL.";
    }

    auto hal_health_loop = std::make_shared<HalHealthLoop>(binder, binder);
    return hal_health_loop->StartLoop();
}
