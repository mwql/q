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
#include <WireGuard-ESP32.h>

#include <Preferences.h>

namespace VpnNat {

enum class Country : uint8_t {
  Direct = 0,
  USA,
  UK,
  Germany,
  Netherlands,
  Custom
};

enum class TunnelState : uint8_t {
  Disabled = 0,
  Direct,
  Connecting,
  Connected,
  Failed
};

struct WgTunnelConfig {
  String privateKey;
  String clientIp = "10.2.0.2";
  String endpoint;
  uint16_t port = 51820;
  String peerPubKey;
  String dns = "1.1.1.1";
  bool valid = false;
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
static struct netif *wg_netif = nullptr;

static netif_input_fn orig_ap_input = nullptr;
static netif_input_fn orig_sta_input = nullptr;
static netif_input_fn orig_wg_input = nullptr;

static bool natActive = false;
static bool wgActive = false;
static uint32_t natBytesRelayed = 0;

static TunnelState tunnelState = TunnelState::Disabled;
static uint32_t tunnelStartTime = 0;
static String tunnelErrorMsg = "";
static WgTunnelConfig activeWgConfig;

static WireGuard wg;
static WiFiUDP dnsUdp;
static const uint16_t kDnsPort = 53;
static IPAddress upstreamDns(1, 1, 1, 1);
static Country currentCountry = Country::Direct;

// Forward declaration
static err_t wg_input_hook(struct pbuf *p, struct netif *inp);

// Parse standard WireGuard .conf file
inline bool parseWireGuardConf(const String& rawText, WgTunnelConfig& cfg) {
  cfg = WgTunnelConfig();
  int start = 0;
  while (start < rawText.length()) {
    int end = rawText.indexOf('\n', start);
    if (end < 0) end = rawText.length();
    String line = rawText.substring(start, end);
    line.trim();
    start = end + 1;

    if (line.startsWith("#") || line.startsWith(";")) continue;
    int eq = line.indexOf('=');
    if (eq <= 0) continue;

    String k = line.substring(0, eq);
    k.trim();
    k.toLowerCase();
    String v = line.substring(eq + 1);
    v.trim();

    if (k == "privatekey") {
      cfg.privateKey = v;
    } else if (k == "publickey") {
      cfg.peerPubKey = v;
    } else if (k == "address") {
      int slash = v.indexOf('/');
      if (slash > 0) cfg.clientIp = v.substring(0, slash);
      else cfg.clientIp = v;
      cfg.clientIp.trim();
    } else if (k == "endpoint") {
      int colon = v.lastIndexOf(':');
      if (colon > 0) {
        cfg.endpoint = v.substring(0, colon);
        cfg.port = v.substring(colon + 1).toInt();
      } else {
        cfg.endpoint = v;
        cfg.port = 51820;
      }
      cfg.endpoint.trim();
    } else if (k == "dns") {
      int comma = v.indexOf(',');
      if (comma > 0) cfg.dns = v.substring(0, comma);
      else cfg.dns = v;
      cfg.dns.trim();
    }
  }

  if (cfg.privateKey.length() >= 40 && cfg.peerPubKey.length() >= 40 && cfg.endpoint.length() >= 4) {
    cfg.valid = true;
    return true;
  }
  return false;
}

inline void loadSavedWgConfig(WgTunnelConfig& cfg) {
  Preferences pref;
  pref.begin("cydhub2", true);
  cfg.valid = pref.getBool("wg_valid", false);
  if (cfg.valid) {
    cfg.privateKey = pref.getString("wg_priv", "");
    cfg.peerPubKey = pref.getString("wg_pub", "");
    cfg.endpoint = pref.getString("wg_end", "");
    cfg.port = pref.getUShort("wg_port", 51820);
    cfg.clientIp = pref.getString("wg_ip", "10.2.0.2");
    cfg.dns = pref.getString("wg_dns", "1.1.1.1");
  }
  pref.end();
}

inline bool saveWgConfig(const WgTunnelConfig& cfg) {
  if (cfg.privateKey.length() < 30 || cfg.peerPubKey.length() < 30 || cfg.endpoint.length() < 3) {
    return false;
  }
  Preferences pref;
  pref.begin("cydhub2", false);
  pref.putString("wg_priv", cfg.privateKey);
  pref.putString("wg_pub", cfg.peerPubKey);
  pref.putString("wg_end", cfg.endpoint);
  pref.putUShort("wg_port", cfg.port);
  pref.putString("wg_ip", cfg.clientIp);
  pref.putString("wg_dns", cfg.dns);
  pref.putBool("wg_valid", true);
  pref.end();
  return true;
}

// Find netif pointers in LwIP list
inline void findNetifs() {
  ap_netif = nullptr;
  sta_netif = nullptr;
  wg_netif = nullptr;
  for (struct netif *n = netif_list; n != nullptr; n = n->next) {
    if (n->name[0] == 'a' && n->name[1] == 'p') {
      ap_netif = n;
    } else if (n->name[0] == 's' && n->name[1] == 't') {
      sta_netif = n;
    } else if (n->name[0] == 'w' && n->name[1] == 'g') {
      wg_netif = n;
    }
  }
}

// Start WireGuard encrypted tunnel
inline bool startWireGuard(Country c, const String& customHostPort = "") {
  loadSavedWgConfig(activeWgConfig);

  // If custom server parameter was passed in host:port:pubkey:privkey:ip format
  if (c == Country::Custom && customHostPort.length() > 20) {
    int c1 = customHostPort.indexOf(':');
    if (c1 > 0) {
      activeWgConfig.endpoint = customHostPort.substring(0, c1);
      int c2 = customHostPort.indexOf(':', c1 + 1);
      if (c2 > 0) {
        activeWgConfig.port = customHostPort.substring(c1 + 1, c2).toInt();
        int c3 = customHostPort.indexOf(':', c2 + 1);
        if (c3 > 0) {
          activeWgConfig.peerPubKey = customHostPort.substring(c2 + 1, c3);
          int c4 = customHostPort.indexOf(':', c3 + 1);
          if (c4 > 0) {
            activeWgConfig.privateKey = customHostPort.substring(c3 + 1, c4);
            activeWgConfig.clientIp = customHostPort.substring(c4 + 1);
          } else {
            activeWgConfig.privateKey = customHostPort.substring(c3 + 1);
          }
          activeWgConfig.valid = true;
          saveWgConfig(activeWgConfig);
        }
      }
    }
  }

  if (!activeWgConfig.valid) {
    tunnelState = TunnelState::Failed;
    tunnelErrorMsg = "No config (open 192.168.4.1)";
    return false;
  }

  // Ensure system timestamp is modern so WireGuard handshake passes validation
  time_t nowSec = time(nullptr);
  if (nowSec < 1600000000) {
    struct timeval tv = { .tv_sec = 1760544000, .tv_usec = 0 }; // 2026 baseline
    settimeofday(&tv, NULL);
  }

  if (activeWgConfig.dns.length()) {
    upstreamDns.fromString(activeWgConfig.dns);
  }

  IPAddress clientIp;
  if (!clientIp.fromString(activeWgConfig.clientIp)) {
    clientIp = IPAddress(10, 2, 0, 2);
  }

  wg.setDefaultRoute(true);
  tunnelState = TunnelState::Connecting;
  tunnelStartTime = millis();
  tunnelErrorMsg = "";

  bool ok = wg.begin(clientIp, activeWgConfig.privateKey.c_str(), activeWgConfig.endpoint.c_str(), activeWgConfig.peerPubKey.c_str(), activeWgConfig.port);
  if (!ok) {
    tunnelState = TunnelState::Failed;
    tunnelErrorMsg = "WireGuard begin failed";
    return false;
  }

  findNetifs();
  if (wg_netif) {
    orig_wg_input = wg_netif->input;
    wg_netif->input = wg_input_hook;
    wgActive = true;

    // Allow handshake packet to complete round-trip
    uint32_t tWait = millis();
    while (!netif_is_link_up(wg_netif) && millis() - tWait < 2500) {
      delay(50);
    }
    if (netif_is_link_up(wg_netif)) {
      tunnelState = TunnelState::Connected;
    } else {
      tunnelState = TunnelState::Connecting;
    }
    return true;
  }

  tunnelState = TunnelState::Failed;
  tunnelErrorMsg = "Interface not found";
  return false;
}

inline void stopWireGuard() {
  if (wgActive) {
    if (wg_netif && orig_wg_input) {
      wg_netif->input = orig_wg_input;
    }
    wg.end();
    wg_netif = nullptr;
    wgActive = false;
  }
  tunnelState = TunnelState::Disabled;
  tunnelErrorMsg = "";
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
  if (!natActive) {
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_IF;
  }

  // Need at least Ethernet header + minimal IP header
  if (p->len < kEthHdrLen + sizeof(struct ip_hdr)) {
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_VAL;
  }

  struct eth_hdr *eth = (struct eth_hdr *)p->payload;
  if (eth->type != PP_HTONS(ETHTYPE_IP)) {
    // Pass ARP, IPv6 to LwIP
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_VAL;
  }

  // Strip 14-byte Ethernet header
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

  bool isDnsQuery = (proto == IP_PROTO_UDP) && (p->len >= hlen + sizeof(struct udp_hdr)) &&
                    (((struct udp_hdr *)((u8_t *)p->payload + hlen))->dest == PP_HTONS(53));

  if (isToLocal && !isDnsQuery) {
    // Deliver local traffic (DHCP server port 67, local web, etc.)
    pbuf_add_header(p, kEthHdrLen);
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_OK;
  }

  // Select outbound network interface:
  // If WireGuard is active and connected, route through WireGuard tunnel!
  // Otherwise, route through standard Wi-Fi Station interface!
  // Select outbound network interface:
  // If Direct mode, route through standard Wi-Fi Station interface!
  // If VPN mode, route through WireGuard tunnel!
  // KILL-SWITCH: If VPN mode but tunnel is not ready, DROP packet so client never leaks real IP!
  struct netif *out_netif = nullptr;
  if (currentCountry == Country::Direct) {
    out_netif = sta_netif;
  } else if (wgActive && wg_netif && netif_is_link_up(wg_netif)) {
    out_netif = wg_netif;
  } else {
    // Drop packet until encrypted tunnel is established to avoid leaking client's real IP
    pbuf_add_header(p, kEthHdrLen);
    return orig_ap_input ? orig_ap_input(p, inp) : ERR_IF;
  }

  ip4_addr_t dstIp;
  dstIp.addr = iphdr->dest.addr;
  const ip4_addr_t *src_ip = netif_ip4_addr(out_netif);

  if (proto == IP_PROTO_TCP) {
    if (p->len >= hlen + sizeof(struct tcp_hdr)) {
      struct tcp_hdr *tcphdr = (struct tcp_hdr *)((u8_t *)p->payload + hlen);
      u16_t srcPort = ntohs(tcphdr->src);
      ip4_addr_t srcIp;
      srcIp.addr = iphdr->src.addr;

      NatEntry *e = getOutboundNat(&srcIp, srcPort, IP_PROTO_TCP, &dstIp);
      if (e) {
        iphdr->src.addr = src_ip->addr;
        tcphdr->src = htons(e->mapped_port);

        if (TCPH_FLAGS(tcphdr) & (TCP_FIN | TCP_RST)) {
          e->last_seen = millis() - (120000 - 10000);
        }

        IPH_CHKSUM_SET(iphdr, 0);
        iphdr->_chksum = inet_chksum(iphdr, hlen);

        u16_t tcp_len = p->tot_len - hlen;
        tcphdr->chksum = 0;
        pbuf_remove_header(p, hlen);
        tcphdr->chksum = inet_chksum_pseudo(p, IP_PROTO_TCP, tcp_len, src_ip, &dstIp);
        pbuf_add_header(p, hlen);

        natBytesRelayed += p->tot_len;

        out_netif->output(out_netif, p, &dstIp);
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

      // Transparent DNS redirection: redirect to upstream DNS through tunnel
      if (udphdr->dest == PP_HTONS(53)) {
        iphdr->dest.addr = (uint32_t)upstreamDns;
        dstIp.addr = (uint32_t)upstreamDns;
      }

      NatEntry *e = getOutboundNat(&srcIp, srcPort, IP_PROTO_UDP, &dstIp);
      if (e) {
        iphdr->src.addr = src_ip->addr;
        udphdr->src = htons(e->mapped_port);

        IPH_CHKSUM_SET(iphdr, 0);
        iphdr->_chksum = inet_chksum(iphdr, hlen);
        udphdr->chksum = 0;

        natBytesRelayed += p->tot_len;

        out_netif->output(out_netif, p, &dstIp);
        pbuf_free(p);
        return ERR_OK;
      }
    }
  } else if (proto == IP_PROTO_ICMP) {
    if (p->len >= hlen + 8) {
      struct icmp_echo_hdr *icmphdr = (struct icmp_echo_hdr *)((u8_t *)p->payload + hlen);
      if (icmphdr->type == 8) {
        u16_t id = ntohs(icmphdr->id);
        ip4_addr_t srcIp;
        srcIp.addr = iphdr->src.addr;

        NatEntry *e = getOutboundNat(&srcIp, id, IP_PROTO_ICMP, &dstIp);
        if (e) {
          iphdr->src.addr = src_ip->addr;
          icmphdr->id = htons(e->mapped_port);

          u16_t icmp_len = p->tot_len - hlen;
          icmphdr->chksum = 0;
          pbuf_remove_header(p, hlen);
          icmphdr->chksum = inet_chksum(p->payload, icmp_len);
          pbuf_add_header(p, hlen);

          IPH_CHKSUM_SET(iphdr, 0);
          iphdr->_chksum = inet_chksum(iphdr, hlen);

          natBytesRelayed += p->tot_len;

          out_netif->output(out_netif, p, &dstIp);
          pbuf_free(p);
          return ERR_OK;
        }
      }
    }
  }

  pbuf_add_header(p, kEthHdrLen);
  return orig_ap_input ? orig_ap_input(p, inp) : ERR_OK;
}

// Hook on STA netif: catches direct replies from home router (when not tunneled)
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
      if (icmphdr->type == 0) {
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

  // Not client NAT traffic -> let local CYD stack (including WireGuard UDP socket) handle it
  pbuf_add_header(p, kEthHdrLen);
  return orig_sta_input ? orig_sta_input(p, inp) : ERR_OK;
}

// Hook on WireGuard netif: catches decrypted inbound packets from the VPN tunnel
static err_t wg_input_hook(struct pbuf *p, struct netif *inp) {
  if (!natActive || !ap_netif || !netif_is_up(ap_netif)) {
    return orig_wg_input ? orig_wg_input(p, inp) : ERR_IF;
  }

  if (p->len < sizeof(struct ip_hdr)) {
    return orig_wg_input ? orig_wg_input(p, inp) : ERR_VAL;
  }

  struct ip_hdr *iphdr = (struct ip_hdr *)p->payload;
  if (IPH_V(iphdr) != 4) {
    return orig_wg_input ? orig_wg_input(p, inp) : ERR_VAL;
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

        // Deliver decrypted packet to client phone
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
      if (icmphdr->type == 0) {
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

  return orig_wg_input ? orig_wg_input(p, inp) : ERR_OK;
}

inline bool start(IPAddress dnsServer = IPAddress(1, 1, 1, 1), Country country = Country::Direct, const String& customSrv = "") {
  findNetifs();
  if (!ap_netif || !sta_netif) {
    return false;
  }

  upstreamDns = dnsServer;
  currentCountry = country;

  // Apply DNS to SoftAP DHCP server so connected devices get valid DNS
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

  // If a country VPN or custom tunnel is selected, start WireGuard encrypted VPN
  if (country != Country::Direct) {
    startWireGuard(country, customSrv);
  }

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
  stopWireGuard();
  dnsUdp.stop();
  portENTER_CRITICAL(&natMux);
  for (uint16_t i = 0; i < kMaxNatEntries; i++) {
    natTable[i].active = false;
  }
  portEXIT_CRITICAL(&natMux);
}

inline void loop() {
  if (!natActive) return;

  // Backup fallback DNS relay on port 53
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

  // Periodic tunnel status monitor
  static uint32_t lastTunnelCheck = 0;
  if (wgActive && millis() - lastTunnelCheck > 1000) {
    lastTunnelCheck = millis();
    if (wg_netif && netif_is_link_up(wg_netif)) {
      tunnelState = TunnelState::Connected;
    } else if (tunnelState == TunnelState::Connecting && millis() - tunnelStartTime > 15000) {
      tunnelState = TunnelState::Failed;
      tunnelErrorMsg = "Handshake timeout";
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

inline bool isVpnEncrypted() {
  return wgActive && wg_netif && netif_is_link_up(wg_netif);
}

inline bool isTunnelConnected() {
  return tunnelState == TunnelState::Connected;
}

inline TunnelState getTunnelState() {
  return tunnelState;
}

inline String getTunnelStateStr() {
  switch (tunnelState) {
    case TunnelState::Connected:  return "Connected (Encrypted)";
    case TunnelState::Connecting: return "Connecting...";
    case TunnelState::Failed:     return "Offline (Needs Config)";
    case TunnelState::Direct:     return "Direct Wi-Fi";
    case TunnelState::Disabled:   return "Disabled";
    default:                      return "Unknown";
  }
}

inline const WgTunnelConfig& getActiveWgConfig() {
  return activeWgConfig;
}

inline uint32_t getBytesRelayed() {
  return natBytesRelayed;
}

} // namespace VpnNat
