// Level Zero Sysman probe — read card power domain limits (read-only).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef int32_t ze_result_t;
typedef void *ze_driver_handle_t, *zes_driver_handle_t, *zes_device_handle_t, *zes_pwr_handle_t;

typedef struct { uint8_t enabled; int32_t power; int32_t interval; } zes_power_sustained_limit_t;
typedef struct { uint8_t enabled; int32_t power; } zes_power_burst_limit_t;
typedef struct { int32_t powerAC, powerDC; } zes_power_peak_limit_t;
typedef struct { uint32_t domain, bus, device, function; } zes_pci_address_t;
typedef struct { int32_t gen; int32_t width; int64_t maxBandwidth; } zes_pci_speed_t;
typedef struct { uint32_t stype; void *pNext; zes_pci_address_t address; zes_pci_speed_t maxSpeed;
                 uint8_t haveBandwidthCounters, havePacketCounters, haveReplayCounters; } zes_pci_properties_t;

int main(void) {
  HMODULE z = LoadLibraryA("C:\\Windows\\System32\\ze_loader.dll");
  if (!z) { printf("ze_loader missing\n"); return 1; }
  ze_result_t (*zeInit)(uint32_t) = (void *)GetProcAddress(z, "zeInit");
  ze_result_t (*zesInit)(uint32_t) = (void *)GetProcAddress(z, "zesInit");
  ze_result_t (*zesDriverGet)(uint32_t *, zes_driver_handle_t *) = (void *)GetProcAddress(z, "zesDriverGet");
  ze_result_t (*zesDeviceGet)(zes_driver_handle_t, uint32_t *, zes_device_handle_t *) = (void *)GetProcAddress(z, "zesDeviceGet");
  ze_result_t (*cardPwr)(zes_device_handle_t, zes_pwr_handle_t *) = (void *)GetProcAddress(z, "zesDeviceGetCardPowerDomain");
  ze_result_t (*getLimits)(zes_pwr_handle_t, zes_power_sustained_limit_t *, zes_power_burst_limit_t *, zes_power_peak_limit_t *) = (void *)GetProcAddress(z, "zesPowerGetLimits");
  ze_result_t (*enumer)(zes_device_handle_t, uint32_t *, zes_pwr_handle_t *) = (void *)GetProcAddress(z, "zesDeviceEnumPowerDomains");
  ze_result_t (*pciProps)(zes_device_handle_t, zes_pci_properties_t *) = (void *)GetProcAddress(z, "zesDevicePciGetProperties");
  if (!zesInit || !zesDriverGet || !zesDeviceGet || !cardPwr || !getLimits) { printf("exports missing\n"); return 1; }
  // Standalone Sysman: NO zeInit first (mixed legacy context breaks zesInit).
  printf("zesInit=0x%x\n", zesInit(0));
  uint32_t n = 0;
  printf("zesDriverGet(count)=0x%x\n", zesDriverGet(&n, NULL));
  printf("drivers=%u\n", n);
  if (!n) return 1;
  zes_driver_handle_t drv[4] = {0};
  if (n > 4) n = 4;
  zesDriverGet(&n, drv);
  uint32_t dn = 0;
  printf("zesDeviceGet(count)=0x%x\n", zesDeviceGet(drv[0], &dn, NULL));
  printf("devices=%u\n", dn);
  if (!dn) return 1;
  zes_device_handle_t dev[8] = {0};
  if (dn > 8) dn = 8;
  zesDeviceGet(drv[0], &dn, dev);
  for (uint32_t i = 0; i < dn; i++) {
    zes_pci_properties_t pci; memset(&pci, 0, sizeof pci); pci.stype = 0x1fff /* ZES_STRUCTURE_TYPE_PCI_PROPERTIES */;
    if (pciProps && pciProps(dev[i], &pci) == 0)
      printf("[%u] bdf=%04x:%02x:%02x.%x gen=%d width=%d\n", i, pci.address.domain, pci.address.bus, pci.address.device, pci.address.function, pci.maxSpeed.gen, pci.maxSpeed.width);
    zes_pwr_handle_t pwr = NULL;
    ze_result_t r = cardPwr(dev[i], &pwr);
    printf("  cardPowerDomain=0x%x\n", r);
    if (r) {
      zes_pwr_handle_t doms[8] = {0};
      uint32_t dc = 8;
      ze_result_t er = enumer ? enumer(dev[i], &dc, doms) : 0xdead;
      printf("  enumPowerDomains=0x%x count=%u\n", er, dc);
      for (uint32_t k = 0; k < dc && k < 8; k++) {
        zes_power_sustained_limit_t s2; memset(&s2, 0, sizeof s2);
        zes_power_burst_limit_t b2; memset(&b2, 0, sizeof b2);
        zes_power_peak_limit_t p2; memset(&p2, 0, sizeof p2);
        ze_result_t gr = getLimits(doms[k], &s2, &b2, &p2);
        printf("   dom[%u] getLimits=0x%x sustained{en=%u W=%d int=%d} burst{en=%u W=%d} peak{AC=%d DC=%d}\n",
               k, gr, s2.enabled, s2.power, s2.interval, b2.enabled, b2.power, p2.powerAC, p2.powerDC);
      }
      continue;
    }
    zes_power_sustained_limit_t s; memset(&s, 0, sizeof s);
    zes_power_burst_limit_t b; memset(&b, 0, sizeof b);
    zes_power_peak_limit_t pk; memset(&pk, 0, sizeof pk);
    r = getLimits(pwr, &s, &b, &pk);
    printf("  getLimits=0x%x sustained{en=%u W=%d int=%d} burst{en=%u W=%d} peak{AC=%d DC=%d}\n",
           r, s.enabled, s.power, s.interval, b.enabled, b.power, pk.powerAC, pk.powerDC);
  }
  return 0;
}
