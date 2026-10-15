#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "esp_netif.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/ip.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/etharp.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ethernet.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/tcp.h"
#include "lwip/prot/udp.h"
#include "lwip/prot/icmp.h"

namespace VpnNat {

enum class Country : uint8_t {
  Direct = 0,
  USA,
  UK,
  Germany,
  Netherlands,
  Custom
};

struct NatEntry {
  ip4_addr_t client_ip;
  ip4_addr_t original_dest_ip;
  u16_t client_port;
  u16_t mapped_port;
  u8_t proto;
  u32_t last_seen;
  bool active;
};

static const uint16_t kMaxNatEntries = 256;
static const uint16_t kNatPortStart = 40000;
static const uint16_t kNatPortEnd   = 65000;
static const size_t   kEthHdrLen    = sizeof(struct eth_hdr);

static NatEntry natTable[kMaxNatEntries];
static uint16_t nextPort = kNatPortStart;
static portMUX_TYPE natMux = portMUX_INITIALIZER_UNLOCKED;

static struct netif *ap_netif = nullptr;
static struct netif *sta_netif = nullptr;
static netif_input_fn orig_ap_input = nullptr;
static netif_input_fn orig_sta_input = nullptr;

static bool natActive = false;
static uint32_t natBytesRelayed = 0;

static WiFiUDP dnsUdp;
static const uint16_t kDnsPort = 53;
static IPAddress upstreamDns(1, 1, 1, 1);

static Country currentCountry = Country::Direct;
static IPAddress countryGatewayIp(0, 0, 0, 0);
static uint16_t countryGatewayPort = 0;

// Preset Exit Gateways / Regional DNS for Country VPN mode
inline void setCountryGateway(Country c, const String& customHostPort = "") {
  currentCountry = c;
  countryGatewayIp = IPAddress(0, 0, 0, 0);
  countryGatewayPort = 0;

  switch (c) {
    case Country::Direct:
      break;
    case Country::USA:
      // USA Anycast / OpenDNS
      upstreamDns = IPAddress(208, 67, 222, 222);
      break;
    case Country::UK:
      // UK CleanBrowsing / Anycast DNS
      upstreamDns = IPAddress(185, 228, 168, 168);
      break;
    case Country::Germany:
      // Germany FFM DNS
      upstreamDns = IPAddress(91, 239, 100, 100);
      break;
    case Country::Netherlands:
      // Netherlands Privacy DNS
      upstreamDns = IPAddress(84, 200, 69, 80);
      break;
    case Country::Custom: {
      if (customHostPort.length()) {
        int colon = customHostPort.indexOf(':');
        String h = (colon > 0) ? customHostPort.substring(0, colon) : customHostPort;
        countryGatewayPort = (colon > 0) ? customHostPort.substring(colon + 1).toInt() : 8080;
        if (!countryGatewayIp.fromString(h)) {
          WiFi.hostByName(h.c_str(), countryGatewayIp);
        }
      }
      break;
    }
  }
}

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
inline NatEntry* getOutboundNat(const ip4_addr_t *client_ip, u16_t client_port, u8_t proto, const ip4_addr_t *dest_ip = nullptr) {
  portENTER_CRITICAL(&natMux);
  u32_t now = millis();
  int oldestIdx = -1;
  u32_t oldestTime = 0xFFFFFFFF;

  for (uint16_t i = 0; i < kMaxNatEntries; i++) {
    if (natTable[i].active) {
      if (natTable[i].proto == proto &&
          natTable[i].client_port == client_port &&
          ip4_addr_cmp(&natTable[i].client_ip, client_ip)) {
        natTable[i].last_seen = now;
        if (dest_ip) natTable[i].original_dest_ip = *dest_ip;
        portEXIT_CRITICAL(&natMux);
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
    e->original_dest_ip = dest_ip ? *dest_ip : (ip4_addr_t){0};
    e->proto = proto;
    e->last_seen = now;
    e->mapped_port = nextPort++;
    if (nextPort >= kNatPortEnd) nextPort = kNatPortStart;
    e->active = true;
    portEXIT_CRITICAL(&natMux);
    return e;
  }
  portEXIT_CRITICAL(&natMux);
  return nullptr;
}

// Look up inbound NAT entry by mapped destination port and protocol
inline NatEntry* getInboundNat(u16_t mapped_port, u8_t proto) {
  portENTER_CRITICAL(&natMux);
  u32_t now = millis();
  for (uint16_t i = 0; i < kMaxNatEntries; i++) {
    if (natTable[i].active &&
        natTable[i].proto == proto &&
        natTable[i].mapped_port == mapped_port) {
      natTable[i].last_seen = now;
      portEXIT_CRITICAL(&natMux);
      return &natTable[i];
    }
  }
  portEXIT_CRITICAL(&natMux);
  return nullptr;
}

// Hook on AP netif: catches packets from connected client phone/laptop
static err_t ap_input_hook(struct pbuf *p, struct netif *inp) {
  if (!natActive || !sta_netif || !netif_is_up(sta_netif) || ip4_addr_isany_val(*netif_ip4_addr(sta_netif))) {
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_IF;
  }

  // Need at least Ethernet header + minimal IP header
  if (p->len < kEthHdrLen + sizeof(struct ip_hdr)) {
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_VAL;
  }

  struct eth_hdr *eth = (struct eth_hdr *)p->payload;
  if (eth->type != PP_HTONS(ETHTYPE_IP)) {
    // ARP (0x0806), IPv6 (0x86DD), etc. -> let LwIP handle normally
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_VAL;
  }

  // Strip the 14-byte Ethernet header so payload points directly to IPv4 header
  if (pbuf_remove_header(p, kEthHdrLen) != 0) {
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_VAL;
  }

  struct ip_hdr *iphdr = (struct ip_hdr *)p->payload;
  if (IPH_V(iphdr) != 4) {
    pbuf_add_header(p, kEthHdrLen);
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_VAL;
  }

  u16_t hlen = IPH_HL(iphdr) * 4;
  u8_t proto = IPH_PROTO(iphdr);

  // Check if packet is destined for the CYD AP itself (192.168.4.1), or broadcast/multicast
  bool isToLocal = (iphdr->dest.addr == netif_ip4_addr(inp)->addr) ||
                   ip4_addr_isbroadcast(&iphdr->dest, inp) ||
                   ip4_addr_ismulticast(&iphdr->dest) ||
                   (iphdr->dest.addr == 0xFFFFFFFF);

  // Check for DNS query (UDP port 53)
  bool isDnsQuery = (proto == IP_PROTO_UDP) && (p->len >= hlen + sizeof(struct udp_hdr)) &&
                    (((struct udp_hdr *)((u8_t *)p->payload + hlen))->dest == PP_HTONS(53));

  if (isToLocal && !isDnsQuery) {
    // Local traffic (DHCP server port 67, local web, etc.): deliver to CYD stack
    pbuf_add_header(p, kEthHdrLen);
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_OK;
  }

  ip4_addr_t dstIp;
  dstIp.addr = iphdr->dest.addr;

  // Outbound Internet Packet NAT
  if (proto == IP_PROTO_TCP) {
    if (p->len >= hlen + sizeof(struct tcp_hdr)) {
      struct tcp_hdr *tcphdr = (struct tcp_hdr *)((u8_t *)p->payload + hlen);
      u16_t srcPort = ntohs(tcphdr->src);
      ip4_addr_t srcIp;
      srcIp.addr = iphdr->src.addr;

      NatEntry *e = getOutboundNat(&srcIp, srcPort, IP_PROTO_TCP, &dstIp);
      if (e) {
        // Rewrite source IP to CYD STA IP
        iphdr->src.addr = netif_ip4_addr(sta_netif)->addr;
        tcphdr->src = htons(e->mapped_port);

        if (TCPH_FLAGS(tcphdr) & (TCP_FIN | TCP_RST)) {
          e->last_seen = millis() - (120000 - 10000);
        }

        IPH_CHKSUM_SET(iphdr, 0);
        iphdr->_chksum = inet_chksum(iphdr, hlen);

        u16_t tcp_len = p->tot_len - hlen;

        // Calculate TCP pseudo-header checksum over TCP segment
        tcphdr->chksum = 0;
        pbuf_remove_header(p, hlen);
        tcphdr->chksum = inet_chksum_pseudo(p, IP_PROTO_TCP, tcp_len, netif_ip4_addr(sta_netif), &dstIp);
        pbuf_add_header(p, hlen);

        natBytesRelayed += p->tot_len;

        // etharp_output will prepend STA Ethernet header and transmit over Wi-Fi
        sta_netif->output(sta_netif, p, &dstIp);
        pbuf_free(p);
        return ERR_OK;
      }
    }
  } else if (proto == IP_PROTO_UDP) {
    if (p->len >= hlen + sizeof(struct udp_hdr)) {
      struct udp_hdr *udphdr = (struct udp_hdr *)((u8_t *)p->payload + hlen);
      u16_t srcPort = ntohs(udphdr->src);
      ip4_addr_t srcIp;
      srcIp.addr = iphdr->src.addr;

      // Transparent DNS redirection: redirect queries to selected upstream DNS server
      if (udphdr->dest == PP_HTONS(53)) {
        iphdr->dest.addr = (uint32_t)upstreamDns;
        dstIp.addr = (uint32_t)upstreamDns;
      }

      NatEntry *e = getOutboundNat(&srcIp, srcPort, IP_PROTO_UDP, &dstIp);
      if (e) {
        iphdr->src.addr = netif_ip4_addr(sta_netif)->addr;
        udphdr->src = htons(e->mapped_port);

        IPH_CHKSUM_SET(iphdr, 0);
        iphdr->_chksum = inet_chksum(iphdr, hlen);
        udphdr->chksum = 0; // IPv4 UDP checksum optional

        natBytesRelayed += p->tot_len;

        sta_netif->output(sta_netif, p, &dstIp);
        pbuf_free(p);
        return ERR_OK;
      }
    }
  } else if (proto == IP_PROTO_ICMP) {
    if (p->len >= hlen + 8) {
      struct icmp_echo_hdr *icmphdr = (struct icmp_echo_hdr *)((u8_t *)p->payload + hlen);
      if (icmphdr->type == 8) { // Echo request
        u16_t id = ntohs(icmphdr->id);
        ip4_addr_t srcIp;
        srcIp.addr = iphdr->src.addr;

        NatEntry *e = getOutboundNat(&srcIp, id, IP_PROTO_ICMP, &dstIp);
        if (e) {
          iphdr->src.addr = netif_ip4_addr(sta_netif)->addr;
          icmphdr->id = htons(e->mapped_port);

          u16_t icmp_len = p->tot_len - hlen;
          icmphdr->chksum = 0;
          pbuf_remove_header(p, hlen);
          icmphdr->chksum = inet_chksum(p->payload, icmp_len);
          pbuf_add_header(p, hlen);

          IPH_CHKSUM_SET(iphdr, 0);
          iphdr->_chksum = inet_chksum(iphdr, hlen);

          natBytesRelayed += p->tot_len;

          sta_netif->output(sta_netif, p, &dstIp);
          pbuf_free(p);
          return ERR_OK;
        }
      }
    }
  }

  // Restore Ethernet header if not forwarded
  pbuf_add_header(p, kEthHdrLen);
  return orig_ap_input ? orig_ap_input(p, inp) : ERR_OK;
}

// Hook on STA netif: catches replies from the Internet and forwards back to client phone
static err_t sta_input_hook(struct pbuf *p, struct netif *inp) {
  if (!natActive || !ap_netif || !netif_is_up(ap_netif)) {
    return orig_sta_input ? orig_sta_input(p, inp) : ERR_IF;
  }

  if (p->len < kEthHdrLen + sizeof(struct ip_hdr)) {
    return orig_sta_input ? orig_sta_input(p, inp) : ERR_VAL;
  }

  struct eth_hdr *eth = (struct eth_hdr *)p->payload;
  if (eth->type != PP_HTONS(ETHTYPE_IP)) {
    return orig_sta_input ? orig_sta_input(p, inp) : ERR_VAL;
  }

  if (pbuf_remove_header(p, kEthHdrLen) != 0) {
    return orig_sta_input ? orig_sta_input(p, inp) : ERR_VAL;
  }

  struct ip_hdr *iphdr = (struct ip_hdr *)p->payload;
  if (IPH_V(iphdr) != 4) {
    pbuf_add_header(p, kEthHdrLen);
    return orig_sta_input ? orig_sta_input(p, inp) : ERR_VAL;
  }

  u16_t hlen = IPH_HL(iphdr) * 4;
  u8_t proto = IPH_PROTO(iphdr);

  if (proto == IP_PROTO_TCP) {
    if (p->len >= hlen + sizeof(struct tcp_hdr)) {
      struct tcp_hdr *tcphdr = (struct tcp_hdr *)((u8_t *)p->payload + hlen);
      u16_t destPort = ntohs(tcphdr->dest);
      NatEntry *e = getInboundNat(destPort, IP_PROTO_TCP);
      if (e) {
        iphdr->dest.addr = e->client_ip.addr;
        tcphdr->dest = htons(e->client_port);

        if (TCPH_FLAGS(tcphdr) & (TCP_FIN | TCP_RST)) {
          e->last_seen = millis() - (120000 - 10000);
        }

        IPH_CHKSUM_SET(iphdr, 0);
        iphdr->_chksum = inet_chksum(iphdr, hlen);

        ip4_addr_t srcIp, dstIp;
        srcIp.addr = iphdr->src.addr;
        dstIp.addr = iphdr->dest.addr;

        u16_t tcp_len = p->tot_len - hlen;
        tcphdr->chksum = 0;
        pbuf_remove_header(p, hlen);
        tcphdr->chksum = inet_chksum_pseudo(p, IP_PROTO_TCP, tcp_len, &srcIp, &dstIp);
        pbuf_add_header(p, hlen);

        natBytesRelayed += p->tot_len;

        ap_netif->output(ap_netif, p, &dstIp);
        pbuf_free(p);
        return ERR_OK;
      }
    }
  } else if (proto == IP_PROTO_UDP) {
    if (p->len >= hlen + sizeof(struct udp_hdr)) {
      struct udp_hdr *udphdr = (struct udp_hdr *)((u8_t *)p->payload + hlen);
      u16_t destPort = ntohs(udphdr->dest);
      NatEntry *e = getInboundNat(destPort, IP_PROTO_UDP);
      if (e) {
        iphdr->dest.addr = e->client_ip.addr;
        udphdr->dest = htons(e->client_port);

        // If client originally queried 192.168.4.1 for DNS, restore 192.168.4.1 as the source IP
        if (e->original_dest_ip.addr != 0) {
          iphdr->src.addr = e->original_dest_ip.addr;
        }

        IPH_CHKSUM_SET(iphdr, 0);
        iphdr->_chksum = inet_chksum(iphdr, hlen);
        udphdr->chksum = 0;

        natBytesRelayed += p->tot_len;

        ip4_addr_t dstIp;
        dstIp.addr = e->client_ip.addr;
        ap_netif->output(ap_netif, p, &dstIp);
        pbuf_free(p);
        return ERR_OK;
      }
    }
  } else if (proto == IP_PROTO_ICMP) {
    if (p->len >= hlen + 8) {
      struct icmp_echo_hdr *icmphdr = (struct icmp_echo_hdr *)((u8_t *)p->payload + hlen);
      if (icmphdr->type == 0) { // Echo reply
        u16_t id = ntohs(icmphdr->id);
        NatEntry *e = getInboundNat(id, IP_PROTO_ICMP);
        if (e) {
          iphdr->dest.addr = e->client_ip.addr;
          icmphdr->id = htons(e->client_port);

          u16_t icmp_len = p->tot_len - hlen;
          icmphdr->chksum = 0;
          pbuf_remove_header(p, hlen);
          icmphdr->chksum = inet_chksum(p->payload, icmp_len);
          pbuf_add_header(p, hlen);

          IPH_CHKSUM_SET(iphdr, 0);
          iphdr->_chksum = inet_chksum(iphdr, hlen);

          natBytesRelayed += p->tot_len;

          ip4_addr_t dstIp;
          dstIp.addr = e->client_ip.addr;
          ap_netif->output(ap_netif, p, &dstIp);
          pbuf_free(p);
          return ERR_OK;
        }
      }
    }
  }

  // Not our NAT packet - let CYD STA handle it
  pbuf_add_header(p, kEthHdrLen);
  return orig_sta_input ? orig_sta_input(p, inp) : ERR_OK;
}

inline bool start(IPAddress dnsServer = IPAddress(1, 1, 1, 1), Country country = Country::Direct, const String& customSrv = "") {
  findNetifs();
  if (!ap_netif || !sta_netif) {
    return false;
  }

  upstreamDns = dnsServer;
  setCountryGateway(country, customSrv);

  // Apply custom DNS to SoftAP DHCP server so connected devices get real DNS directly
  esp_netif_t *esp_ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (esp_ap) {
    esp_netif_dhcps_stop(esp_ap);
    esp_netif_dns_info_t d;
    d.ip.type = ESP_IPADDR_TYPE_V4;
    d.ip.u_addr.ip4.addr = (uint32_t)upstreamDns;
    esp_netif_set_dns_info(esp_ap, ESP_NETIF_DNS_MAIN, &d);
    esp_netif_dhcps_start(esp_ap);
  }

  portENTER_CRITICAL(&natMux);
  for (uint16_t i = 0; i < kMaxNatEntries; i++) {
    natTable[i].active = false;
  }
  nextPort = kNatPortStart;
  portEXIT_CRITICAL(&natMux);
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
  portENTER_CRITICAL(&natMux);
  for (uint16_t i = 0; i < kMaxNatEntries; i++) {
    natTable[i].active = false;
  }
  portEXIT_CRITICAL(&natMux);
}

inline void loop() {
  if (!natActive) return;

  // Backup fallback DNS relay on port 53 (for any query caught by socket)
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
      while (millis() - start < 150) {
        replySize = upstreamUdp.parsePacket();
        if (replySize > 0) break;
        delay(1);
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

  // Periodic NAT table sweep to clean up dead sessions
  static uint32_t lastSweep = 0;
  if (millis() - lastSweep > 5000) {
    lastSweep = millis();
    u32_t now = millis();
    portENTER_CRITICAL(&natMux);
    for (uint16_t i = 0; i < kMaxNatEntries; i++) {
      if (natTable[i].active) {
        u32_t timeout = (natTable[i].proto == IP_PROTO_TCP) ? 120000 : 30000;
        if (now - natTable[i].last_seen > timeout) {
          natTable[i].active = false;
        }
      }
    }
    portEXIT_CRITICAL(&natMux);
  }
}

inline bool isActive() {
  return natActive;
}

inline uint32_t getBytesRelayed() {
  return natBytesRelayed;
}

} // namespace VpnNat
