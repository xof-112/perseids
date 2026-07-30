#pragma once

// Compatibility shim — Phase 9 moved Blocks 8–9 to spatial_params.h.
#include "spatial_params.h"

namespace perseids
{

using DummyBlockParamId     = SpatialParamId;
using DummyBlockParamValues = SpatialParamValues;

} // namespace perseids
