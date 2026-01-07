#pragma once

// Initialize testing environment.
void initializeTestEnvironment();

// Minimal init without creating SerialConsole or portduino peripherals (useful for lightweight logic tests)
void initializeTestEnvironmentMinimal();