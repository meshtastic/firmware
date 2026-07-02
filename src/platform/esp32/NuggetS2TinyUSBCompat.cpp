#if defined(NUGGET_S2_LORA)

#include <stdint.h>

#include "tusb.h"

extern "C" {

// The ESP32-S2 Arduino TinyUSB archive is built with every class enabled.
// Nugget only uses CDC; these stubs keep unused class buffers out of DRAM.

void hidd_init(void) {}
bool hidd_deinit(void)
{
    return true;
}
void hidd_reset(uint8_t rhport) {}
uint16_t hidd_open(uint8_t rhport, const tusb_desc_interface_t *itf_desc, uint16_t max_len)
{
    return 0;
}
bool hidd_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request)
{
    return false;
}
bool hidd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t event, uint32_t xferred_bytes)
{
    return false;
}

void midid_init(void) {}
bool midid_deinit(void)
{
    return true;
}
void midid_reset(uint8_t rhport) {}
uint16_t midid_open(uint8_t rhport, const tusb_desc_interface_t *itf_desc, uint16_t max_len)
{
    return 0;
}
bool midid_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request)
{
    return false;
}
bool midid_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    return false;
}

void mscd_init(void) {}
bool mscd_deinit(void)
{
    return true;
}
void mscd_reset(uint8_t rhport) {}
uint16_t mscd_open(uint8_t rhport, const tusb_desc_interface_t *itf_desc, uint16_t max_len)
{
    return 0;
}
bool mscd_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request)
{
    return false;
}
bool mscd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t event, uint32_t xferred_bytes)
{
    return false;
}

void audiod_init(void) {}
bool audiod_deinit(void)
{
    return true;
}
void audiod_reset(uint8_t rhport) {}
uint16_t audiod_open(uint8_t rhport, const tusb_desc_interface_t *itf_desc, uint16_t max_len)
{
    return 0;
}
bool audiod_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request)
{
    return false;
}
bool audiod_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    return false;
}
bool audiod_xfer_isr(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    return false;
}
void audiod_sof_isr(uint8_t rhport, uint32_t frame_count) {}

void videod_init(void) {}
bool videod_deinit(void)
{
    return true;
}
void videod_reset(uint8_t rhport) {}
uint16_t videod_open(uint8_t rhport, const tusb_desc_interface_t *itf_desc, uint16_t max_len)
{
    return 0;
}
bool videod_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request)
{
    return false;
}
bool videod_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    return false;
}

void dfu_moded_init(void) {}
bool dfu_moded_deinit(void)
{
    return true;
}
void dfu_moded_reset(uint8_t rhport) {}
uint16_t dfu_moded_open(uint8_t rhport, const tusb_desc_interface_t *itf_desc, uint16_t max_len)
{
    return 0;
}
bool dfu_moded_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request)
{
    return false;
}

void dfu_rtd_init(void) {}
bool dfu_rtd_deinit(void)
{
    return true;
}
void dfu_rtd_reset(uint8_t rhport) {}
uint16_t dfu_rtd_open(uint8_t rhport, const tusb_desc_interface_t *itf_desc, uint16_t max_len)
{
    return 0;
}
bool dfu_rtd_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request)
{
    return false;
}

void vendord_init(void) {}
bool vendord_deinit(void)
{
    return true;
}
void vendord_reset(uint8_t rhport) {}
uint16_t vendord_open(uint8_t rhport, const tusb_desc_interface_t *itf_desc, uint16_t max_len)
{
    return 0;
}
bool vendord_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t event, uint32_t xferred_bytes)
{
    return false;
}

void netd_init(void) {}
bool netd_deinit(void)
{
    return true;
}
void netd_reset(uint8_t rhport) {}
uint16_t netd_open(uint8_t rhport, const tusb_desc_interface_t *itf_desc, uint16_t max_len)
{
    return 0;
}
bool netd_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request)
{
    return false;
}
bool netd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    return false;
}

bool tuh_deinit(uint8_t rhport)
{
    return true;
}
bool tuh_inited(void)
{
    return false;
}
bool tuh_rhport_init(uint8_t rhport, const tusb_rhport_init_t *rh_init)
{
    return false;
}
void tuh_task_ext(uint32_t timeout_ms, bool in_isr) {}
bool usbh_edpt_claim(uint8_t dev_addr, uint8_t ep_addr)
{
    return false;
}
bool usbh_edpt_release(uint8_t dev_addr, uint8_t ep_addr)
{
    return false;
}
bool usbh_edpt_xfer_with_callback(uint8_t dev_addr, uint8_t ep_addr, uint8_t *buffer, uint16_t total_bytes,
                                  tuh_xfer_cb_t complete_cb, uintptr_t user_data)
{
    return false;
}
void hcd_int_handler(uint8_t rhport, bool in_isr) {}

uint32_t tud_dfu_get_timeout_cb(uint8_t alt, uint8_t state)
{
    return 0;
}

void tud_dfu_download_cb(uint8_t alt, uint16_t block_num, const uint8_t *data, uint16_t length) {}

void tud_dfu_manifest_cb(uint8_t alt) {}

void tud_dfu_runtime_reboot_to_dfu_cb(void) {}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t itf)
{
    return nullptr;
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    return 0;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, const uint8_t *buffer, uint16_t bufsize)
{
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    return false;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    return -1;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    return -1;
}

int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    return -1;
}

bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
    return false;
}

uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
    return 0;
}

} // extern "C"

#endif
