#pragma once

#include "Client/IGameClient.h"

#include <memory>

namespace mrg
{
    // Takes exclusive ownership of the Client and runs the complete engine
    // lifetime.  See Docs/ExecutionFlow.md for the detailed call order.
    int Run(std::unique_ptr<IGameClient> client);
}
