#!/usr/bin/env python3
"""
Simple ELRS to Meshtastic Target Converter

Converts ELRS target definitions to Meshtastic variant.h files.
"""

# TODO: - Add support for amplifier chips
# TODO: - Clean up names of targets to avoid names like dual-dual

import json
import os
import sys
import argparse
import subprocess
import shutil
import tempfile
from pathlib import Path
from typing import Dict, List, Optional

class SimpleELRSConverter:
    def __init__(self):
        self.targets_data = {}
        self.hardware_data = {}
        self.temp_repo_path = None
        
        # Supported ESP32 platforms
        self.supported_platforms = {'esp32', 'esp32-c3', 'esp32-s3'}
        
        # Radio chip mappings
        self.radio_chips = {
            'sx1276': {'define': 'USE_RF95', 'freq': '900', 'type': 'rf95'},
            'sx1277': {'define': 'USE_RF95', 'freq': '900', 'type': 'rf95'},
            'sx1280': {'define': 'USE_SX1280', 'freq': '2400', 'type': 'sx128x'},
            'lr1121': {'define': 'USE_LR1121', 'freq': 'dual', 'type': 'lr1121'}
        }
        
        # Pin mappings from ELRS to Meshtastic
        self.pin_mappings = {
            # Basic SPI pins
            'radio_mosi': 'LORA_MOSI',
            'radio_miso': 'LORA_MISO',
            'radio_sck': 'LORA_SCK',
            'radio_nss': 'LORA_CS',
            'radio_rst': 'LORA_RESET',
            
            # Radio specific pins
            'radio_dio0': 'LORA_DIO0',
            'radio_dio1': 'LORA_DIO1', 
            'radio_busy': 'LORA_BUSY',
            'radio_txen': 'LORA_TXEN',
            'radio_rxen': 'LORA_RXEN',
            
            # Second radio pins (for true diversity)
            'radio_nss_2': 'LORA_CS_2',
            'radio_rst_2': 'LORA_RESET_2',
            'radio_dio0_2': 'LORA_DIO0_2',
            'radio_dio1_2': 'LORA_DIO1_2',
            'radio_busy_2': 'LORA_BUSY_2',
            'radio_txen_2': 'LORA_TXEN_2',
            'radio_rxen_2': 'LORA_RXEN_2',
            
            # LED pins
            'led': 'LED_PIN',
            'led_red': 'LED_PIN',
            'led_blue': 'LED_PIN',
            'led_rgb': 'NEOPIXEL_DATA',
            
            # Other pins
            'pin_button': 'BUTTON_PIN',
            'serial_rx': 'SERIAL_RX_PIN',
            'serial_tx': 'SERIAL_TX_PIN',
        }

    def clone_elrs_repo(self) -> bool:
        """Clone the ELRS targets repository to a temporary directory"""
        try:
            # Create temporary directory
            self.temp_repo_path = Path(tempfile.mkdtemp(prefix="elrs_targets_"))
            print(f"Cloning ELRS targets repository to {self.temp_repo_path}")
            
            # Clone the repository
            result = subprocess.run([
                'git', 'clone', '--depth', '1', 
                'https://github.com/ExpressLRS/targets.git', 
                str(self.temp_repo_path)
            ], capture_output=True, text=True)
            
            if result.returncode != 0:
                print(f"Failed to clone repository: {result.stderr}")
                return False
                
            print("Successfully cloned ELRS targets repository")
            return True
            
        except Exception as e:
            print(f"Error cloning repository: {e}")
            return False

    def cleanup_repo(self):
        """Remove the temporary repository directory"""
        if self.temp_repo_path and self.temp_repo_path.exists():
            try:
                shutil.rmtree(self.temp_repo_path)
                print(f"Cleaned up temporary repository at {self.temp_repo_path}")
            except Exception as e:
                print(f"Warning: Failed to cleanup temporary directory: {e}")

    def load_elrs_data(self) -> bool:
        """Load ELRS targets.json and hardware definitions from local repository"""
        if not self.temp_repo_path or not self.temp_repo_path.exists():
            print("Error: Repository not cloned. Call clone_elrs_repo() first.")
            return False
            
        print("Loading ELRS data from local repository...")
        
        # Load targets.json
        targets_file = self.temp_repo_path / 'targets.json'
        if not targets_file.exists():
            print(f"Error: targets.json not found at {targets_file}")
            return False
            
        print("  Loading targets.json...")
        with open(targets_file, 'r') as f:
            self.targets_data = json.load(f)
            
        print(f"  Loaded {len(self.targets_data)} target brands")
        
        # Load hardware definitions from RX directory only
        hw_dir = self.temp_repo_path / 'RX'
        if not hw_dir.exists():
            print("  Warning: RX directory not found")
            return False
            
        print("  Loading RX directory...")
        hw_files = list(hw_dir.glob('*.json'))
        
        if not hw_files:
            print("    No JSON files found in RX")
            return False
            
        print(f"    Found {len(hw_files)} files")
        
        for hw_file in hw_files:
            try:
                with open(hw_file, 'r') as f:
                    hw_data = json.load(f)
                    self.hardware_data[hw_file.name] = hw_data
            except Exception as e:
                print(f"    Warning: Failed to load {hw_file.name}: {e}")
                    
        print(f"  Successfully loaded {len(self.hardware_data)} hardware definitions")
        return True

    def extract_targets(self) -> List[Dict]:
        """Extract and process all supported targets"""
        processed_targets = []
        
        for brand, categories in self.targets_data.items():
            if not isinstance(categories, dict):
                continue
                
            for category, targets in categories.items():
                if not isinstance(targets, dict) or not category.startswith('rx_'):
                    continue  # Only process RX targets
                    
                for target_name, target_data in targets.items():
                    if not isinstance(target_data, dict):
                        continue
                        
                    # Get hardware layout file
                    layout_file = target_data.get('layout_file', '')
                    if not layout_file:
                        continue
                        
                    # Find the hardware definition
                    hw_filename = layout_file if layout_file.endswith('.json') else f"{layout_file}.json"
                    hw_data = self.hardware_data.get(hw_filename)
                    
                    if not hw_data:
                        print(f"Warning: No hardware definition found for {hw_filename}")
                        continue
                        
                    # Process this target
                    target_config = self.process_target(brand, target_name, category, target_data, hw_data)
                    if target_config:
                        processed_targets.append(target_config)
                    
        return processed_targets

    def process_target(self, brand: str, target_name: str, category: str, target_data: Dict, hw_data: Dict) -> Optional[Dict]:
        """Process a single target"""
        # Get firmware info
        firmware = target_data.get('firmware', '')
        
        # Determine platform from firmware first, then fallback to target_data
        platform = self.detect_platform_from_firmware(firmware) or target_data.get('platform', '')
        
        # Check if platform is supported
        if platform not in self.supported_platforms:
            return None
        
        # Determine radio chip - use firmware only
        radio_chip = self.detect_radio_chip(firmware)
        if not radio_chip:
            return None
            
        radio_info = self.radio_chips[radio_chip]
        pins = self.extract_pins(hw_data)
        is_diversity = self.detect_true_diversity(hw_data)
        define_name = self.generate_define_name(brand, target_name, radio_info['freq'])
        
        return {
            'brand': brand,
            'name': target_name,
            'category': category,
            'platform': platform,
            'firmware': firmware,
            'define_name': define_name,
            'radio_chip': radio_chip,
            'radio_info': radio_info,
            'pins': pins,
            'is_diversity': is_diversity,
            'hw_data': hw_data
        }

    def detect_radio_chip(self, firmware: str) -> Optional[str]:
        """Detect radio chip from firmware ONLY"""
        if not firmware:
            return None
        
        firmware_upper = firmware.upper()
        
        # Check for explicit radio chip mentions first
        if 'LR1121' in firmware_upper:
            return 'lr1121'
        elif 'SX1280' in firmware_upper:
            return 'sx1280'
        elif 'SX1276' in firmware_upper or 'SX127X' in firmware_upper or 'SX1277' in firmware_upper:
            return 'sx1276'
        
        # Fall back to frequency detection
        if '2400' in firmware_upper:
            return 'sx1280'
        elif '900' in firmware_upper:
            return 'sx1276'
        
        return None

    def detect_platform_from_firmware(self, firmware: str) -> Optional[str]:
        """Detect platform from firmware string"""
        if not firmware:
            return None
            
        firmware_upper = firmware.upper()
        
        # Check for platform in firmware name
        if 'ESP32C3' in firmware_upper:
            return 'esp32-c3'
        elif 'ESP32S3' in firmware_upper:
            return 'esp32-s3'
        elif 'ESP32' in firmware_upper:
            return 'esp32'
        
        return None

    def detect_true_diversity(self, hw_data: Dict) -> bool:
        """Detect if this is a true diversity setup by checking for second radio pins"""
        diversity_pins = ['radio_dio0_2', 'radio_dio1_2', 'radio_rst_2', 'radio_nss_2', 'radio_busy_2']
        
        return any(pin in hw_data and isinstance(hw_data[pin], int) for pin in diversity_pins)

    def extract_pins(self, hw_data: Dict) -> Dict[str, int]:
        """Extract pin definitions from hardware data"""
        pins = {}
        
        for elrs_pin, meshtastic_pin in self.pin_mappings.items():
            if elrs_pin in hw_data and isinstance(hw_data[elrs_pin], int):
                pins[meshtastic_pin] = hw_data[elrs_pin]
                
        return pins

    def generate_define_name(self, brand: str, target_name: str, freq: str) -> str:
        """Generate a clean define name for the target"""
        # Clean up strings and remove common noise words
        brand_clean = brand.upper().replace(' ', '_').replace('-', '_')
        target_clean = target_name.upper().replace(' ', '_').replace('-', '_')
        freq_clean = freq.upper()
        
        for word in ['MODULE', 'EXPRESSLRS', 'ELRS']:
            brand_clean = brand_clean.replace(word, '')
            target_clean = target_clean.replace(word, '')
            
        # Clean up extra underscores
        brand_clean = '_'.join(filter(None, brand_clean.split('_')))
        target_clean = '_'.join(filter(None, target_clean.split('_')))
        
        return f"ELRS_{brand_clean}_{target_clean}_{freq_clean}"

    def group_targets_by_pins(self, targets: List[Dict]) -> Dict[str, List[Dict]]:
        """Group targets that have identical pin configurations"""
        pin_groups = {}
        
        for target in targets:
            # Create a signature of the pins and radio chip for grouping
            pin_sig = self.create_pin_signature(target)
            
            if pin_sig not in pin_groups:
                pin_groups[pin_sig] = []
            pin_groups[pin_sig].append(target)
            
        return pin_groups

    def create_pin_signature(self, target: Dict) -> str:
        """Create a unique signature for pin configuration AND radio chip AND diversity status"""
        # Sort pins to create consistent signature
        pin_items = sorted(target['pins'].items())
        radio_chip = target['radio_info']['type']
        is_diversity = target['is_diversity']
        return f"{pin_items}_{radio_chip}_{is_diversity}"

    def generate_variant_file(self, platform: str, pin_groups: Dict, output_dir: Path):
        """Generate variant.h file for a platform"""
        variant_dir = output_dir / platform
        variant_dir.mkdir(parents=True, exist_ok=True)
        
        variant_file = variant_dir / 'variant.h'
        
        with open(variant_file, 'w') as f:
            f.write(self.generate_variant_content(platform, pin_groups))
            
        print(f"Generated {variant_file}")

    def generate_variant_content(self, platform: str, pin_groups: Dict) -> str:
        """Generate the content of variant.h file"""
        content = []
        
        # Header
        content.append("// ELRS Target Selection - automatically defined by PlatformIO environment")
        
        # Generate target selection defines
        all_targets = []
        for targets_list in pin_groups.values():
            all_targets.extend(targets_list)
            
        for i, target in enumerate(all_targets):
            content.append(f"// #define {target['define_name']}")
            
        content.append("")
        content.append("// Common settings")
        content.append("#undef HAS_GPS")
        content.append("#undef GPS_RX_PIN") 
        content.append("#undef GPS_TX_PIN")
        content.append("#undef EXT_NOTIFY_OUT")
        content.append("")
        
        # Add global chip-specific mappings at the top
        content.append("// Global chip-specific pin mappings")
        content.append("// SX128X mappings (2.4GHz)")
        content.append("#define SX128X_CS LORA_CS")
        content.append("#define SX128X_DIO1 LORA_DIO1")
        content.append("#define SX128X_BUSY LORA_BUSY")
        content.append("#define SX128X_RESET LORA_RESET")
        content.append("")
        content.append("// LR1121 mappings (dual-band)")
        content.append("#define LR1121_SPI_NSS_PIN LORA_CS")
        content.append("#define LR1121_SPI_SCK_PIN LORA_SCK")
        content.append("#define LR1121_SPI_MOSI_PIN LORA_MOSI")
        content.append("#define LR1121_SPI_MISO_PIN LORA_MISO")
        content.append("#define LR1121_NRESET_PIN LORA_RESET")
        content.append("#define LR1121_BUSY_PIN LORA_BUSY")
        content.append("#define LR1121_IRQ_PIN LORA_DIO1")
        content.append("#define LR11X0_DIO_AS_RF_SWITCH")
        content.append("")
        content.append("// Second radio mappings for true diversity")
        content.append("#define SX128X_CS_2 LORA_CS_2")
        content.append("#define SX128X_DIO0_2 LORA_DIO0_2")
        content.append("#define SX128X_DIO1_2 LORA_DIO1_2")
        content.append("#define SX128X_BUSY_2 LORA_BUSY_2")
        content.append("#define SX128X_RESET_2 LORA_RESET_2")
        content.append("#define LR1121_SPI_NSS_2_PIN LORA_CS_2")
        content.append("#define LR1121_NRESET_2_PIN LORA_RESET_2")
        content.append("#define LR1121_BUSY_2_PIN LORA_BUSY_2")
        content.append("#define LR1121_IRQ_2_PIN LORA_DIO1_2")
        content.append("")
        
        # Generate pin configurations for each group
        group_num = 1
        for pin_sig, targets_list in pin_groups.items():
            target = targets_list[0]  # All targets in group have same pins/radio chip
            
            if len(targets_list) > 1:
                content.append(f"// Pin Configuration {group_num} - Shared by multiple targets")
                conditions = " || ".join([f"defined({t['define_name']})" for t in targets_list])
                content.append(f"#if {conditions}")
            else:
                content.append(f"// {target['brand']} {target['name']}")
                content.append(f"#ifdef {target['define_name']}")
                
            content.append(f"#define {target['radio_info']['define']}")
            
            # Add TWO_RADIOS define if this is a diversity setup
            if target['is_diversity']:
                content.append("#define TWO_RADIOS")
            
            content.append("")
            
            # Separate pins into categories
            lora_pins = {k: v for k, v in target['pins'].items() if k.startswith('LORA_') and not k.endswith('_2')}
            lora2_pins = {k: v for k, v in target['pins'].items() if k.startswith('LORA_') and k.endswith('_2')}
            other_pins = {k: v for k, v in target['pins'].items() if not k.startswith('LORA_')}
            
            # Output radio pins
            if lora_pins:
                content.append("// Radio pins")
                content.extend(f"#define {pin_name} {pin_value}" for pin_name, pin_value in sorted(lora_pins.items()))
                content.append("")
            
            # Output second radio pins for diversity
            if lora2_pins:
                content.append("// Second radio pins (true diversity)")
                content.extend(f"#define {pin_name} {pin_value}" for pin_name, pin_value in sorted(lora2_pins.items()))
                content.append("")
            
            # Output other pins
            if other_pins:
                content.append("// Other pins")
                content.extend(f"#define {pin_name} {pin_value}" for pin_name, pin_value in sorted(other_pins.items()))
                content.append("")
            # Add special features
            if 'NEOPIXEL_DATA' in target['pins']:
                content.extend([
                    "",
                    "// RGB LED",
                    "#define HAS_NEOPIXEL",
                    "#define NEOPIXEL_COUNT 1",
                    "#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)"
                ])
                
            if 'BUTTON_PIN' in target['pins']:
                content.append("#define BUTTON_NEED_PULLUP")
                
            # Close conditional block
            if len(targets_list) > 1:
                conditions = " || ".join([f"defined({t['define_name']})" for t in targets_list])
                content.append(f"#endif // {conditions}")
            else:
                content.append(f"#endif // {target['define_name']}")
            content.append("")
            
            group_num += 1
            
        # Set PIN_ENABLE_HIGH to second radio CS pin for diversity setups
        has_diversity = any(target['is_diversity'] for targets_list in pin_groups.values() for target in targets_list)
        if has_diversity:
            # Find the CS_2 pin from any diversity target
            for targets_list in pin_groups.values():
                for target in targets_list:
                    if target['is_diversity'] and 'LORA_CS_2' in target['pins']:
                        content.extend([
                            "// Set second radio CS pin high at startup to disable it",
                            "#if defined(TWO_RADIOS)",
                            "#define PIN_ENABLE_HIGH LORA_CS_2",
                            "#endif // TWO_RADIOS",
                            ""
                        ])
                        break
                else:
                    continue
                break
        
        # Footer - ensure only one target is selected
        all_defines = [t['define_name'] for targets_list in pin_groups.values() for t in targets_list]
        condition = " + ".join([f"defined({define})" for define in all_defines])
        content.extend([
            "// Ensure only one target is selected",
            f"#if {condition} != 1",
            '#error "Exactly one ELRS target must be defined"',
            "#endif"
        ])
        
        return '\n'.join(content)

    def generate_platformio_ini(self, platform: str, targets: List[Dict], output_dir: Path):
        """Generate platformio.ini with individual environments for each target"""
        variant_dir = output_dir / platform
        variant_dir.mkdir(parents=True, exist_ok=True)
        
        # Platform-specific configuration
        platform_config = {
            'esp32': {
                'base': 'esp32_base',
                'board': 'esp32-pico-d4',
                'board_level': None
            },
            'esp32-c3': {
                'base': 'esp32c3_base', 
                'board': 'esp32-c3-devkitm-1',
                'board_level': 'extra'
            },
            'esp32-s3': {
                'base': 'esp32c3_base',  # Using esp32c3_base as reference from your example
                'board': 'esp32-s3-devkitc-1',
                'board_level': 'extra'
            }
        }
        
        config = platform_config.get(platform, platform_config['esp32'])
        content = []
        
        # Generate individual environment for each target
        for target in targets:
            env_name = f"elrs-{target['define_name'].lower().replace('elrs_', '').replace('_', '-')}"
            target_dir = f"variants/esp32/elrs/{platform}"
            
            content.extend([
                f"[env:{env_name}]",
                f"extends = {config['base']}",
                "platform = espressif32",
                f"board = {config['board']}",
                "framework = arduino"
            ])
            
            if config['board_level']:
                content.append(f"board_level = {config['board_level']}")
            
            content.extend([
                "build_flags =",
                f"    ${{{config['base']}.build_flags}}",
                f"    -I {target_dir}",
                f"    -D {target['define_name']}",
                f"    -D PRIVATE_HW",
                "    -O2",
                "    -D CONFIG_DISABLE_HAL_LOCKS=1"
            ])
            
            # Add monitor speed for non-esp32 platforms
            if platform != 'esp32':
                content.append("monitor_speed = 115200")
                
            content.extend([
                "upload_protocol = esptool",
                "upload_speed = 460800" if platform == 'esp32' else "upload_speed = 921600",
                "lib_deps =",
                f"    ${{{config['base']}.lib_deps}}",
                ""
            ])
        
        with open(variant_dir / 'platformio.ini', 'w') as f:
            f.write('\n'.join(content))
            
        print(f"Generated {variant_dir / 'platformio.ini'} with {len(targets)} environments")

    def copy_rfswitch_file(self, platform: str, output_dir: Path):
        """Copy rfswitch.h file to the variant directory"""
        variant_dir = output_dir / platform
        variant_dir.mkdir(parents=True, exist_ok=True)
        
        # Source rfswitch.h file (in the same directory as this script)
        source_file = Path(__file__).parent / 'rfswitch.h'
        target_file = variant_dir / 'rfswitch.h'
        
        if source_file.exists():
            shutil.copy2(source_file, target_file)
            print(f"Copied rfswitch.h to {target_file}")
        else:
            print(f"Warning: rfswitch.h not found at {source_file}")

    def convert(self, output_dir: str) -> bool:
        """Main conversion function"""
        output_path = Path(output_dir)
        
        # Clone the repository
        if not self.clone_elrs_repo():
            return False
            
        try:
            if not self.load_elrs_data():
                return False
                
            targets = self.extract_targets()
            if not targets:
                print("No targets found to convert")
                return False
                
            print(f"Found {len(targets)} targets to convert")
            
            # Group targets by platform
            by_platform = {}
            for target in targets:
                platform = target['platform']
                by_platform.setdefault(platform, []).append(target)
                
            # Generate files for each platform
            for platform, platform_targets in by_platform.items():
                print(f"Processing {len(platform_targets)} targets for {platform}")
                
                pin_groups = self.group_targets_by_pins(platform_targets)
                self.generate_variant_file(platform, pin_groups, output_path)
                self.generate_platformio_ini(platform, platform_targets, output_path)
                self.copy_rfswitch_file(platform, output_path)
                
            return True
            
        finally:
            self.cleanup_repo()


def main():
    parser = argparse.ArgumentParser(description='Convert ELRS targets to Meshtastic format')
    parser.add_argument('--output-dir', default='.',
                       help='Output directory for generated files (default: current directory)')
    
    args = parser.parse_args()
    
    converter = SimpleELRSConverter()
    
    if converter.convert(args.output_dir):
        print("Conversion completed successfully!")
        return 0
    else:
        print("Conversion failed!")
        return 1


if __name__ == '__main__':
    sys.exit(main())
