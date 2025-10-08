/*
 *    Copyright (c) 2022 Project CHIP Authors
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

#pragma once

#include "platform/LEDWidget.h"

#include <array>
#include <utility>

template <uint8_t size>
class FactoryResetLEDsWrapper
{
public:
    using Gpio = uint32_t;
    using Leds = std::array<std::pair<Gpio, LEDWidget>, size>;

    explicit FactoryResetLEDsWrapper(std::array<Gpio, size> leds)
    {
        size_t idx = 0;
        for (const auto & led : leds)
        {
            mLeds[idx++] = std::make_pair(led, LEDWidget());
        }
        Init();
    }

    void Set(bool state)
    {
        for (auto & led : mLeds)
        {
            led.second.Set(state);
        }
    }

    void Blink(uint32_t rateMs)
    {
        for (auto & led : mLeds)
        {
            led.second.Blink(rateMs);
        }
    }

private:
    void Init()
    {
        LEDWidget::InitGpio();
        for (auto & led : mLeds)
        {
            led.second.Init(led.first);
        }
    }

    Leds mLeds;
};
