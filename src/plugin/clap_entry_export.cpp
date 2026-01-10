// CLAP entry export for clap-wrapper (clap-first builds)

#include <clap/clap.h>

bool ninjam_entry_init(const char* path);
void ninjam_entry_deinit(void);
const void* ninjam_entry_get_factory(const char* factory_id);

extern "C" {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif

const CLAP_EXPORT struct clap_plugin_entry clap_entry = {
    CLAP_VERSION,
    ninjam_entry_init,
    ninjam_entry_deinit,
    ninjam_entry_get_factory
};

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}
