export interface Device {
  serial_number: string;
  node_num: number | null;
  friendly_name: string | null;
  hw_model: string | null;
  vid: string | null;
  pid: string | null;
  role: string | null;
  current_port: string | null;
  firmware_version: string | null;
  region: string | null;
  env: string | null;
  env_locked: number;
  flashed_fw_branch: string | null;
  flashed_fw_sha: string | null;
  flashed_at: number | null;
  hub_location: string | null;
  hub_port: number | null;
  online: number;
  first_seen: number;
  last_seen: number;
  has_stable_id: boolean;
  stale: boolean;
}

export interface Camera {
  id: number;
  name: string;
  type: string;
  device_index: string | null;
  backend: string | null;
  rotation: number;
  mirror: number;
  enabled: number;
  created_at: number;
  device_serial: string | null;
  assigned_at: number | null;
  deleted?: boolean;
}

export interface FirmwareRef {
  available: boolean;
  branch?: string | null;
  sha?: string | null;
  short_sha?: string | null;
  dirty?: boolean | null;
  subject?: string | null;
  committed_at?: string | null;
}

export interface TestLeaf {
  nodeid: string;
  tier: string;
  file: string;
  testname: string;
  outcome: string; // pending | running | passed | failed | skipped
  duration?: number | null;
}

export interface TestRun {
  id: number;
  started_at: number;
  finished_at: number | null;
  exit_code: number | null;
  fw_branch: string | null;
  fw_sha: string | null;
  passed: number;
  failed: number;
  skipped: number;
}
