#include "System/ComApartment.h"

#include <stdexcept>

namespace mrg::platform
{
    ComApartment::ComApartment(const DWORD concurrencyModel)
    {
        result_ = CoInitializeEx(nullptr, concurrencyModel);
        if (FAILED(result_) && result_ != RPC_E_CHANGED_MODE)
        {
            throw std::runtime_error("CoInitializeEx failed.");
        }
    }

    ComApartment::~ComApartment()
    {
        if (SUCCEEDED(result_))
        {
            CoUninitialize();
        }
    }
}
