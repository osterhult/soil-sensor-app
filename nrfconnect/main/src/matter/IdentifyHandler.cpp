#include "matter/IdentifyHandler.h"

#include "AppConfig.h"
#include "platform/LEDWidget.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/clusters/identify-server/identify-server.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(identify, LOG_LEVEL_INF);

namespace
{
using namespace chip;
using namespace chip::app;

constexpr EndpointId kSoilEndpoint = 1;
constexpr uint32_t kBlinkOnMs      = 250;
constexpr uint32_t kBlinkOffMs     = 250;

#if CONFIG_STATE_LEDS
LEDWidget sIdentifyLED;
#endif

Protocols::InteractionModel::Status ReadIdentifyTime(EndpointId endpoint, uint16_t & outValue)
{
    return chip::app::Clusters::Identify::Attributes::IdentifyTime::Get(endpoint, &outValue);
}

void OnIdentifyStart(::Identify * identify)
{
    uint16_t identifyTime = 0;
    if (ReadIdentifyTime(identify->mEndpoint, identifyTime) != Protocols::InteractionModel::Status::Success)
    {
        LOG_WRN("Identify START ep=%u (time read failed)", static_cast<unsigned>(identify->mEndpoint));
    }
    else
    {
        LOG_INF("Identify START ep=%u time=%u", static_cast<unsigned>(identify->mEndpoint), identifyTime);
    }

#if CONFIG_STATE_LEDS
    sIdentifyLED.Blink(kBlinkOnMs, kBlinkOffMs);
#endif
}

void OnIdentifyStop(::Identify * identify)
{
#if CONFIG_STATE_LEDS
    sIdentifyLED.Set(false);
#endif

    uint16_t identifyTime = 0;
    if (ReadIdentifyTime(identify->mEndpoint, identifyTime) != Protocols::InteractionModel::Status::Success)
    {
        LOG_INF("Identify STOP ep=%u time=(unavailable)", static_cast<unsigned>(identify->mEndpoint));
    }
    else
    {
        LOG_INF("Identify STOP ep=%u time=%u", static_cast<unsigned>(identify->mEndpoint), identifyTime);
    }
}

void OnIdentifyEffect(::Identify * identify)
{
    LOG_INF("Identify EFFECT ep=%u id=%u variant=%u", static_cast<unsigned>(identify->mEndpoint),
            static_cast<unsigned>(identify->mTargetEffectIdentifier), static_cast<unsigned>(identify->mEffectVariant));
}

::Identify sIdentify{ kSoilEndpoint, OnIdentifyStart, OnIdentifyStop,
                      chip::app::Clusters::Identify::IdentifyTypeEnum::kVisibleIndicator, OnIdentifyEffect };

} // namespace

void IdentifyHandler_Init()
{
#if CONFIG_STATE_LEDS
    sIdentifyLED.Init(IDENTIFY_STATE_LED);
    sIdentifyLED.Set(false);
    LOG_INF("Identify LED ready on GPIO %u", IDENTIFY_STATE_LED);
#else
    LOG_WRN("CONFIG_STATE_LEDS disabled; Identify LED callbacks limited to logging");
#endif
}
