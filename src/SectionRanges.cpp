#include "SectionRanges.h"
#include <LittleFS.h>

static int parseBankNumber(const String& line) {
    // expects: "-- Bank N"
    int pos = line.lastIndexOf(' ');
    if (pos < 0) return -1;
    return line.substring(pos + 1).toInt();
}

bool SectionRanges::loadFromPresetList(const char* listPath) {
    sections_.clear();
    File f = LittleFS.open(listPath, "r");
    if (!f) return false;

    SectionRange current;
    bool haveOpenSection = false;
    int lastSeenBank = -1;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        if (line.startsWith("#")) {
            // starting a new section; close previous if any
            if (haveOpenSection) {
                // if no endBank decided yet, use last seen bank
                if (current.endBank < current.startBank && lastSeenBank >= current.startBank) {
                    current.endBank = lastSeenBank;
                }
                sections_.push_back(current);
            }
            current = SectionRange{};
            String secName = line.substring(1);
            secName.trim();
            current.label = secName;

            current.startBank = 999999; // sentinel; set when we see first -- Bank
            current.endBank = -1;
            haveOpenSection = true;
        } else if (line.startsWith("-- Bank")) {
            int b = parseBankNumber(line);
            if (b > 0) {
                lastSeenBank = b;
                if (current.startBank == 999999) current.startBank = b; // first bank in section
                // keep expanding end to the max bank we see under this section
                if (b > current.endBank) current.endBank = b;
            }
        } else {
            // preset filename line; ignore here (we only need bank ranges)
        }
    }
    f.close();

    if (haveOpenSection) {
        if (current.endBank < current.startBank) current.endBank = lastSeenBank;
        sections_.push_back(current);
    }

    // If user forgot to add any '# Section' lines, create a single catch-all
    if (sections_.empty()) {
        SectionRange all;
        all.label = "All Banks";
        all.startBank = 1;
        all.endBank = lastSeenBank > 0 ? lastSeenBank : 1;
        sections_.push_back(all);
    }
    currentIndex_ = 0;
    return true;
}
