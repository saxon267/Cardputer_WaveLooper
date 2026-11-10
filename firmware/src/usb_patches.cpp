// USB descriptor / isochronous endpoint patches, adapted from Cardputer-ADV
// KeySpeaker (MIT, https://github.com/kariagepompadour/Cardputer-ADV-KeySpeaker).
//
//  - Windows needs an IAD in front of the UAC interfaces (otherwise Code 10).
//  - TinyUSB 0.20.x on the Cardputer ADV hangs when the host restarts the
//    isochronous OUT stream after a pause; the wrapper keeps the endpoint alive.
#include <USB.h>
#include "tusb.h"

#include "UsbAudioDesc.h"
#include "UsbIsoFix.h"
