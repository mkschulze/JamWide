// CLAP entry export for clap-wrapper (clap-first builds)
// JamWide Plugin

#include <clap/clap.h>

bool jamwide_entry_init(const char* path);
void jamwide_entry_deinit(void);
const void* jamwide_entry_get_factory(const char* factory_id);

extern "C" {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif

const CLAP_EXPORT struct clap_plugin_entry clap_entry = {
    CLAP_VERSION,
    jamwide_entry_init,
    jamwide_entry_deinit,
    jamwide_entry_get_factory
};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
