#pragma once
// USB CDC-NCM device on the ESP32-S2/S3 (board setting: "USB Mode: USB-OTG (TinyUSB)").
//
// Threading: TinyUSB's NCM driver is not thread-safe, so every call into it
// (tud_network_xmit / can_xmit / recv_renew) is made from the TinyUSB task.
// Other tasks only enqueue frames and schedule work with usbd_defer_func().
#include <Arduino.h>
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp32-hal-tinyusb.h"
#include "tusb.h"
#include "device/usbd_pvt.h"  // usbd_defer_func, usbd_edpt_busy
#include "portable/synopsys/dwc2/dwc2_type.h"  // register layout of the USB controller

#include "../../core/Interfaces.h"

#if !CFG_TUD_NCM
#error "TinyWifiDongle: this core was built without TinyUSB NCM (CFG_TUD_NCM)."
#endif

namespace twd {

/**
 * @brief USB CDC-NCM network device for the ESP32-S2/S3 (TinyUSB).
 *
 * Implements UsbTransport on top of the TinyUSB NCM class that the Arduino
 * core ships:
 *  - registers the NCM interface and its descriptors,
 *  - queues outgoing frames and feeds them to the driver, one frame per USB
 *    transfer, from the TinyUSB task,
 *  - re-arms reception after every frame,
 *  - re-enumerates the device if the send path stalls for longer than the
 *    stall timeout (1 second by default, see setStallTimeout()).
 *
 * The TinyUSB callbacks are C functions, so the class is a singleton in
 * effect: the instance that ran begin() receives them.
 */
class Esp32UsbNcm : public UsbTransport {
 public:
  /// @brief Counters since start: frames received from and sent to the host,
  /// frames dropped on the way out, polls that found the driver busy, and
  /// stall recoveries (USB re-enumerations).
  struct Stats {
    uint32_t rxFrames;  ///< frames received from the host
    uint32_t txFrames;  ///< frames handed to the USB driver
    uint32_t txDrops;   ///< frames dropped on the way out
    uint32_t txBusy;    ///< polls that found the driver busy
    uint32_t resets;    ///< stall recoveries (USB re-enumerations)
  };

  /// Create the transport; nothing is started before begin().
  explicit Esp32UsbNcm(Logger& log) : log_(log) {}

  bool begin(const char* productName) override {
    product_ = productName;
    instance() = this;
    deriveMacs();
    txQueue_ = xQueueCreate(kTxQueueLen, sizeof(TxItem));
    if (!txQueue_) return false;
    esp_timer_create_args_t args = {};
    args.arg = this;
    args.callback = &Esp32UsbNcm::drainTimerCb;
    args.name = "ncmTx";
    if (esp_timer_create(&args, &drainTimer_) != ESP_OK) return false;
    args.callback = &Esp32UsbNcm::reconnectTimerCb;
    args.name = "ncmUsb";
    if (esp_timer_create(&args, &reconnectTimer_) != ESP_OK) return false;
    return tinyusb_enable_interface(USB_INTERFACE_CUSTOM, TUD_CDC_NCM_DESC_LEN, &Esp32UsbNcm::descriptorCb) == ESP_OK;
  }

  bool ready() override { return tud_ready(); }

  void setReceiver(FrameReceiver* receiver) override { receiver_ = receiver; }

  void setHostMac(const uint8_t mac[6]) override {
    memcpy(hostMac_, mac, 6);
    macFixed_ = true;
  }

  /// Runs in the caller's task: copy, enqueue, never block.
  bool send(const uint8_t* frame, uint16_t len) override {
    if (!tud_ready() || len == 0 || len > CFG_TUD_NET_MTU) return false;  // nobody listening
    // The NCM driver sends 56 bytes of NTB headers plus the frame rounded up
    // to 4 bytes as one USB transfer. A transfer that is an exact multiple of
    // the 64 byte packet size needs a terminating zero-length packet, which
    // wedges the send path on this chip. Trailing zero bytes on an Ethernet
    // frame are harmless, so such frames are padded.
    size_t outLen = len;
    if ((kNtbHeader + ((len + 3u) & ~3u)) % 64 == 0) outLen = len + 4u;
    uint8_t* copy = (uint8_t*)malloc(outLen);
    if (!copy) {
      stats_.txDrops++;
      return false;
    }
    memcpy(copy, frame, len);
    memset(copy + len, 0, outLen - len);
    TxItem item = {copy, (uint16_t)outLen};
    if (xQueueSend(txQueue_, &item, 0) != pdTRUE) {
      free(copy);
      stats_.txDrops++;  // congested: TCP will retransmit
      return false;
    }
    scheduleDrain();
    return true;
  }

  /// Largest Ethernet frame announced to the host; call before begin().
  void setMaxFrame(uint16_t bytes) { maxFrame_ = bytes; }

  /// Re-enumerate USB if sending makes no progress for `ms` milliseconds.
  /// 0 switches the watchdog off: a stalled send path then stays stalled.
  void setStallTimeout(uint32_t ms) { stallTimeoutMs_ = ms; }

  /// MAC address of the adapter as the host sees it.
  const uint8_t* hostMac() const { return hostMac_; }

  /// Current counters.
  Stats stats() const { return stats_; }
  /// Logs the USB controller's registers of the data IN endpoint: control,
  /// transfer size, interrupt flags, FIFO space, and the interrupt masks. A
  /// debugging aid for a stalled send path.
  /// Only available on the ESP32-S2/S3, where the controller sits at a known
  /// address; on other chips it only reports that.
  void logControllerState() {
#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3
    volatile dwc2_regs_t* d = reinterpret_cast<volatile dwc2_regs_t*>(0x60080000);  // USB-OTG of the S2/S3
    unsigned n = epIn_ & 0x7F;
    // Two short lines: the log port drops what does not fit its buffer.
    log_.log("dw%u ctl=%08x tsiz=%08x int=%08x txs=%x", n, (unsigned)d->epin[n].diepctl, (unsigned)d->epin[n].tsiz,
             (unsigned)d->epin[n].diepint, (unsigned)d->epin[n].dtxfsts);
    vTaskDelay(pdMS_TO_TICKS(100));
    log_.log("dw em=%08x gi=%08x/%08x da=%08x/%08x", (unsigned)d->diepempmsk, (unsigned)d->gintsts,
             (unsigned)d->gintmsk, (unsigned)d->daint, (unsigned)d->daintmsk);
#else
    log_.log("controller registers: not available on this chip");
#endif
  }

  /// Number of frames waiting to be sent to the host.
  uint32_t txQueued() const { return txQueue_ ? uxQueueMessagesWaiting(txQueue_) : 0; }

  // ---- called by the TinyUSB glue at the bottom of this file -------------------
  /// The instance that receives the TinyUSB callbacks.
  static Esp32UsbNcm* instance_() { return instance(); }

  /// A frame from the host (TinyUSB task; must not block).
  void onNcmReceive(const uint8_t* src, uint16_t size) {
    stats_.rxFrames++;
    if (receiver_) receiver_->onFrame(src, size);
    // The NCM driver hands over one datagram per tud_network_recv_renew(), and
    // the call must happen after the receive callback returned. Count the
    // accepted datagrams and work them off in one deferred call; renew itself
    // may deliver further datagrams, which just raise the count again.
    renewCount_++;
    if (renewActive_) return;
    renewActive_ = true;
    usbd_defer_func(&Esp32UsbNcm::renewCb, this, false);
  }

 private:
  static constexpr int kTxQueueLen = 32;
  static constexpr size_t kNtbHeader = 56;  // NTH + NDP headers of the driver's NTB
  struct TxItem {
    uint8_t* data;
    uint16_t len;
  };

  Logger& log_;
  String product_;
  FrameReceiver* receiver_ = nullptr;
  QueueHandle_t txQueue_ = nullptr;
  esp_timer_handle_t drainTimer_ = nullptr;
  esp_timer_handle_t reconnectTimer_ = nullptr;
  uint8_t hostMac_[6] = {};
  char macStr_[13] = {};  // hostMac_ as 12 hex chars (NCM string descriptor)
  uint32_t stallTimeoutMs_ = 1000;
  uint16_t maxFrame_ = CFG_TUD_NET_MTU;  // wMaxSegmentSize announced to the host
  bool macFixed_ = false;  // set by setHostMac(): do not derive the MAC from the chip id
  uint8_t epIn_ = 0;      // bulk IN endpoint address of the data pipe
  Stats stats_ = {};

  // TinyUSB task only.
  volatile bool drainPending_ = false;
  volatile bool renewActive_ = false;
  volatile uint32_t renewCount_ = 0;
  bool stalled_ = false;
  uint32_t stallSince_ = 0;

  static Esp32UsbNcm*& instance() {
    static Esp32UsbNcm* p = nullptr;
    return p;
  }

  void deriveMacs() {
    if (!macFixed_) {
      uint8_t base[6];
      esp_efuse_mac_get_default(base);
      memcpy(hostMac_, base, 6);
      hostMac_[0] = (hostMac_[0] | 0x02) & 0xFE;  // locally administered, unicast
      hostMac_[5] ^= 0x01;
    }
    snprintf(macStr_, sizeof(macStr_), "%02X%02X%02X%02X%02X%02X", hostMac_[0], hostMac_[1], hostMac_[2], hostMac_[3],
             hostMac_[4], hostMac_[5]);
  }

  static uint16_t descriptorCb(uint8_t* dst, uint8_t* itf) {
    Esp32UsbNcm* s = instance();
    uint8_t itfnum = *itf;
    *itf += 2;  // control + data interface
    uint8_t strDesc = tinyusb_add_string_descriptor(s->product_.c_str());
    uint8_t strMac = tinyusb_add_string_descriptor(s->macStr_);
    uint8_t epData = tinyusb_get_free_duplex_endpoint();
    uint8_t epNotif = tinyusb_get_free_in_endpoint();
    if (!strDesc || !strMac || !epData || !epNotif) return 0;
    s->epIn_ = (uint8_t)(0x80 | epData);
    const uint16_t bulk = TUD_OPT_HIGH_SPEED ? 512 : 64;
    uint8_t desc[] = {TUD_CDC_NCM_DESCRIPTOR(itfnum, strDesc, strMac, (uint8_t)(0x80 | epNotif), 64, epData,
                                             (uint8_t)(0x80 | epData), bulk, s->maxFrame_, 50, 0)};
    memcpy(dst, desc, sizeof(desc));
    return sizeof(desc);
  }

  // ---- transmit: queue -> NCM driver, in the TinyUSB task -------------------------
  void scheduleDrain() {
    if (__atomic_exchange_n(&drainPending_, true, __ATOMIC_ACQ_REL)) return;
    usbd_defer_func(&Esp32UsbNcm::drainCb, this, false);
  }
  static void drainTimerCb(void* arg) { ((Esp32UsbNcm*)arg)->scheduleDrain(); }
  static void drainCb(void* arg) { ((Esp32UsbNcm*)arg)->drain(); }

  // One frame per USB transfer: the NCM driver would otherwise batch frames
  // that arrive while a transfer is in flight, and the padding rule in send()
  // only holds for a transfer carrying a single frame.
  void drain() {
    drainPending_ = false;
    TxItem item;
    uint32_t now = millis();
    while (xQueuePeek(txQueue_, &item, 0) == pdTRUE) {
      if (tud_ready()) {
        if (usbd_edpt_busy(0, epIn_) || !tud_network_can_xmit(item.len)) {
          stats_.txBusy++;
          if (!stalled_) {
            stalled_ = true;
            stallSince_ = now;
          } else if (stallTimeoutMs_ && now - stallSince_ > stallTimeoutMs_) {
            recover();
            return;
          }
          esp_timer_start_once(drainTimer_, 300);  // transfer in flight: retry shortly (us)
          return;
        }
        stalled_ = false;
        tud_network_xmit(item.data, item.len);  // copies via tud_network_xmit_cb
        stats_.txFrames++;
      } else {
        stats_.txDrops++;
        stalled_ = false;
      }
      xQueueReceive(txQueue_, &item, 0);
      free(item.data);
    }
    stalled_ = false;
  }

  // Safety net: if the send path stops making progress, drop the queued frames
  // and make the host enumerate the device again.
  void recover() {
    stats_.resets++;
    TxItem it;
    while (xQueueReceive(txQueue_, &it, 0) == pdTRUE) free(it.data);
    stalled_ = false;
    tud_disconnect();
    esp_timer_start_once(reconnectTimer_, 800000);
  }
  static void reconnectTimerCb(void* arg) { usbd_defer_func(&Esp32UsbNcm::connectCb, arg, false); }
  static void connectCb(void*) { tud_connect(); }

  static void renewCb(void* arg) {
    Esp32UsbNcm* s = (Esp32UsbNcm*)arg;
    while (s->renewCount_) {
      s->renewCount_--;
      tud_network_recv_renew();
    }
    s->renewActive_ = false;
  }
};

}  // namespace twd

// ---- TinyUSB network class callbacks (override the core's weak stubs) ---------
extern "C" {
bool tud_network_recv_cb(const uint8_t* src, uint16_t size) {
  twd::Esp32UsbNcm* s = twd::Esp32UsbNcm::instance_();
  if (s) s->onNcmReceive(src, size);
  return true;
}
uint16_t tud_network_xmit_cb(uint8_t* dst, void* ref, uint16_t arg) {
  memcpy(dst, ref, arg);
  return arg;
}
void tud_network_init_cb(void) {}
}
