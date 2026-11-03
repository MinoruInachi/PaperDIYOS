#include "Apps.h"

namespace pdos {

App* const kApps[] = {
    &ebookApp(),  &badgeApp(),   &clockApp(),   &imageApp(),   &listApp(),
    &qrgenApp(),  &weatherApp(), &newsApp(),    &netInfoApp(), &transferApp(),
    &fontsApp(),  &infoApp(),    &helpApp(),    &settingsApp(),
};

const uint16_t kAppCount = sizeof(kApps) / sizeof(kApps[0]);

}  // namespace pdos
