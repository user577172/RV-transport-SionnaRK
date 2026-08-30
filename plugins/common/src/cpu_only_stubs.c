/* CPU-only implementations for plugins that require CUDA or TensorRT. */
#include "plugins.h"
#include <stddef.h>
#include <string.h>

int load_receiver_lib(char *version, receiver_interface_t *interface)
{
  (void)version;
  if (interface != NULL)
    memset(interface, 0, sizeof(*interface));
  return 0;
}

int free_receiver_lib(receiver_interface_t *interface)
{
  if (interface != NULL)
    memset(interface, 0, sizeof(*interface));
  return 0;
}

void init_channel_emulator_libs(const NR_DL_FRAME_PARMS *fp) { (void)fp; }
void init_channel_emulator_worker_thread(void) {}
void free_channel_emulator_libs(void) {}
int is_channel_emulation_enabled(void) { return 0; }
const void *channel_emulator_cir_read_and_apply(void) { return NULL; }
