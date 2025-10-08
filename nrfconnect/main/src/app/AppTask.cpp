/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "app/AppTask.h"

#include "AppConfig.h"
#include "app/AppEvent.h"
#include "LEDUtil.h"

#include <app/server/Server.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/ConnectivityManager.h>
#include <system/SystemError.h>

#include <dk_buttons_and_leds.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(soil_app, LOG_LEVEL_INF);

using namespace ::chip;
using namespace ::chip::DeviceLayer;

namespace
{

constexpr uint32_t kFactoryResetTriggerTimeout      = 3000;
constexpr uint32_t kFactoryResetCancelWindowTimeout = 3000;
constexpr size_t kAppEventQueueSize = 10;
constexpr size_t kAppTaskStackSize  = 2048;
constexpr int kAppTaskPriority      = K_PRIO_PREEMPT(5);

namespace LedConsts
{
constexpr uint32_t kBlinkRateMs{ 500 };
namespace StatusLed
{
namespace Provisioned
{
constexpr uint32_t kOnMs{ 50 };
constexpr uint32_t kOffMs{ 950 };
} // namespace Provisioned

namespace Unprovisioned
{
constexpr uint32_t kOnMs{ 100 };
constexpr uint32_t kOffMs{ 100 };
} // namespace Unprovisioned
} // namespace StatusLed
} // namespace LedConsts

K_MSGQ_DEFINE(sAppEventQueue, sizeof(AppEvent), kAppEventQueueSize, alignof(AppEvent));
k_timer sFunctionTimer;

LEDWidget sStatusLED;

#if defined(FACTORY_RESET_SIGNAL_LED) && defined(FACTORY_RESET_SIGNAL_LED1)
FactoryResetLEDsWrapper<2> sFactoryResetLEDs{ { FACTORY_RESET_SIGNAL_LED, FACTORY_RESET_SIGNAL_LED1 } };
#define HAS_FACTORY_RESET_LEDS 1
#elif defined(FACTORY_RESET_SIGNAL_LED)
FactoryResetLEDsWrapper<1> sFactoryResetLEDs{ { FACTORY_RESET_SIGNAL_LED } };
#define HAS_FACTORY_RESET_LEDS 1
#else
#define HAS_FACTORY_RESET_LEDS 0
#endif

bool sIsNetworkProvisioned = false;
bool sIsNetworkEnabled     = false;
bool sHaveBLEConnections   = false;

K_THREAD_STACK_DEFINE(sAppTaskStackArea, kAppTaskStackSize);
struct k_thread sAppTaskThread;

} // namespace

CHIP_ERROR AppTask::Init()
{
#ifdef CONFIG_STATE_LEDS
    LEDWidget::InitGpio();
    LEDWidget::SetStateUpdateCallback(LEDStateUpdateHandler);

    sStatusLED.Init(SYSTEM_STATE_LED);
#if HAS_FACTORY_RESET_LEDS
    sFactoryResetLEDs.Set(false);
#endif
    UpdateStatusLED();
#endif

    k_timer_init(&sFunctionTimer, &AppTask::FunctionTimerTimeoutCallback, nullptr);
    k_timer_user_data_set(&sFunctionTimer, this);

    int ret = dk_buttons_init(ButtonEventHandler);
    if (ret != 0)
    {
        LOG_ERR("dk_buttons_init() failed: %d", ret);
        return System::MapErrorZephyr(ret);
    }

    PlatformMgr().AddEventHandler(ChipEventHandler, 0);

    return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartApp()
{
    ReturnErrorOnFailure(Init());

    k_tid_t tid = k_thread_create(&sAppTaskThread, sAppTaskStackArea, K_THREAD_STACK_SIZEOF(sAppTaskStackArea), AppTaskMain,
                                  this, nullptr, nullptr, kAppTaskPriority, 0, K_NO_WAIT);
    if (tid == nullptr)
    {
        return CHIP_ERROR_INTERNAL;
    }

    k_thread_name_set(&sAppTaskThread, "app_task");
    return CHIP_NO_ERROR;
}

void AppTask::AppTaskMain(void * pv, void *, void *)
{
    ARG_UNUSED(pv);

    AppEvent event;
    while (k_msgq_get(&sAppEventQueue, &event, K_FOREVER) == 0)
    {
        DispatchEvent(event);
    }
}

void AppTask::PostEvent(const AppEvent & event)
{
    if (k_msgq_put(&sAppEventQueue, &event, K_NO_WAIT) != 0)
    {
        LOG_WRN("AppTask event queue full");
    }
}

void AppTask::DispatchEvent(const AppEvent & event)
{
    if (event.Handler)
    {
        event.Handler(event);
    }
    else
    {
        LOG_WRN("Dropping AppEvent without handler");
    }
}

void AppTask::FunctionTimerEventHandler(const AppEvent & event)
{
    if (event.Type != AppEventType::Timer)
    {
        return;
    }

    if (Instance().mFunctionTimerActive && Instance().mFunction == FunctionEvent::SoftwareUpdate)
    {
        LOG_INF("Factory Reset Triggered. Release button within %ums to cancel.", kFactoryResetCancelWindowTimeout);

        StartTimer(kFactoryResetCancelWindowTimeout);
        Instance().mFunction = FunctionEvent::FactoryReset;

#ifdef CONFIG_STATE_LEDS
        sStatusLED.Set(false);
#if HAS_FACTORY_RESET_LEDS
        sFactoryResetLEDs.Set(false);
        sFactoryResetLEDs.Blink(LedConsts::kBlinkRateMs);
#else
        sStatusLED.Blink(LedConsts::kBlinkRateMs);
#endif
#endif
    }
    else if (Instance().mFunctionTimerActive && Instance().mFunction == FunctionEvent::FactoryReset)
    {
        Instance().mFunction = FunctionEvent::NoneSelected;
        LOG_INF("Factory Reset triggered");
        Server::GetInstance().ScheduleFactoryReset();
    }
}

void AppTask::FunctionHandler(const AppEvent & event)
{
    if (event.ButtonEvent.PinNo != FUNCTION_BUTTON)
    {
        return;
    }

    if (event.ButtonEvent.Action == static_cast<uint8_t>(AppEventType::ButtonPushed))
    {
        if (!Instance().mFunctionTimerActive && Instance().mFunction == FunctionEvent::NoneSelected)
        {
            StartTimer(kFactoryResetTriggerTimeout);
            Instance().mFunction = FunctionEvent::SoftwareUpdate;
        }
    }
    else
    {
        if (Instance().mFunctionTimerActive && Instance().mFunction == FunctionEvent::SoftwareUpdate)
        {
            CancelTimer();
            Instance().mFunction = FunctionEvent::NoneSelected;
            LOG_INF("Factory Reset trigger canceled before timeout");
        }
        else if (Instance().mFunctionTimerActive && Instance().mFunction == FunctionEvent::FactoryReset)
        {
#ifdef CONFIG_STATE_LEDS
#if HAS_FACTORY_RESET_LEDS
            sFactoryResetLEDs.Set(false);
#else
            sStatusLED.Set(false);
#endif
            UpdateStatusLED();
#endif
            CancelTimer();
            Instance().mFunction = FunctionEvent::NoneSelected;
            LOG_INF("Factory Reset has been Canceled");
        }
        else if (Instance().mFunctionTimerActive)
        {
            CancelTimer();
            Instance().mFunction = FunctionEvent::NoneSelected;
        }
    }
}

void AppTask::ButtonEventHandler(uint32_t buttonState, uint32_t hasChanged)
{
    AppEvent event;
    event.Type = AppEventType::Button;

    if (FUNCTION_BUTTON_MASK & hasChanged)
    {
        event.ButtonEvent.PinNo = FUNCTION_BUTTON;
        event.ButtonEvent.Action = static_cast<uint8_t>((FUNCTION_BUTTON_MASK & buttonState) ? AppEventType::ButtonPushed
                                                                                            : AppEventType::ButtonReleased);
        event.Handler = FunctionHandler;
        PostEvent(event);
    }
}

void AppTask::LEDStateUpdateHandler(LEDWidget & ledWidget)
{
    AppEvent event;
    event.Type                       = AppEventType::UpdateLedState;
    event.UpdateLedStateEvent.LedWidget = &ledWidget;
    event.Handler                    = [](const AppEvent & e) {
        if (e.UpdateLedStateEvent.LedWidget)
        {
            e.UpdateLedStateEvent.LedWidget->UpdateState();
        }
    };
    PostEvent(event);
}

void AppTask::FunctionTimerTimeoutCallback(k_timer * timer)
{
    if (timer == nullptr)
    {
        return;
    }

    AppEvent event;
    event.Type               = AppEventType::Timer;
    event.TimerEvent.Context = k_timer_user_data_get(timer);
    event.Handler            = FunctionTimerEventHandler;
    PostEvent(event);
}

void AppTask::StartTimer(uint32_t timeoutInMs)
{
    Instance().mFunctionTimerActive = true;
    k_timer_start(&sFunctionTimer, K_MSEC(timeoutInMs), K_NO_WAIT);
}

void AppTask::CancelTimer()
{
    Instance().mFunctionTimerActive = false;
    k_timer_stop(&sFunctionTimer);
}

void AppTask::UpdateStatusLED()
{
#ifdef CONFIG_STATE_LEDS
    if (sIsNetworkProvisioned && sIsNetworkEnabled)
    {
        sStatusLED.Set(true);
    }
    else if (sHaveBLEConnections)
    {
        sStatusLED.Blink(LedConsts::StatusLed::Unprovisioned::kOnMs, LedConsts::StatusLed::Unprovisioned::kOffMs);
    }
    else
    {
        sStatusLED.Blink(LedConsts::StatusLed::Provisioned::kOnMs, LedConsts::StatusLed::Provisioned::kOffMs);
    }
#endif
}

void AppTask::ChipEventHandler(const ChipDeviceEvent * event, intptr_t)
{
    switch (event->Type)
    {
    case DeviceEventType::kCHIPoBLEConnectionEstablished:
    case DeviceEventType::kCHIPoBLEConnectionClosed:
        sHaveBLEConnections = ConnectivityMgr().NumBLEConnections() != 0;
        UpdateStatusLED();
        break;
    case DeviceEventType::kCHIPoBLEAdvertisingChange:
        sHaveBLEConnections = ConnectivityMgr().NumBLEConnections() != 0;
        UpdateStatusLED();
        break;
    case DeviceEventType::kWiFiConnectivityChange:
        sIsNetworkProvisioned = ConnectivityMgr().IsWiFiStationProvisioned();
        sIsNetworkEnabled     = ConnectivityMgr().IsWiFiStationConnected();
        UpdateStatusLED();
        break;
    case DeviceEventType::kThreadStateChange:
        sIsNetworkProvisioned = ConnectivityMgr().IsThreadProvisioned();
        sIsNetworkEnabled     = ConnectivityMgr().IsThreadEnabled();
        UpdateStatusLED();
        break;
    default:
        break;
    }
}
