#if 0
// Turn off for now
uint32_t axpDebugRead()
{
  axp.debugCharging();
  LOG_DEBUG("vbus current %f\n", axp.getVbusCurrent());
  LOG_DEBUG("charge current %f\n", axp.getBattChargeCurrent());
  LOG_DEBUG("bat voltage %f\n", axp.getBattVoltage());
  LOG_DEBUG("batt pct %d\n", axp.getBattPercentage());
  LOG_DEBUG("is battery connected %d\n", axp.isBatteryConnect());
  LOG_DEBUG("is USB connected %d\n", axp.isVBUSPlug());
  LOG_DEBUG("is charging %d\n", axp.isChargeing());

  return 30 * 1000;
}

Periodic axpDebugOutput(axpDebugRead);
axpDebugOutput.setup();
#endif