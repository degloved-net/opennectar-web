#ifndef PC_SETTINGS_TABS_H
#define PC_SETTINGS_TABS_H

#include "settings/pc_settings_rows.h"

struct PcSettingsTabs {
    int x[PC_SET_GROUP_COUNT] {};
    int width[PC_SET_GROUP_COUNT] {};
    int fontWidth = 10;

    int hit(int position) const {
        for (int group = 0; group < PC_SET_GROUP_COUNT; ++group)
            if (position >= x[group] && position < x[group] + width[group]) return group;
        return -1;
    }
};

inline PcSettingsTabs pc_settings_tabs_layout(int left, int available, int (*measure)(const char*, int)) {
    PcSettingsTabs tabs;
    constexpr int padding = 8;
    int total;
    do {
        total = 0;
        for (int group = 0; group < PC_SET_GROUP_COUNT; ++group) {
            tabs.width[group] = measure(pc_settings_group_name(group), tabs.fontWidth) + padding;
            total += tabs.width[group];
        }
        if (total <= available || tabs.fontWidth == 1) break;
        --tabs.fontWidth;
    } while (true);

    int spare = available - total;
    for (int group = 0; group < PC_SET_GROUP_COUNT; ++group) {
        const int extra = spare > 0 ? spare / (PC_SET_GROUP_COUNT - group) : 0;
        tabs.x[group] = left;
        tabs.width[group] += extra;
        left += tabs.width[group];
        spare -= extra;
    }
    return tabs;
}

#endif
