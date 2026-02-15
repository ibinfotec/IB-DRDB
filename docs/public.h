// public.h - HAStorRepCtl ABI (Phase 1A)
// Must remain stable once shipped.

#pragma once
#include <stdint.h>

#define HA_MAGIC 0x48515250u /* 'HQRP' */

#define HA_DATA_MAX 4096
#define HA_MAX_EVENTS_PER_BATCH 64

typedef enum _HA_EVENT_TYPE {
  HA_EVT_NONE     = 0,
  HA_EVT_TEST     = 1,
  HA_EVT_OVERFLOW = 2,
  HA_EVT_ERROR    = 3
} HA_EVENT_TYPE;

#pragma pack(push, 1)

typedef struct _HA_EVENT {
  uint64_t volume_id;
  uint64_t seq;
  uint32_t event_type;
  uint32_t flags;
  uint64_t offset;
  uint32_t len;
  uint32_t data_len;
  uint64_t timestamp_ns;
  uint32_t pid;
  uint32_t reserved;
  uint8_t  data[HA_DATA_MAX];
} HA_EVENT;

typedef struct _HA_REGISTER_ENGINE_IN {
  uint32_t magic;     // HA_MAGIC
  uint32_t pid;
  uint32_t features;  // reserved for future
  uint32_t reserved;
} HA_REGISTER_ENGINE_IN;

typedef struct _HA_REGISTER_ENGINE_OUT {
  uint32_t driver_version; // e.g. 0x00010000
  uint32_t limits;         // max events per batch etc.
  uint64_t session_id;
} HA_REGISTER_ENGINE_OUT;

typedef struct _HA_GET_EVENTS_IN {
  uint64_t session_id;
  uint32_t max_events;   // <= HA_MAX_EVENTS_PER_BATCH
  uint32_t timeout_ms;   // 0 means wait indefinitely (Phase1: allow 5s max internally)
} HA_GET_EVENTS_IN;

typedef struct _HA_GET_EVENTS_OUT {
  uint32_t count;
  uint32_t reserved;
  // Followed by HA_EVENT[count]
} HA_GET_EVENTS_OUT;

typedef struct _HA_ACK_EVENTS_IN {
  uint64_t session_id;
  uint64_t ack_seq;     // last acked seq (future use)
  uint32_t ack_stage;   // 0=applied, 1=flushed
  uint32_t reserved;
} HA_ACK_EVENTS_IN;

#pragma pack(pop)

// IOCTL
#ifdef _WIN32
#include <winioctl.h>
#define HA_CTL_CODE(_f) CTL_CODE(FILE_DEVICE_UNKNOWN, (_f), METHOD_BUFFERED, FILE_ANY_ACCESS)
#else
#define HA_CTL_CODE(_f) (_f)
#endif

#define IOCTL_HA_REGISTER_ENGINE HA_CTL_CODE(0x800)
#define IOCTL_HA_GET_EVENTS      HA_CTL_CODE(0x801)
#define IOCTL_HA_ACK_EVENTS      HA_CTL_CODE(0x802)
