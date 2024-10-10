#if 0
// Turn off for now
uint32_t axpDebugRead()
{
  axp.debugCharging();
  LOG_DEBUG("vbus current %f", axp.getVbusCurrent());
  LOG_DEBUG("charge current %f", axp.getBattChargeCurrent());
  LOG_DEBUG("bat voltage %f", axp.getBattVoltage());
  LOG_DEBUG("batt pct %d", axp.getBattPercentage());
  LOG_DEBUG("is battery connected %d", axp.isBatteryConnect());
  LOG_DEBUG("is USB connected %d", axp.isVBUSPlug());
  LOG_DEBUG("is charging %d", axp.isChargeing());

  return 30 * 1000;
}

Periodic axpDebugOutput(axpDebugRead);
axpDebugOutput.setup();
#endif