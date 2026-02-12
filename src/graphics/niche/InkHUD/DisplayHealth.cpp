#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./DisplayHealth.h"
#include "DisplayHealth.h"

using namespace NicheGraphics;

// Timing for "maintenance"
// Paying off full-refresh debt with unprovoked updates, if the display is not very active
static constexpr uint32_t MAINTENANCE_MS_INITIAL = 60 * 1000UL;
static constexpr uint32_t MAINTENANCE_MS = 60 * 60 * 1000UL;

InkHUD::DisplayHealth::DisplayHealth() : concurrency::OSThread("Mediator")
{
    // Timer disabled by default
    OSThread::disable();
}

// Request which update type we would prefer, when the display image next changes
// DisplayHealth class will consider our suggestion, and weigh it against other requests
void InkHUD::DisplayHealth::requestUpdateType(Drivers::EInk::UpdateTypes type)
{
    // Update our "working decision", to decide if this request is important enough to change our plan
    if (!forced)
        workingDecision = prioritize(workingDecision, type);
}

// Demand that a specific update type be used, when the display image next changes
// Note: multiple DisplayHealth::force calls should not be made,
// but if they are, the importance of the type will be weighed the same as if both calls were to DisplayHealth::request
void InkHUD::DisplayHealth::forceUpdateType(Drivers::EInk::UpdateTypes type)
{
    if (!forced)
        workingDecision = type;
    else
        workingDecision = prioritize(workingDecision, type);

    forced = true;
}

// Find out which update type the DisplayHealth has chosen for us
// Calling this method consumes the result, and resets for the next update
Drivers::EInk::UpdateTypes InkHUD::DisplayHealth::decideUpdateType()
{
    LOG_DEBUG("FULL-update debt:%f", debt);

    // For convenience
    typedef Drivers::EInk::UpdateTypes UpdateTypes;

    // Grab our final decision for the update type, so we can reset now, for the next update
    // We do this at top of the method, so we can return early
    UpdateTypes finalDecision = workingDecision;
    workingDecision = UpdateTypes::UNSPECIFIED;
    forced = false;

    // Check whether we've paid off enough debt to stop unprovoked refreshing (if in progress)
    // This maintenance behavior will also have opportunity to halt itself when the timer next fires,
    // but that could be an hour away, so we can stop it early here and free up resources
    if (OSThread::enabled && debt == 0.0)
        endMaintenance();

    // Explicitly requested FULL
    if (finalDecision == UpdateTypes::FULL) {
        LOG_DEBUG("Explicit FULL");
        debt = max(debt - 1.0, 0.0); // Record that we have paid back (some of) the FULL refresh debt
        return UpdateTypes::FULL;
    }

    // Explicitly requested FAST
    if (finalDecision == UpdateTypes::FAST) {
        LOG_DEBUG("Explicit FAST");
        // Add to the FULL refresh debt
        if (debt < 1.0)
            debt += 1.0 / fastPerFull;
        else
            debt += stressMultiplier * (1.0 / fastPerFull); // More debt if too many consecutive FAST refreshes

        // If *significant debt*, begin occasionally refreshing *unprovoked*
        // This maintenance behavior is only triggered here, by periods of user interaction
        // Debt would otherwise not be able to climb above 1.0
        if (debt >= 2.0)
            beginMaintenance();

        return UpdateTypes::FAST; // Give them what the asked for
    }

    // Handling UpdateTypes::UNSPECIFIED
    // -----------------------------------
    // In this case, the UI doesn't care which refresh we use

    // Not much debt: suggest FAST
    if (debt < 1.0) {
        LOG_DEBUG("UNSPECIFIED: using FAST");
        debt += 1.0 / fastPerFull;
        return UpdateTypes::FAST;
    }

    // In debt: suggest FULL
    else {
        LOG_DEBUG("UNSPECIFIED: using FULL");
        debt = max(debt - 1.0, 0.0); // Record that we have paid back (some of) the FULL refresh debt

        // When maintenance begins, the first refresh happens shortly after user interaction ceases (a minute or so)
        // If we *are* given an opportunity to refresh before that, we'll skip that initial maintenance refresh
        // We were intending to use that initial refresh to redraw the screen as FULL, but we're doing that now, organically
        if (OSThread::enabled && OSThread::interval == MAINTENANCE_MS_INITIAL)
            OSThread::setInterval(MAINTENANCE_MS); // Note: not intervalFromNow

        return UpdateTypes::FULL;
    }
}

// Determine which of two update types is more important to honor
// Explicit FAST is more important than UNSPECIFIED - prioritize responsiveness
// Explicit FULL is more important than explicit FAST - prioritize image quality: explicit FULL is rare
// Used when multiple applets have all requested update simultaneously, each with their own preferred UpdateType
Drivers::EInk::UpdateTypes InkHUD::DisplayHealth::prioritize(Drivers::EInk::UpdateTypes type1, Drivers::EInk::UpdateTypes type2)
{
    switch (type1) {
    case Drivers::EInk::UpdateTypes::UNSPECIFIED:
        return type2;

    case Drivers::EInk::UpdateTypes::FAST:
        return (type2 == Drivers::EInk::UpdateTypes::FULL) ? Drivers::EInk::UpdateTypes::FULL : Drivers::EInk::UpdateTypes::FAST;

    case Drivers::EInk::UpdateTypes::FULL:
        return type1;
    }

    return Drivers::EInk::UpdateTypes::UNSPECIFIED; // Suppress compiler warning only
}

// We're using the timer to perform "maintenance"
// If significant FULL-refresh debt has accumulated, we will occasionally run FULL refreshes unprovoked.
// This prevents gradual build-up of debt,
// in case we aren't doing enough UNSPECIFIED refreshes to pay the debt back organically.
// The first refresh takes place shortly after user finishes interacting with the device; this does the bulk of the restoration
// Subsequent refreshes take place *much* less frequently.
// Hopefully an applet will want to render before this, meaning we can cancel the maintenance.
int32_t InkHUD::DisplayHealth::runOnce()
{
    if (debt > 0.0) {
        LOG_DEBUG("debt=%f: performing maintenance", debt);

        // Ask WindowManager to redraw everything, purely for the refresh
        // Todo: optimize? Could update without re-rendering
        InkHUD::getInstance()->forceUpdate(Drivers::EInk::UpdateTypes::FULL);

        // Record that we have paid back (some of) the FULL refresh debt
        debt = max(debt - 1.0, 0.0);

        // Next maintenance refresh scheduled - long wait (an hour?)
        return MAINTENANCE_MS;
    }

    else
        return endMaintenance();
}

// Begin periodically refreshing the display, to repay FULL-refresh debt
// We do this in case user doesn't have enough activity to repay it organically, with UpdateTypes::UNSPECIFIED
// After an initial refresh, to redraw as FULL, we only perform these maintenance refreshes very infrequently
// This gives the display a chance to heal by evaluating UNSPECIFIED as FULL, which is preferable
void InkHUD::DisplayHealth::beginMaintenance()
{
    OSThread::setIntervalFromNow(MAINTENANCE_MS_INITIAL);
    OSThread::enabled = true;
}

// FULL-refresh debt is low enough that we no longer need to pay it back with periodic updates
int32_t InkHUD::DisplayHealth::endMaintenance()
{
    return OSThread::disable();
}

#endif