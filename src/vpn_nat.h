#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/ip.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/etharp.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/tcp.h"
#include "lwip/prot/udp.h"

namespace VpnNat {

struct NatEntry {
  ip4_addr_t client_ip;
  u16_t client_port;
  u16_t mapped_port;
  u8_t proto;
  u32_t last_seen;
  bool active;
};

static const uint16_t kMaxNatEntries = 128;
static const uint16_t kNatPortStart = 50000;
static const uint16_t kNatPortEnd   = 60000;

static NatEntry natTable[kMaxNatEntries];
static uint16_t nextPort = kNatPortStart;

static struct netif *ap_netif = nullptr;
static struct netif *sta_netif = nullptr;
static netif_input_fn orig_ap_input = nullptr;
static netif_input_fn orig_sta_input = nullptr;

static bool natActive = false;
static uint32_t natBytesRelayed = 0;

static WiFiUDP dnsUdp;
static const uint16_t kDnsPort = 53;
static IPAddress upstreamDns(1, 1, 1, 1);

// Find netif pointers in LwIP list
inline void findNetifs() {
  ap_netif = nullptr;
  sta_netif = nullptr;
  for (struct netif *n = netif_list; n != nullptr; n = n->next) {
    if (n->name[0] == 'a' && n->name[1] == 'p') {
      ap_netif = n;
    } else if (n->name[0] == 's' && n->name[1] == 't') {
      sta_netif = n;
    }
  }
}

// Look up or allocate a NAT entry for an outbound packet
inline NatEntry* getOutboundNat(const ip4_addr_t *client_ip, u16_t client_port, u8_t proto) {
  u32_t now = millis();
  int oldestIdx = -1;
  u32_t oldestTime = 0xFFFFFFFF;

  for (uint16_t i = 0; i < kMaxNatEntries; i++) {
    if (natTable[i].active) {
      if (natTable[i].proto == proto &&
          natTable[i].client_port == client_port &&
          ip4_addr_cmp(&natTable[i].client_ip, client_ip)) {
        natTable[i].last_seen = now;
        return &natTable[i];
      }
      if (natTable[i].last_seen < oldestTime) {
        oldestTime = natTable[i].last_seen;
        oldestIdx = i;
      }
    } else if (oldestIdx == -1) {
      oldestIdx = i;
      oldestTime = 0;
    }
  }

  if (oldestIdx >= 0) {
    NatEntry *e = &natTable[oldestIdx];
    e->client_ip = *client_ip;
    e->client_port = client_port;
    e->proto = proto;
    e->last_seen = now;
    e->mapped_port = nextPort++;
    if (nextPort >= kNatPortEnd) nextPort = kNatPortStart;
    e->active = true;
    return e;
  }
  return nullptr;
}

// Look up inbound NAT entry by mapped destination port and protocol
inline NatEntry* getInboundNat(u16_t mapped_port, u8_t proto) {
  u32_t now = millis();
  for (uint16_t i = 0; i < kMaxNatEntries; i++) {
    if (natTable[i].active &&
        natTable[i].proto == proto &&
        natTable[i].mapped_port == mapped_port) {
      natTable[i].last_seen = now;
      return &natTable[i];
    }
  }
  return nullptr;
}

// Hook on AP netif: catches packets from connected phone/laptop
static err_t ap_input_hook(struct pbuf *p, struct netif *inp) {
  if (!natActive || !sta_netif || !netif_is_up(sta_netif)) {
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_IF;
  }

  if (p->len < sizeof(struct ip_hdr)) {
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_VAL;
  }

  struct ip_hdr *iphdr = (struct ip_hdr *)p->payload;

  if (IPH_V(iphdr) != 4) {
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_VAL;
  }

  // If packet is destined for the CYD itself (192.168.4.1), pass to local stack
  if (iphdr->dest.addr == netif_ip4_addr(inp)->addr) {
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_OK;
  }

  u16_t hlen = IPH_HL(iphdr) * 4;
  u8_t proto = IPH_PROTO(iphdr);

  if (proto == IP_PROTO_UDP) {
    if (p->len >= hlen + sizeof(struct udp_hdr)) {
      struct udp_hdr *udphdr = (struct udp_hdr *)((u8_t *)p->payload + hlen);
      u16_t srcPort = ntohs(udphdr->src);
      ip4_addr_t srcIp;
      srcIp.addr = iphdr->src.addr;
      NatEntry *e = getOutboundNat(&srcIp, srcPort, IP_PROTO_UDP);
      if (e) {
        iphdr->src.addr = netif_ip4_addr(sta_netif)->addr;
        udphdr->src = htons(e->mapped_port);
        IPH_CHKSUM_SET(iphdr, 0);
        iphdr->_chksum = inet_chksum(iphdr, hlen);
        udphdr->chksum = 0;
        natBytesRelayed += p->tot_len;
        ip4_addr_t dstIp;
        dstIp.addr = iphdr->dest.addr;
        return sta_netif->output(sta_netif, p, &dstIp);
      }
    }
  } else if (proto == IP_PROTO_TCP) {
    if (p->len >= hlen + sizeof(struct tcp_hdr)) {
      struct tcp_hdr *tcphdr = (struct tcp_hdr *)((u8_t *)p->payload + hlen);
      u16_t srcPort = ntohs(tcphdr->src);
      ip4_addr_t srcIp;
      srcIp.addr = iphdr->src.addr;
      NatEntry *e = getOutboundNat(&srcIp, srcPort, IP_PROTO_TCP);
      if (e) {
        iphdr->src.addr = netif_ip4_addr(sta_netif)->addr;
        tcphdr->src = htons(e->mapped_port);
        IPH_CHKSUM_SET(iphdr, 0);
        iphdr->_chksum = inet_chksum(iphdr, hlen);
        tcphdr->chksum = 0;
        ip4_addr_t dstIp;
        dstIp.addr = iphdr->dest.addr;
        tcphdr->chksum = inet_chksum_pseudo(p, IP_PROTO_TCP, p->tot_len - hlen,
                                            netif_ip4_addr(sta_netif), &dstIp);
        natBytesRelayed += p->tot_len;
        return sta_netif->output(sta_netif, p, &dstIp);
      }
    }
  } else if (proto == IP_PROTO_ICMP) {
    iphdr->src.addr = netif_ip4_addr(sta_netif)->addr;
    IPH_CHKSUM_SET(iphdr, 0);
    iphdr->_chksum = inet_chksum(iphdr, hlen);
    natBytesRelayed += p->tot_len;
    ip4_addr_t dstIp;
    dstIp.addr = iphdr->dest.addr;
    return sta_netif->output(sta_netif, p, &dstIp);
  }

  return orig_ap_input ? orig_ap_input(p, inp) : ERR_OK;
}

// Hook on STA netif: catches replies from the Internet and forwards to client
static err_t sta_input_hook(struct pbuf *p, struct netif *inp) {
  if (!natActive || !ap_netif || !netif_is_up(ap_netif)) {
    return orig_sta_input ? orig_sta_input(p, inp) : ERR_IF;
  }

  if (p->len < sizeof(struct ip_hdr)) {
    return orig_sta_input ? orig_sta_input(p, inp) : ERR_VAL;
  }

  struct ip_hdr *iphdr = (struct ip_hdr *)p->payload;
  if (IPH_V(iphdr) != 4) {
    return orig_sta_input ? orig_sta_input(p, inp) : ERR_VAL;
  }

  u16_t hlen = IPH_HL(iphdr) * 4;
  u8_t proto = IPH_PROTO(iphdr);

  if (proto == IP_PROTO_UDP && p->len >= hlen + sizeof(struct udp_hdr)) {
    struct udp_hdr *udphdr = (struct udp_hdr *)((u8_t *)p->payload + hlen);
    u16_t destPort = ntohs(udphdr->dest);
    NatEntry *e = getInboundNat(destPort, IP_PROTO_UDP);
    if (e) {
      iphdr->dest.addr = e->client_ip.addr;
      udphdr->dest = htons(e->client_port);
      IPH_CHKSUM_SET(iphdr, 0);
      iphdr->_chksum = inet_chksum(iphdr, hlen);
      udphdr->chksum = 0;
      natBytesRelayed += p->tot_len;
      ip4_addr_t dstIp;
      dstIp.addr = iphdr->dest.addr;
      return ap_netif->output(ap_netif, p, &dstIp);
    }
  } else if (proto == IP_PROTO_TCP && p->len >= hlen + sizeof(struct tcp_hdr)) {
    struct tcp_hdr *tcphdr = (struct tcp_hdr *)((u8_t *)p->payload + hlen);
    u16_t destPort = ntohs(tcphdr->dest);
    NatEntry *e = getInboundNat(destPort, IP_PROTO_TCP);
    if (e) {
      iphdr->dest.addr = e->client_ip.addr;
      tcphdr->dest = htons(e->client_port);
      IPH_CHKSUM_SET(iphdr, 0);
      iphdr->_chksum = inet_chksum(iphdr, hlen);
      tcphdr->chksum = 0;
      ip4_addr_t srcIp;
      srcIp.addr = iphdr->src.addr;
      ip4_addr_t dstIp;
      dstIp.addr = iphdr->dest.addr;
      tcphdr->chksum = inet_chksum_pseudo(p, IP_PROTO_TCP, p->tot_len - hlen,
                                          &srcIp, &dstIp);
      natBytesRelayed += p->tot_len;
      return ap_netif->output(ap_netif, p, &dstIp);
    }
  }

  return orig_sta_input ? orig_sta_input(p, inp) : ERR_OK;
}

inline bool start(IPAddress dnsServer = IPAddress(1, 1, 1, 1)) {
  findNetifs();
  if (!ap_netif || !sta_netif) {
    return false;
  }

  upstreamDns = dnsServer;

  for (uint16_t i = 0; i < kMaxNatEntries; i++) {
    natTable[i].active = false;
  }
  nextPort = kNatPortStart;
  natBytesRelayed = 0;

  if (!natActive) {
    orig_ap_input = ap_netif->input;
    orig_sta_input = sta_netif->input;
    ap_netif->input = ap_input_hook;
    sta_netif->input = sta_input_hook;
    natActive = true;
  }

  dnsUdp.begin(kDnsPort);
  return true;
}

inline void stop() {
  if (natActive) {
    if (ap_netif && orig_ap_input) ap_netif->input = orig_ap_input;
    if (sta_netif && orig_sta_input) sta_netif->input = orig_sta_input;
    natActive = false;
  }
  dnsUdp.stop();
  for (uint16_t i = 0; i < kMaxNatEntries; i++) {
    natTable[i].active = false;
  }
}

inline void loop() {
  if (!natActive) return;

  int packetSize = dnsUdp.parsePacket();
  if (packetSize > 0 && packetSize <= 512) {
    uint8_t dnsBuf[512];
    IPAddress clientIP = dnsUdp.remoteIP();
    uint16_t clientPort = dnsUdp.remotePort();
    dnsUdp.read(dnsBuf, packetSize);

    WiFiUDP upstreamUdp;
    if (upstreamUdp.begin(0)) {
      upstreamUdp.beginPacket(upstreamDns, 53);
      upstreamUdp.write(dnsBuf, packetSize);
      upstreamUdp.endPacket();

      uint32_t start = millis();
      int replySize = 0;
      while (millis() - start < 250) {
        replySize = upstreamUdp.parsePacket();
        if (replySize > 0) break;
        delay(2);
      }

      if (replySize > 0 && replySize <= 512) {
        uint8_t replyBuf[512];
        upstreamUdp.read(replyBuf, replySize);
        dnsUdp.beginPacket(clientIP, clientPort);
        dnsUdp.write(replyBuf, replySize);
        dnsUdp.endPacket();
        natBytesRelayed += (packetSize + replySize);
      }
      upstreamUdp.stop();
    }
  }

  static uint32_t lastSweep = 0;
  if (millis() - lastSweep > 30000) {
    lastSweep = millis();
    u32_t now = millis();
    for (uint16_t i = 0; i < kMaxNatEntries; i++) {
      if (natTable[i].active) {
        u32_t timeout = (natTable[i].proto == IP_PROTO_TCP) ? 120000 : 30000;
        if (now - natTable[i].last_seen > timeout) {
          natTable[i].active = false;
        }
      }
    }
  }
}

inline bool isActive() {
  return natActive;
}

inline uint32_t getBytesRelayed() {
  return natBytesRelayed;
}

} // namespace VpnNat
