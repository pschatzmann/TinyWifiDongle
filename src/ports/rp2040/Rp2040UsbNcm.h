#pragma once
// USB CDC-NCM device for the Raspberry Pi Pico W / Pico 2 W (arduino-pico core,
// USB stack "Pico SDK", which is the default).
//
// The core's NCM driver is used at frame level. Frames are exchanged through
// two queues and two pools of frame buffers (one per direction, so a burst
// towards the host cannot starve the acknowledgements coming from it); a
// worker in the core's lwIP/CYW43 async context moves them in and out of the
// USB stack under the USB mutex.
#include <Arduino.h>
#include <LwipEthernet.h>  // __getEthernetContext()
#include <USB.h>
#include <pico/async_context.h>
#include <pico/mutex.h>
#include <pico/util/queue.h>
#include <tusb-ncm.h>  // empty header: makes the build include the core's NCM class driver
#include <tusb.h>

#include "../../core/Interfaces.h"

#if !CFG_TUD_NCM
#error "TinyWifiDongle: the USB stack of this core has no NCM class (CFG_TUD_NCM). Use USB Stack: Pico SDK."
#endif

extern "C" {
extern uint8_t tud_network_mac_address[6];
}

namespace twd {

/**
 * @brief USB CDC-NCM network device for the Raspberry Pi Pico W / Pico 2 W.
 *
 * Implements UsbTransport on top of the TinyUSB NCM class of the arduino-pico
 * core (USB stack "Pico SDK"). Frames from the host are queued by the TinyUSB
 * receive callback and handed to the FrameReceiver by a worker in the core's
 * lwIP async context; frames to the host are queued by send() and pushed into
 * the USB stack by the same worker, which holds the USB mutex while it does so.
 * The frame buffers are allocated by begin(); their number per direction is
 * set with setBuffers() (TinyWifiDongleConfig::framesToHost and framesFromHost).
 *
 * The TinyUSB callbacks are C functions, so the instance that ran begin()
 * receives them.
 */
class Rp2040UsbNcm : public UsbTransport {
 public:
  /// @brief Counters since start.
  struct Stats {
    uint32_t rxFrames;  ///< frames received from the host
    uint32_t txFrames;  ///< frames handed to the USB stack
    uint32_t txDrops;   ///< frames dropped on the way out (no buffer, host not ready)
    uint32_t rxDrops;   ///< frames from the host dropped (no buffer)
  };

  bool begin(const char* productName) override {
    instance() = this;
    product_ = productName;
    if (!toHost_) toHost_ = kDefaultToHost;
    if (!fromHost_) fromHost_ = kDefaultFromHost;
    txPool_ = (Frame*)calloc(toHost_, sizeof(Frame));
    rxPool_ = (Frame*)calloc(fromHost_, sizeof(Frame));
    if (!txPool_ || !rxPool_) {
      TWD_LOGGER.error("USB: not enough memory for %d + %d frame buffers", toHost_, fromHost_);
      free(txPool_);
      free(rxPool_);
      txPool_ = rxPool_ = nullptr;
      return false;
    }
    queue_init(&txFreeQ_, sizeof(Frame*), toHost_);
    queue_init(&txQ_, sizeof(Frame*), toHost_);
    queue_init(&rxFreeQ_, sizeof(Frame*), fromHost_);
    queue_init(&rxQ_, sizeof(Frame*), fromHost_);
    for (int i = 0; i < toHost_; i++) {
      Frame* f = &txPool_[i];
      f->rx = false;
      queue_try_add(&txFreeQ_, &f);
    }
    for (int i = 0; i < fromHost_; i++) {
      Frame* f = &rxPool_[i];
      f->rx = true;
      queue_try_add(&rxFreeQ_, &f);
    }
    worker_.user_data = this;
    worker_.do_work = &Rp2040UsbNcm::work;
    if (!async_context_add_when_pending_worker(__getEthernetContext(), &worker_)) {
      TWD_LOGGER.error("USB: could not add the worker to the async context");
      return false;
    }

    memcpy(tud_network_mac_address, hostMac_, 6);
    snprintf(macStr_, sizeof(macStr_), "%02X%02X%02X%02X%02X%02X", hostMac_[0], hostMac_[1], hostMac_[2], hostMac_[3],
             hostMac_[4], hostMac_[5]);

    USB.disconnect();
    epIn_ = USB.registerEndpointIn();
    epOut_ = USB.registerEndpointOut();
    epNotif_ = USB.registerEndpointIn();
    strDesc_ = USB.registerString(product_.c_str());
    strMac_ = USB.registerString(macStr_);
    if (!epIn_ || !epOut_ || !epNotif_) {
      TWD_LOGGER.error("USB: no free endpoints for the network interface");
      USB.connect();
      return false;
    }
    id_ = USB.registerInterface(2, &Rp2040UsbNcm::interfaceCb, this, TUD_CDC_NCM_DESC_LEN, 3, 0);
    USB.connect();
    return true;
  }

  bool ready() override { return tud_ready(); }

  void setReceiver(FrameReceiver* receiver) override { receiver_ = receiver; }

  void setHostMac(const uint8_t mac[6]) override { memcpy(hostMac_, mac, 6); }

  /// Number of frame buffers for frames to the host and from the host; 0 = the
  /// default of the chip. Call before begin().
  void setBuffers(uint8_t toHost, uint8_t fromHost) {
    toHost_ = toHost;
    fromHost_ = fromHost;
  }

  /// Queues the frame; never blocks. Returns false if it was dropped.
  bool send(const uint8_t* frame, uint16_t len) override {
    if (!tud_ready() || len == 0 || len > kMaxFrame) return false;  // nobody listening
    Frame* f = allocToHost();
    if (!f) {
      stats_.txDrops++;
      return false;
    }
    f->len = len;
    memcpy(f->data, frame, len);
    if (!queue_try_add(&txQ_, &f)) {
      release(f);
      stats_.txDrops++;
      return false;
    }
    schedule();
    return true;
  }

  /// Current counters.
  Stats stats() const { return stats_; }

  /// Number of frames waiting to be sent to the host.
  uint32_t txQueued() { return queue_get_level(&txQ_); }

  // ---- called by the TinyUSB glue at the bottom of this file -------------------
  /// The instance that receives the TinyUSB callbacks.
  static Rp2040UsbNcm* instance_() { return instance(); }

  /// Copies the frame `ref` (as passed to tud_network_xmit()) to `dst`; returns its length.
  static uint16_t copyFrame(uint8_t* dst, void* ref) {
    Frame* f = (Frame*)ref;
    memcpy(dst, f->data, f->len);
    return f->len;
  }

  /// A frame from the host. May run in interrupt context: copy and queue only.
  bool onNcmReceive(const uint8_t* src, uint16_t size) {
    Frame* f = allocFromHost();
    if (!f || size > kMaxFrame) {
      if (f) release(f);
      stats_.rxDrops++;
      schedule();
      return false;  // TinyUSB keeps the packet until the next recv_renew()
    }
    f->len = size;
    memcpy(f->data, src, size);
    if (!queue_try_add(&rxQ_, &f)) {
      release(f);
      stats_.rxDrops++;
      schedule();
      return false;
    }
    stats_.rxFrames++;
    gotRx_ = true;
    schedule();
    return true;
  }

 private:
  static constexpr uint16_t kNotifEndpointSize = 64;  // size of the interrupt endpoint of the NCM class
#if defined(PICO_RP2350)
  static constexpr int kDefaultToHost = 48;    // RP2350: 520 KB of RAM
  static constexpr int kDefaultFromHost = 16;
#else
  static constexpr int kDefaultToHost = 24;    // RP2040: 264 KB of RAM
  static constexpr int kDefaultFromHost = 8;
#endif
  static constexpr uint16_t kMaxFrame = 1536;
  struct Frame {
    uint16_t len;
    bool rx;  // belongs to the pool of frames from the host
    uint8_t data[kMaxFrame];
  };

  String product_;
  FrameReceiver* receiver_ = nullptr;
  uint8_t hostMac_[6] = {};
  char macStr_[13] = {};
  uint8_t epIn_ = 0, epOut_ = 0, epNotif_ = 0, strDesc_ = 0, strMac_ = 0, id_ = 0;
  Frame* txPool_ = nullptr;
  Frame* rxPool_ = nullptr;
  int toHost_ = 0, fromHost_ = 0;  // buffers per direction
  queue_t txFreeQ_, rxFreeQ_, rxQ_, txQ_;
  async_when_pending_worker_t worker_ = {};
  volatile bool gotRx_ = false;
  Stats stats_ = {};

  static Rp2040UsbNcm*& instance() {
    static Rp2040UsbNcm* p = nullptr;
    return p;
  }

  Frame* allocToHost() {
    Frame* f = nullptr;
    return queue_try_remove(&txFreeQ_, &f) ? f : nullptr;
  }
  Frame* allocFromHost() {
    Frame* f = nullptr;
    return queue_try_remove(&rxFreeQ_, &f) ? f : nullptr;
  }
  void release(Frame* f) { queue_try_add(f->rx ? &rxFreeQ_ : &txFreeQ_, &f); }
  void schedule() { async_context_set_work_pending(__getEthernetContext(), &worker_); }

  static void interfaceCb(int itf, uint8_t* dst, int len, void* param) {
    Rp2040UsbNcm* s = (Rp2040UsbNcm*)param;
    uint8_t desc[TUD_CDC_NCM_DESC_LEN] = {
        // interface number, description string, MAC string, notification endpoint and size,
        // data endpoints (out, in) and size, largest Ethernet frame
        TUD_CDC_NCM_DESCRIPTOR((uint8_t)itf, s->strDesc_, s->strMac_, s->epNotif_, kNotifEndpointSize, s->epOut_, s->epIn_,
                               CFG_TUD_NET_ENDPOINT_SIZE, CFG_TUD_NET_MTU)};
    memcpy(dst, desc, len);
  }

  // Worker in the lwIP async context.
  static void work(async_context_t*, async_when_pending_worker_t* w) { ((Rp2040UsbNcm*)w->user_data)->pump(w); }

  void pump(async_when_pending_worker_t* w) {
    // Host -> uplink: this context may call the WiFi chip.
    Frame* f;
    while (queue_try_remove(&rxQ_, &f)) {
      if (receiver_) receiver_->onFrame(f->data, f->len);
      release(f);
    }

    // The USB stack is shared with the rest of the core: take its mutex or try again later.
    if (!mutex_try_enter(&USB.mutex, nullptr)) {
      w->work_pending = true;
      return;
    }
    while (queue_try_peek(&txQ_, &f)) {
      if (!tud_ready()) {  // host went away: drop what is queued
        queue_try_remove(&txQ_, &f);
        release(f);
        stats_.txDrops++;
        continue;
      }
      if (!tud_network_can_xmit(f->len)) break;  // driver busy, try again below
      tud_network_xmit(f, 0);                    // copies via tud_network_xmit_cb
      queue_try_remove(&txQ_, &f);
      release(f);
      stats_.txFrames++;
    }
    // The driver hands over one datagram per renew.
    int limit = 10;
    do {
      gotRx_ = false;
      tud_network_recv_renew();
      tud_task();
    } while (gotRx_ && --limit > 0);
    mutex_exit(&USB.mutex);

    if (!queue_is_empty(&txQ_) || !queue_is_empty(&rxQ_)) w->work_pending = true;
  }
};

}  // namespace twd

// ---- TinyUSB network class callbacks ---------------------------------------------
extern "C" {
uint8_t tud_network_mac_address[6] = {0};

void tud_network_init_cb(void) {}

bool tud_network_recv_cb(const uint8_t* src, uint16_t size) {
  twd::Rp2040UsbNcm* s = twd::Rp2040UsbNcm::instance_();
  return s ? s->onNcmReceive(src, size) : false;
}

uint16_t tud_network_xmit_cb(uint8_t* dst, void* ref, uint16_t arg) {
  (void)arg;
  return twd::Rp2040UsbNcm::copyFrame(dst, ref);
}
}
