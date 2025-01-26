#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "./UpdateMediator.h"

#include "./WindowManager.h"

using namespace NicheGraphics;

static constexpr uint32_t MAINTENANCE_MS_INITIAL = 60 * 1000UL;
static constexpr uint32_t MAINTENANCE_MS = 60 * 60 * 1000UL;

InkHUD::UpdateMediator::UpdateMediator() : concurrency::OSThread("Mediator")
{
    // Timer disabled by default
    OSThread::disable();
}

// Ask which type of update operation we should perform
// Even if we explicitly want a FAST or FULL update, we should pass it through this method,
// as it allows UpdateMediator to count the refreshes.
// Internal "maintenance" refreshes are not passed through evaluate, however.
Drivers::EInk::UpdateTypes InkHUD::UpdateMediator::evaluate(Drivers::EInk::UpdateTypes requested)
{
    LOG_DEBUG("FULL-update debt:%f", debt);

    // For conveninece
    typedef Drivers::EInk::UpdateTypes UpdateTypes;

    // Check whether we've paid off enough debt to stop unprovoked refreshing (if in progress)
    // This maintenance behavior will also halt itself when the timer next fires,
    // but that could be an hour away, so we can stop it early here and free up resources
    if (OSThread::enabled && debt == 0.0)
        endMaintenance();

    // Explicitly requested FULL
    if (requested == UpdateTypes::FULL) {
        LOG_DEBUG("Explicit FULL");
        debt = max(debt - 1.0, 0.0); // Record that we have paid back (some of) the FULL refresh debt
        return UpdateTypes::FULL;
    }

    // Explicitly requested FAST
    if (requested == UpdateTypes::FAST) {
        LOG_DEBUG("Explicit FAST");
        // Add to the FULL refresh debt
        if (debt < 1.0)
            debt += 1.0 / fastPerFull;
        else
            debt += stressMultiplier * (1.0 / fastPerFull); // More debt if too many consecutive FAST refreshes

        // If *significant debt*, begin occasionally refreshing *unprovoked*
        // This maintenance behavior is only triggered here, during periods of user interaction
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
        if (OSThread::enabled && OSThread::interval == MAINTENANCE_MS_INITIAL) {
            LOG_DEBUG("Initial maintenance skipped");
            OSThread::setInterval(MAINTENANCE_MS); // Note: not intervalFromNow
        }

        return UpdateTypes::FULL;
    }
}

// Determine which of two update types is more important to honor
// Explicit FAST is more important than UNSPECIFIED - prioritize responsiveness
// Explicit FULL is more important than explicint FAST - prioritize image quality: explicit FULL is rare
Drivers::EInk::UpdateTypes InkHUD::UpdateMediator::prioritize(Drivers::EInk::UpdateTypes type1, Drivers::EInk::UpdateTypes type2)
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
// If signifcant FULL-refresh debt has accumulated, we will occasionally run FULL refreshes unprovoked.
// This prevents gradual build-up of debt,
// in case we don't have enough UNSPECIFIED refreshes to pay the debt back organically.
// The first refresh takes place shortly after user finishes interacting with the device; this does the bulk of the restoration
// Subsequent refreshes take place *much* less frequently.
// Hopefully an applet will want to render before this, meaning we can cancel the maintenance.
int32_t InkHUD::UpdateMediator::runOnce()
{
    if (debt > 0.0) {
        LOG_DEBUG("debt=%f: performing maintenance", debt);

        // Ask WindowManager to redraw everything, purely for the refresh
        // Todo: optimize? Could update without re-rendering
        WindowManager::getInstance()->forceUpdate(EInk::UpdateTypes::FULL);

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
void InkHUD::UpdateMediator::beginMaintenance()
{
    LOG_DEBUG("Maintenance enabled");
    OSThread::setIntervalFromNow(MAINTENANCE_MS_INITIAL);
    OSThread::enabled = true;
}

// FULL-refresh debt is low enough that we no longer need to pay it back with periodic updates
int32_t InkHUD::UpdateMediator::endMaintenance()
{
    LOG_DEBUG("Maintenance disabled");
    return OSThread::disable();
}

#endif