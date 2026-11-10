/*
  UsbIsoFix.h — KeySpeaker (USB Speaker & BLE Keyboard): a guard around TinyUSB's isochronous endpoint switch

  When the computer starts the speaker stream again after a pause, it selects the streaming alternate setting, and
  TinyUSB (0.20.1 in m5stack:esp32 3.3.10) calls dcd_edpt_iso_activate() for the audio OUT endpoint. That endpoint is
  still enabled from the last stream, so the DWC2 driver first disables it and busy-waits for the controller to confirm.
  On the Cardputer ADV that confirmation never comes: the usbd task stops, and the task watchdog restarts the Cardputer
  (crash records of S1-3 and S1-5: edpt_disable <- dcd_edpt_iso_activate <- audiod_set_interface).

  S1-5 disabled the endpoint here first, with time limits, but the endpoint did not turn off within 3 ms either, and
  the original then hung as before. So since S1-6 the endpoint is not switched off at all when it is still on with the
  same settings (isochronous, active, same packet size): the original would only disable it and write those same
  settings back. TinyUSB then queues the next transfer on it as usual. Every other case goes to the original.

  The linker option -Wl,--wrap=dcd_edpt_iso_activate (set in firmware/build.sh) sends TinyUSB's call here first.
  Without it (for example a build from the Arduino IDE), nothing calls this and the original runs alone.
*/
#pragma once
#include <atomic>
#include "esp32-hal-tinyusb.h"
#include "soc/usb_struct.h"

// DWC2 DOEPCTL fields (Synopsys databook names)
static constexpr uint32_t DWC2_DOEPCTL_EPENA = 1u << 31;     // endpoint enabled (cleared only by the controller)
static constexpr uint32_t DWC2_DOEPCTL_CNAK = 1u << 26;      // clear NAK
static constexpr uint32_t DWC2_DOEPCTL_USBACTEP = 1u << 15;  // endpoint active
static constexpr uint32_t DWC2_DOEPCTL_EPTYP_MASK = 3u << 18, DWC2_DOEPCTL_EPTYP_ISO = 1u << 18;
static constexpr uint32_t DWC2_DOEPCTL_MPS_MASK = 0x7ff;

// Counters for the log: switches kept as they were here, and switches passed on to the original.
static std::atomic<uint32_t> isoKeptCount{0}, isoPassedCount{0};

extern "C" bool __real_dcd_edpt_iso_activate(uint8_t rhport, tusb_desc_endpoint_t const* p_endpoint_desc);

extern "C" bool __wrap_dcd_edpt_iso_activate(uint8_t rhport, tusb_desc_endpoint_t const* p_endpoint_desc) {
  uint8_t addr = p_endpoint_desc->bEndpointAddress;
  uint8_t epnum = addr & 0x0f;
  if ((addr & 0x80) == 0 && epnum > 0 && epnum < 7) {
    uint32_t ctl = USB0.out_ep_reg[epnum].doepctl;
    if ((ctl & DWC2_DOEPCTL_EPENA) && (ctl & DWC2_DOEPCTL_USBACTEP) &&
        (ctl & DWC2_DOEPCTL_EPTYP_MASK) == DWC2_DOEPCTL_EPTYP_ISO &&
        (ctl & DWC2_DOEPCTL_MPS_MASK) == tu_edpt_packet_size(p_endpoint_desc)) {
      USB0.out_ep_reg[epnum].doepctl = ctl | DWC2_DOEPCTL_CNAK;  // keep it on and receiving
      isoKeptCount++;
      return true;
    }
  }
  isoPassedCount++;
  return __real_dcd_edpt_iso_activate(rhport, p_endpoint_desc);
}
