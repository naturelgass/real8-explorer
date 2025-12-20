#pragma once
#if defined(ARDUINO) && defined(ESP32)
  #include <esp_attr.h>
  #include <esp_heap_caps.h>
  #define P8_ALLOC(size)  heap_caps_malloc((size), MALLOC_CAP_SPIRAM)
  #define P8_CALLOC(n,sz) heap_caps_calloc((n), (sz), MALLOC_CAP_SPIRAM)
  #define P8_FREE(p)      heap_caps_free((p))
  #ifndef EXT_RAM_ATTR
    #define EXT_RAM_ATTR
  #endif
#else
  #include <stdlib.h>
  #define P8_ALLOC(size)  malloc((size))
  #define P8_CALLOC(n,sz) calloc((n),(sz))
  #define P8_FREE(p)      free((p))
  #define EXT_RAM_ATTR
#endif