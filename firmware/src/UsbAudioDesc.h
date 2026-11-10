/*
  UsbAudioDesc.h — KeySpeaker (USB Speaker & BLE Keyboard): an Interface Association Descriptor in front of the
  speaker, for Windows (S2-9), and the speaker's output terminal type (S2-10)

  The linker option -Wl,--wrap=tinyusb_enable_interface (set in firmware/build.sh) sends the interface registrations
  here first. The audio one gets the changes below; any other interface passes through unchanged. Without the linker
  option, nothing calls this and the descriptors stay as they were.

  The core's UAC1 speaker is two interfaces, audio control and audio streaming, with no IAD in front of them. The USB serial before them has one, so the device
  is a composite with IADs. Windows then takes the audio control interface alone as a function, and its USB audio
  driver cannot start it: "This device cannot start. (Code 10)" (Windows 11, 2026-10-31). macOS groups the two by the
  audio control header and never needed the IAD. The IAD groups the two interfaces as one audio function (class 1,
  subclass 0, protocol 0, as Linux's UAC1 gadget does); the descriptor gets 8 more bytes.
  The core's output terminal is headphones (0x0302), so Windows listed the speaker as "Headphones (KeySpeaker)". S2-10
  makes it a speaker (0x0301): Windows 11 lists "Speakers (KeySpeaker)" (2026-10-31). A Windows PC that had seen an
  earlier version showed no output at all until KeySpeaker was uninstalled in Device Manager and plugged in again.
*/
#pragma once
#include <stdint.h>
#include <string.h>
#include "esp32-hal-tinyusb.h"

extern "C" esp_err_t __real_tinyusb_enable_interface(tinyusb_interface_t interface, uint16_t descriptor_len, tinyusb_descriptor_cb_t cb);

static tinyusb_descriptor_cb_t audioDescriptorCb = nullptr;

static uint16_t audioDescriptor(uint8_t* dst, uint8_t* itf) {
  const uint8_t first = *itf;
  uint16_t len = audioDescriptorCb(dst + 8, itf);
  if (!len) return 0;
  const uint8_t iad[8] = {8, 0x0B, first, (uint8_t)(*itf - first), 0x01, 0x00, 0x00, 0};
  memcpy(dst, iad, sizeof iad);
  // The output terminal: a class-specific descriptor (0x24) of subtype 3 inside the audio control interface (class 1,
  // subclass 1). In the streaming interface subtype 3 means something else, so the interface is followed.
  bool inControl = false;
  for (uint8_t* d = dst + 8; d < dst + 8 + len && d[0] >= 2; d += d[0]) {
    if (d[1] == 0x04) inControl = d[5] == 0x01 && d[6] == 0x01;
    else if (inControl && d[1] == 0x24 && d[2] == 0x03 && d[0] >= 6 && d[4] == 0x02 && d[5] == 0x03) d[4] = 0x01;
  }
  return (uint16_t)(len + sizeof iad);
}

extern "C" esp_err_t __wrap_tinyusb_enable_interface(tinyusb_interface_t interface, uint16_t descriptor_len, tinyusb_descriptor_cb_t cb) {
  if (interface == USB_INTERFACE_AUDIO && cb) {
    audioDescriptorCb = cb;
    return __real_tinyusb_enable_interface(interface, (uint16_t)(descriptor_len + 8), audioDescriptor);
  }
  return __real_tinyusb_enable_interface(interface, descriptor_len, cb);
}
