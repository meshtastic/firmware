#pragma once

/**
 * @brief Master include file for all Screen draw renderers
 *
 * This file includes all the individual renderer headers to provide
 * a convenient single include for accessing all draw functions.
 */

#include "graphics/draw/ClockRenderer.h"
#include "graphics/draw/CompassRenderer.h"
#include "graphics/draw/DebugRenderer.h"
#include "graphics/draw/NodeListRenderer.h"
#include "graphics/draw/ScreenRenderer.h"
#include "graphics/draw/UIRenderer.h"

namespace graphics
{

/**
 * @brief Collection of all draw renderers
 *
 * This namespace provides access to all the specialized rendering
 * functions organized by category.
 */
namespace DrawRenderers
{
// Re-export all renderer namespaces for convenience
using namespace ClockRenderer;
using namespace CompassRenderer;
using namespace DebugRenderer;
using namespace NodeListRenderer;
using namespace ScreenRenderer;
using namespace UIRenderer;

} // namespace DrawRenderers

} // namespace graphics
