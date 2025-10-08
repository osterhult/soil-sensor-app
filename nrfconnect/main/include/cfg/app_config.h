#pragma once

#include <lib/core/CHIPError.h>

namespace cfg
{
namespace app_config
{

int InitSettings();
void LoadSettingsIfEnabled();
void ConfigureBasicInformation();

} // namespace app_config
} // namespace cfg

