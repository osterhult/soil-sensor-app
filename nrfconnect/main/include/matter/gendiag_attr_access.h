#pragma once

#include <app/AttributeAccessInterface.h>
#include <app/util/attribute-storage.h>
#include <lib/core/CHIPError.h>
#include <lib/core/Optional.h>

namespace matter
{

class GeneralDiagAttrAccess : public chip::app::AttributeAccessInterface
{
public:
    GeneralDiagAttrAccess();

    CHIP_ERROR Read(const chip::app::ConcreteReadAttributePath & path,
                    chip::app::AttributeValueEncoder & encoder) override;
};

CHIP_ERROR RegisterGeneralDiagAttrAccess();

} // namespace matter
