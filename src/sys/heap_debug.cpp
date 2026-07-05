#include "heap_debug.h"

#include "esp_heap_caps.h"
#include "esp_rom_sys.h"

void heapMark(const char *tag) {
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  Serial.printf(
    "[HEAP] %-18s free=%u min=%u largest=%u alloc=%u free_blocks=%u\n",
    tag,
    info.total_free_bytes,
    info.minimum_free_bytes,
    info.largest_free_block,
    info.allocated_blocks,
    info.free_blocks
  );
  Serial.printf(
    "       ESP free=%u min=%u max_alloc=%u psram=%u\n",
    ESP.getFreeHeap(),
    ESP.getMinFreeHeap(),
    ESP.getMaxAllocHeap(),
    ESP.getPsramSize()
  );
  Serial.flush();

  esp_rom_printf("[HEAP-rom] %-18s free=%u min=%u largest=%u esp_free=%u max_alloc=%u psram=%u\n",
                 tag,
                 info.total_free_bytes,
                 info.minimum_free_bytes,
                 info.largest_free_block,
                 ESP.getFreeHeap(),
                 ESP.getMaxAllocHeap(),
                 ESP.getPsramSize());
}
