// Adapter 类声明
// Adapter
#pragma once

#include "adapter/config.hpp"
#include "adapter/types.hpp"

namespace adapter
{
    class Adapter
    {
      public:
        explicit Adapter(const AdapterConfig &config);
        AdapterOutput adapt(const AdapterInput &input) const;

      private:
        AdapterConfig config_;
    };

} // namespace adapter
