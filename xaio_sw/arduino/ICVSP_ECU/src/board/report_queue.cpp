#include "report_queue.h"

#include <cstring>

namespace icvsp {

bool ReportQueue::push(const char* line) {
    const size_t n = line ? std::strlen(line) : 0;
    if (n == 0 || n >= kMaxLine) return false;
    if (count_ == kCapacity) {
        pop();
        ++dropped_;
    }
    std::memcpy(slots_[(head_ + count_) % kCapacity], line, n + 1);
    ++count_;
    return true;
}

void ReportQueue::pop() {
    if (!count_) return;
    slots_[head_][0] = '\0';
    head_ = (head_ + 1) % kCapacity;
    --count_;
}

}  // namespace icvsp
