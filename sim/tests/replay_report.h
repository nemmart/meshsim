// replay_report.h (WORKER25, P7b): ref's Replayer lists as ref's harnesses print them, from the C++ replayer's lists
// (units::Replayer::list_*: ref's text per entry, the first list_capacity() kept, every entry counted). Test-side,
// configuration-regime code (strings), used after a run.
//   list_string(r, l)   List.toString of ref's list: "[a, b, c]"
//   head(r, l)          StageB.head: the whole list if 3 entries or fewer, else the first three and "... (N in all)"
//   error_report(r)     Replayer.errorReport(): "violations [...], invalid [...], overruns [...], underflows [...],
//                       leftover [...]"
// A list longer than the kept entries can't be printed whole: list_string then ends "..., (N in all, M kept)]",
// which no ref output has; drivers that print whole lists (Stage A) size the lists to make that unreachable.
#pragma once

#include <cstdint>
#include <string>

#include "units/replayer.h"

namespace rreport {

inline std::string list_string(const units::Replayer& r, unsigned l) {
    std::string s = "[";
    uint64_t n = r.list_count(l), kept = n < r.list_capacity() ? n : r.list_capacity();
    for (uint64_t i = 0; i < kept; i++) { if (i) s += ", "; s += r.list_entry(l, uint32_t(i)); }
    if (kept < n) s += ", (" + std::to_string(n) + " in all, " + std::to_string(kept) + " kept)";
    return s + "]";
}

inline std::string head(const units::Replayer& r, unsigned l) {
    uint64_t n = r.list_count(l);
    if (n <= 3) return list_string(r, l);
    std::string s = "[";
    for (uint32_t i = 0; i < 3; i++) { if (i) s += ", "; s += r.list_entry(l, i); }
    return s + "] ... (" + std::to_string(n) + " in all)";
}

inline std::string error_report(const units::Replayer& r) {
    using R = units::Replayer;
    return "violations " + list_string(r, R::L_VIOLATIONS) + ", invalid " + list_string(r, R::L_INVALID) + ", overruns " +
           list_string(r, R::L_OVERRUNS) + ", underflows " + list_string(r, R::L_UNDERFLOWS) + ", leftover " +
           list_string(r, R::L_LEFTOVER);
}

}  // namespace rreport
