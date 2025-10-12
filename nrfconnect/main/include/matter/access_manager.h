#pragma once

#include <lib/core/CHIPError.h>

namespace chip
{
class Server;
} // namespace chip

namespace matter
{
namespace access_manager
{

CHIP_ERROR InitManagementClusters();
void EnsureAccessControlReady();
void AssertRootAccessControlReady();
void InitializeFabricHandlers(chip::Server & server);
CHIP_ERROR DoFullMatterWipe();
void OpenCommissioningWindowIfNeeded(chip::Server & server);

} // namespace access_manager
} // namespace matter
