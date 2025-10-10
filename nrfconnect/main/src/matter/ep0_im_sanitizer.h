#pragma once

#include <app/AttributeAccessInterface.h>
#include <app/ConcreteAttributePath.h>
#include <lib/core/Optional.h>

namespace matter {
namespace ep0 {

class AttrListSanitizer : public chip::app::AttributeAccessInterface
{
public:
    explicit AttrListSanitizer(chip::ClusterId clusterId);

    CHIP_ERROR Read(const chip::app::ConcreteReadAttributePath & aPath,
                    chip::app::AttributeValueEncoder & aEncoder) override;

private:
    bool IsEp0(const chip::app::ConcreteReadAttributePath & aPath) const;

    chip::ClusterId mClusterId;
};

void Register();

} // namespace ep0
} // namespace matter
