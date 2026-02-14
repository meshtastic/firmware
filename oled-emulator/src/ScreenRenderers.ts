/**
 * ScreenRenderers.ts - Sample screen rendering functions
 *
 * These demonstrate how to port Meshtastic firmware UI screens
 * to the OLED emulator. The API matches the firmware's drawing code.
 */

import {
  OLEDDisplay,
  WHITE,
  BLACK,
  TEXT_ALIGN_LEFT,
  TEXT_ALIGN_CENTER,
  TEXT_ALIGN_RIGHT,
} from "./OLEDDisplay";
import { Font_6x8 } from "./SimpleFonts";
import * as images from "./OLEDImages";

// Font height for simple 6x8 font
const FONT_HEIGHT_SMALL = 8;

// Screen resolution detection (matches SharedUIDisplay.cpp)
export type ScreenResolution = "UltraLow" | "Low" | "High";

export function determineScreenResolution(
  width: number,
  height: number,
): ScreenResolution {
  if (width <= 64 || height <= 48) {
    return "UltraLow";
  }
  if (width > 128) {
    return "High";
  }
  return "Low";
}

// Helper for consistent line positioning
export function getTextPositions(display: OLEDDisplay): number[] {
  const fontHeight = FONT_HEIGHT_SMALL;
  return [
    0, // textZeroLine
    fontHeight - 1, // textFirstLine
    fontHeight - 1 + (fontHeight - 5), // textSecondLine
    fontHeight - 1 + 2 * (fontHeight - 5), // textThirdLine
    fontHeight - 1 + 3 * (fontHeight - 5), // textFourthLine
    fontHeight - 1 + 4 * (fontHeight - 5), // textFifthLine
  ];
}

/**
 * Draw a rounded highlight box (used for inverted headers)
 */
export function drawRoundedHighlight(
  display: OLEDDisplay,
  x: number,
  y: number,
  w: number,
  h: number,
  r: number,
): void {
  // Draw the center and side rectangles
  display.fillRect(x + r, y, w - 2 * r, h); // center bar
  display.fillRect(x, y + r, r, h - 2 * r); // left edge
  display.fillRect(x + w - r, y + r, r, h - 2 * r); // right edge

  // Draw the rounded corners using filled circles
  display.fillCircle(x + r + 1, y + r, r); // top-left
  display.fillCircle(x + w - r - 1, y + r, r); // top-right
  display.fillCircle(x + r + 1, y + h - r - 1, r); // bottom-left
  display.fillCircle(x + w - r - 1, y + h - r - 1, r); // bottom-right
}

/**
 * Draw the common header bar (battery, title, icons)
 */
export function drawCommonHeader(
  display: OLEDDisplay,
  x: number,
  y: number,
  titleStr: string,
  options: {
    inverted?: boolean;
    batteryPercent?: number;
    isCharging?: boolean;
    hasUSB?: boolean;
    hasUnreadMessage?: boolean;
  } = {},
): void {
  const {
    inverted = true,
    batteryPercent = 75,
    isCharging = false,
    hasUSB = false,
    hasUnreadMessage = false,
  } = options;

  const HEADER_OFFSET_Y = 1;
  y += HEADER_OFFSET_Y;

  display.setFont(Font_6x8);
  const highlightHeight = FONT_HEIGHT_SMALL - 1;
  const screenW = display.getWidth();

  if (inverted) {
    // Draw inverted header background
    display.setColor(WHITE);
    drawRoundedHighlight(display, x, y, screenW, highlightHeight, 2);
    display.setColor(BLACK);
  } else {
    // Draw line under header
    display.setColor(WHITE);
    display.drawLine(0, 14, screenW, 14);
  }

  // Draw title centered
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(screenW / 2, y, titleStr);

  // Reset color for remaining elements
  if (inverted) {
    display.setColor(WHITE);
  }

  // Draw battery on left
  let batteryX = 1;
  const batteryY = HEADER_OFFSET_Y + 1;

  if (hasUSB && !isCharging) {
    // Draw USB icon
    display.drawXbm(batteryX + 1, batteryY + 2, 10, 8, images.imgUSB);
    batteryX += 11;
  } else {
    // Draw battery outline
    display.drawXbm(batteryX, batteryY, 7, 11, images.batteryBitmap_v);

    if (isCharging) {
      // Draw lightning bolt
      display.drawXbm(
        batteryX + 1,
        batteryY + 3,
        5,
        5,
        images.lightning_bolt_v,
      );
    } else {
      // Draw battery level
      display.drawXbm(
        batteryX - 1,
        batteryY + 4,
        8,
        3,
        images.batteryBitmap_sidegaps_v,
      );
      const fillHeight = Math.floor((8 * batteryPercent) / 100);
      const fillY = batteryY - fillHeight + 10;
      display.fillRect(batteryX + 1, fillY, 5, fillHeight);
    }
    batteryX += 9;
  }

  // Draw mail icon on right if unread message
  if (hasUnreadMessage) {
    const mailX = screenW - images.mail_width - 2;
    display.drawXbm(
      mailX,
      y + 2,
      images.mail_width,
      images.mail_height,
      images.mail,
    );
  }

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setColor(WHITE);
}

/**
 * Draw a boot/splash screen with logo
 */
export function drawBootScreen(
  display: OLEDDisplay,
  appVersion: string = "2.5.0",
): void {
  display.clear();
  display.setFont(Font_6x8);
  display.setColor(WHITE);

  const centerX = display.getWidth() / 2;
  const centerY = display.getHeight() / 2;

  // Draw logo centered
  const logoX = centerX - images.logo_width / 2;
  const logoY = centerY - images.logo_height / 2 - 8;
  display.drawXbm(
    logoX,
    logoY,
    images.logo_width,
    images.logo_height,
    images.meshtastic_logo,
  );

  // Draw version below
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(
    centerX,
    centerY + images.logo_height / 2,
    `v${appVersion}`,
  );

  display.setTextAlignment(TEXT_ALIGN_LEFT);
}

/**
 * Draw a node info screen
 */
export function drawNodeInfoScreen(
  display: OLEDDisplay,
  nodeInfo: {
    shortName: string;
    longName: string;
    nodeId: string;
    batteryLevel?: number;
    lastHeard?: string;
    snr?: number;
    hopsAway?: number;
  },
): void {
  display.clear();

  // Draw header
  drawCommonHeader(display, 0, 0, "Node Info", {
    inverted: true,
    batteryPercent: 85,
  });

  display.setFont(Font_6x8);
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_LEFT);

  const lines = getTextPositions(display);
  const screenW = display.getWidth();

  // Node name (bolded by drawing twice offset)
  display.drawString(0, lines[1], nodeInfo.longName);
  display.drawString(1, lines[1], nodeInfo.longName);

  // Short name and ID
  display.drawString(0, lines[2], `${nodeInfo.shortName} • ${nodeInfo.nodeId}`);

  // Last heard
  if (nodeInfo.lastHeard) {
    display.drawString(0, lines[3], `Heard: ${nodeInfo.lastHeard}`);
  }

  // Signal info on right side
  if (nodeInfo.snr !== undefined) {
    const snrStr = `SNR: ${nodeInfo.snr}dB`;
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(screenW, lines[3], snrStr);
  }

  // Hops
  if (nodeInfo.hopsAway !== undefined) {
    const hopStr =
      nodeInfo.hopsAway === 0 ? "Direct" : `${nodeInfo.hopsAway} hops`;
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, lines[4], hopStr);
  }

  display.setTextAlignment(TEXT_ALIGN_LEFT);
}

/**
 * Draw a message screen
 */
export function drawMessageScreen(
  display: OLEDDisplay,
  message: {
    from: string;
    text: string;
    time: string;
    channel?: string;
  },
): void {
  display.clear();

  // Draw header with channel name if provided
  const headerTitle = message.channel || "Message";
  drawCommonHeader(display, 0, 0, headerTitle, {
    inverted: true,
    batteryPercent: 72,
    hasUnreadMessage: true,
  });

  display.setFont(Font_6x8);
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_LEFT);

  const lines = getTextPositions(display);
  const screenW = display.getWidth();

  // From line
  display.drawString(0, lines[1], `From: ${message.from}`);

  // Time on right
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(screenW, lines[1], message.time);

  // Message text (with word wrap)
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawStringMaxWidth(0, lines[2], screenW, message.text);
}

/**
 * Draw a GPS/location screen
 */
export function drawGPSScreen(
  display: OLEDDisplay,
  gpsInfo: {
    latitude?: number;
    longitude?: number;
    altitude?: number;
    satellites?: number;
    hasLock: boolean;
    speed?: number;
  },
): void {
  display.clear();

  drawCommonHeader(display, 0, 0, "GPS", {
    inverted: true,
    batteryPercent: 90,
  });

  display.setFont(Font_6x8);
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_LEFT);

  const lines = getTextPositions(display);

  // Draw satellite icon and count
  display.drawXbm(
    0,
    lines[1],
    images.imgSatellite_width,
    images.imgSatellite_height,
    images.imgSatellite,
  );

  if (!gpsInfo.hasLock) {
    display.drawString(12, lines[1], "No Lock");
  } else {
    display.drawString(12, lines[1], `${gpsInfo.satellites || 0} sats`);
  }

  if (
    gpsInfo.hasLock &&
    gpsInfo.latitude !== undefined &&
    gpsInfo.longitude !== undefined
  ) {
    // Coordinates
    display.drawString(0, lines[2], `Lat: ${gpsInfo.latitude.toFixed(6)}`);
    display.drawString(0, lines[3], `Lon: ${gpsInfo.longitude.toFixed(6)}`);

    // Altitude
    if (gpsInfo.altitude !== undefined) {
      display.drawString(0, lines[4], `Alt: ${gpsInfo.altitude.toFixed(0)}m`);
    }

    // Speed
    if (gpsInfo.speed !== undefined) {
      const screenW = display.getWidth();
      display.setTextAlignment(TEXT_ALIGN_RIGHT);
      display.drawString(screenW, lines[4], `${gpsInfo.speed.toFixed(1)} km/h`);
    }
  }

  display.setTextAlignment(TEXT_ALIGN_LEFT);
}

/**
 * Draw a node list screen
 */
export function drawNodeListScreen(
  display: OLEDDisplay,
  nodes: Array<{
    shortName: string;
    longName: string;
    lastHeard: string;
    snr?: number;
  }>,
  selectedIndex: number = 0,
): void {
  display.clear();

  drawCommonHeader(display, 0, 0, `Nodes (${nodes.length})`, {
    inverted: true,
    batteryPercent: 80,
  });

  display.setFont(Font_6x8);
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_LEFT);

  const lines = getTextPositions(display);
  const screenW = display.getWidth();
  const maxVisibleNodes = 4;
  const startIndex = Math.max(
    0,
    selectedIndex - Math.floor(maxVisibleNodes / 2),
  );

  for (let i = 0; i < maxVisibleNodes && startIndex + i < nodes.length; i++) {
    const node = nodes[startIndex + i];
    const lineY = lines[i + 1];
    const isSelected = startIndex + i === selectedIndex;

    // Highlight selected row
    if (isSelected) {
      display.fillRect(0, lineY - 1, screenW, FONT_HEIGHT_SMALL);
      display.setColor(BLACK);
    }

    // Node short name
    display.drawString(0, lineY, node.shortName);

    // Last heard on right
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(screenW, lineY, node.lastHeard);
    display.setTextAlignment(TEXT_ALIGN_LEFT);

    if (isSelected) {
      display.setColor(WHITE);
    }
  }
}

/**
 * Draw a system info screen
 */
export function drawSystemScreen(
  display: OLEDDisplay,
  sysInfo: {
    uptime: string;
    channelUtil: number;
    airUtil: number;
    batteryVoltage?: number;
    nodes: number;
    freeMemory?: number;
  },
): void {
  display.clear();

  drawCommonHeader(display, 0, 0, "System", {
    inverted: true,
    batteryPercent: 95,
  });

  display.setFont(Font_6x8);
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_LEFT);

  const lines = getTextPositions(display);
  const screenW = display.getWidth();

  // Uptime
  display.drawString(0, lines[1], `Uptime: ${sysInfo.uptime}`);

  // Channel utilization
  display.drawString(0, lines[2], `ChUtil: ${sysInfo.channelUtil.toFixed(1)}%`);

  // Air utilization
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(
    screenW,
    lines[2],
    `AirTx: ${sysInfo.airUtil.toFixed(1)}%`,
  );
  display.setTextAlignment(TEXT_ALIGN_LEFT);

  // Nodes
  display.drawString(0, lines[3], `Nodes: ${sysInfo.nodes}`);

  // Battery voltage
  if (sysInfo.batteryVoltage !== undefined) {
    display.setTextAlignment(TEXT_ALIGN_RIGHT);
    display.drawString(
      screenW,
      lines[3],
      `${sysInfo.batteryVoltage.toFixed(2)}V`,
    );
  }

  // Free memory
  if (sysInfo.freeMemory !== undefined) {
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(
      0,
      lines[4],
      `Free: ${(sysInfo.freeMemory / 1024).toFixed(0)}KB`,
    );
  }

  display.setTextAlignment(TEXT_ALIGN_LEFT);
}

/**
 * Draw a compass screen with bearing arrow
 */
export function drawCompassScreen(
  display: OLEDDisplay,
  compassInfo: {
    heading: number; // Device heading in degrees
    bearing: number; // Bearing to target in degrees
    distance?: number; // Distance to target in meters
    targetName?: string;
  },
): void {
  display.clear();

  drawCommonHeader(display, 0, 0, compassInfo.targetName || "Compass", {
    inverted: true,
    batteryPercent: 70,
  });

  display.setFont(Font_6x8);
  display.setColor(WHITE);

  const screenW = display.getWidth();
  const screenH = display.getHeight();

  // Compass center and radius
  const compassRadius = Math.min(screenW, screenH - 20) / 2 - 4;
  const compassX = screenW / 2;
  const compassY = (screenH + 16) / 2;

  // Draw compass circle
  display.drawCircle(compassX, compassY, compassRadius);

  // Draw cardinal directions
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(compassX, compassY - compassRadius - 10, "N");

  // Calculate relative bearing (bearing - heading)
  const relativeBearing =
    ((compassInfo.bearing - compassInfo.heading) * Math.PI) / 180;

  // Draw bearing arrow
  const arrowLength = compassRadius - 6;
  const arrowX = compassX + Math.sin(relativeBearing) * arrowLength;
  const arrowY = compassY - Math.cos(relativeBearing) * arrowLength;

  display.drawLine(compassX, compassY, arrowX, arrowY);

  // Draw arrowhead
  const headAngle = 0.4;
  const headLength = 8;
  const angle1 = relativeBearing + Math.PI - headAngle;
  const angle2 = relativeBearing + Math.PI + headAngle;
  display.drawLine(
    arrowX,
    arrowY,
    arrowX + Math.sin(angle1) * headLength,
    arrowY - Math.cos(angle1) * headLength,
  );
  display.drawLine(
    arrowX,
    arrowY,
    arrowX + Math.sin(angle2) * headLength,
    arrowY - Math.cos(angle2) * headLength,
  );

  // Draw distance at bottom
  if (compassInfo.distance !== undefined) {
    let distStr: string;
    if (compassInfo.distance >= 1000) {
      distStr = `${(compassInfo.distance / 1000).toFixed(1)} km`;
    } else {
      distStr = `${Math.round(compassInfo.distance)} m`;
    }
    display.drawString(compassX, screenH - FONT_HEIGHT_SMALL, distStr);
  }

  display.setTextAlignment(TEXT_ALIGN_LEFT);
}

/**
 * Draw a progress/loading screen
 */
export function drawProgressScreen(
  display: OLEDDisplay,
  title: string,
  progress: number,
  statusText?: string,
): void {
  display.clear();
  display.setFont(Font_6x8);
  display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_CENTER);

  const screenW = display.getWidth();
  const screenH = display.getHeight();
  const centerX = screenW / 2;
  const centerY = screenH / 2;

  // Title
  display.drawString(centerX, centerY - 20, title);

  // Progress bar
  const barWidth = screenW - 20;
  const barHeight = 10;
  const barX = 10;
  const barY = centerY - barHeight / 2;

  display.drawProgressBar(barX, barY, barWidth, barHeight, progress);

  // Progress percentage
  display.drawString(centerX, barY + barHeight + 4, `${Math.round(progress)}%`);

  // Status text
  if (statusText) {
    display.drawString(centerX, screenH - FONT_HEIGHT_SMALL - 2, statusText);
  }

  display.setTextAlignment(TEXT_ALIGN_LEFT);
}
