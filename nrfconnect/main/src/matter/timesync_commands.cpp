#include <app/CommandHandler.h>
#include <app/ConcreteCommandPath.h>
#include <app/clusters/time-synchronization-server/time-synchronization-server.h>
#include <protocols/interaction_model/StatusCode.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::Protocols::InteractionModel;

extern "C" bool emberAfTimeSynchronizationClusterSetUTCTimeCallback(
    CommandHandler * commandObj, const ConcreteCommandPath & commandPath,
    const TimeSynchronization::Commands::SetUTCTime::DecodableType & commandData);

extern "C" bool emberAfTimeSynchronizationClusterSetUtcTimeCallback(
    CommandHandler * commandObj, const ConcreteCommandPath & commandPath,
    const TimeSynchronization::Commands::SetUTCTime::DecodableType & commandData)
{
    // Forward legacy lowercase entry point to the shared handler.
    return emberAfTimeSynchronizationClusterSetUTCTimeCallback(commandObj, commandPath, commandData);
}
