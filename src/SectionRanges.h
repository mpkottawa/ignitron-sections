#pragma once
#include <vector>
#include <Arduino.h>

struct SectionRange {
    String label;     // e.g., "Nirvana"
    int startBank = 1;
    int endBank = 1;  // inclusive
};

class SectionRanges {
public:
    static SectionRanges& get() {
        static SectionRanges inst; return inst;
    }

    bool loadFromPresetList(const char* listPath = "/PresetList.txt");

    const std::vector<SectionRange>& all() const { return sections_; }
    int count() const { return (int)sections_.size(); }
    int currentIndex() const { return currentIndex_; }
    void setCurrentIndex(int idx) { if (idx>=0 && idx<count()) currentIndex_ = idx; }
    const SectionRange* current() const {
        if (currentIndex_ < 0 || currentIndex_ >= (int)sections_.size()) return nullptr;
        return &sections_[currentIndex_];
    }

private:
    SectionRanges() = default;
    std::vector<SectionRange> sections_;
    int currentIndex_ = 0;
};
