// Read-only pool of motif fragments extracted from subject and countersubject.

#include "fugue/motif_pool.h"

#include <algorithm>
#include <set>
#include <vector>

#include "constraint/motif_constraint.h"
#include "core/basic_types.h"
#include "core/pitch_utils.h"

namespace bach {

namespace {

/// @brief Normalize a motif so the first note starts at tick 0.
/// @param notes Note events to normalize in place.
void normalizeToTickZero(std::vector<NoteEvent>& notes) {
  if (notes.empty()) return;
  Tick offset = notes[0].start_tick;
  for (auto& note : notes) {
    note.start_tick = (note.start_tick >= offset) ? note.start_tick - offset : 0;
  }
}

/// @brief Extract the first N notes from a sequence.
/// @param notes Source notes.
/// @param max_notes Maximum number of notes to extract.
/// @return First min(max_notes, notes.size()) notes.
std::vector<NoteEvent> extractHead(const std::vector<NoteEvent>& notes, size_t max_notes) {
  size_t count = std::min(max_notes, notes.size());
  return std::vector<NoteEvent>(notes.begin(), notes.begin() + static_cast<int>(count));
}

/// @brief Extract the last N notes from a sequence.
/// @param notes Source notes.
/// @param num_notes Number of tail notes to extract.
/// @return Last min(num_notes, notes.size()) notes.
std::vector<NoteEvent> extractTail(const std::vector<NoteEvent>& notes, size_t num_notes) {
  if (num_notes >= notes.size()) return notes;
  return std::vector<NoteEvent>(notes.end() - static_cast<int>(num_notes), notes.end());
}

/// @brief Score a window of notes for characteristic quality.
///
/// Uses the same criteria as extractCharacteristicMotif() in episode.cpp:
///   +0.3  Rhythmic diversity (distinct durations / window_size)
///   +0.3  Intervallic interest (contains a leap >= 3 semitones)
///   +0.2  Proximity to opening (front-weighted)
///   +0.2  Tonal stability (contains root pitch class)
///
/// @param notes Full subject notes.
/// @param start Window start index.
/// @param length Window length.
/// @param window_count Total number of possible windows.
/// @return Characteristic score in [0.0, 1.0].
float scoreWindow(const std::vector<NoteEvent>& notes, size_t start, size_t length,
                  size_t window_count) {
  float score = 0.0f;

  // Rhythmic diversity: count distinct durations in window.
  std::set<Tick> durations;
  for (size_t idx = start; idx < start + length; ++idx) {
    durations.insert(notes[idx].duration);
  }
  score += 0.3f * static_cast<float>(durations.size()) / static_cast<float>(length);

  // Intervallic interest: contains a leap (>= 3 semitones).
  bool has_leap = false;
  for (size_t idx = start + 1; idx < start + length; ++idx) {
    int interval = absoluteInterval(notes[idx].pitch, notes[idx - 1].pitch);
    if (interval >= 3) {
      has_leap = true;
      break;
    }
  }
  if (has_leap) score += 0.3f;

  // Proximity to opening.
  float proximity = (window_count > 0)
                        ? 1.0f - static_cast<float>(start) / static_cast<float>(window_count)
                        : 1.0f;
  score += 0.2f * proximity;

  // Tonal stability: contains root pitch class (first note's pitch class).
  int root_pc = getPitchClass(notes[0].pitch);
  bool has_root = false;
  for (size_t idx = start; idx < start + length; ++idx) {
    if (getPitchClass(notes[idx].pitch) == root_pc) {
      has_root = true;
      break;
    }
  }
  if (has_root) score += 0.2f;

  return score;
}

/// @brief Find the best characteristic window in the subject.
/// @param notes Subject notes.
/// @param motif_length Window length (default 4).
/// @return Notes of the best characteristic window, normalized to tick 0.
std::vector<NoteEvent> findCharacteristicWindow(const std::vector<NoteEvent>& notes,
                                                size_t motif_length) {
  if (notes.size() <= motif_length) {
    auto result = notes;
    normalizeToTickZero(result);
    return result;
  }

  float best_score = -1.0f;
  size_t best_start = 0;
  size_t window_count = notes.size() - motif_length + 1;

  for (size_t start = 0; start < window_count; ++start) {
    float win_score = scoreWindow(notes, start, motif_length, window_count);
    if (win_score > best_score) {
      best_score = win_score;
      best_start = start;
    }
  }

  auto result = std::vector<NoteEvent>(
      notes.begin() + static_cast<int>(best_start),
      notes.begin() + static_cast<int>(best_start + motif_length));
  normalizeToTickZero(result);
  return result;
}

}  // namespace

void MotifPool::build(const std::vector<NoteEvent>& subject_notes,
                      const std::vector<NoteEvent>& countersubject_notes,
                      SubjectCharacter /*character*/) {
  motifs_.clear();

  if (subject_notes.empty()) return;

  // --- 1. Subject head (first 4 notes) -- highest characteristic score ---
  constexpr size_t kHeadLength = 4;
  {
    auto head = extractHead(subject_notes, kHeadLength);
    normalizeToTickZero(head);
    PooledMotif motif;
    motif.notes = std::move(head);
    motif.characteristic_score = 1.0f;
    motif.origin = "subject_head";
    motifs_.push_back(std::move(motif));
  }

  // --- 2. Subject characteristic (best 4-note window) ---
  constexpr size_t kCharacteristicLength = 4;
  if (subject_notes.size() > kHeadLength) {
    // Only add if the subject is long enough that the characteristic window
    // might differ from the head.
    auto char_notes = findCharacteristicWindow(subject_notes, kCharacteristicLength);
    PooledMotif motif;
    motif.notes = std::move(char_notes);
    motif.characteristic_score = 0.9f;
    motif.origin = "subject_characteristic";
    motifs_.push_back(std::move(motif));
  }

  // --- 3. Subject tail (last 3 notes) ---
  constexpr size_t kTailLength = 3;
  if (subject_notes.size() >= kTailLength) {
    auto tail = extractTail(subject_notes, kTailLength);
    normalizeToTickZero(tail);
    PooledMotif motif;
    motif.notes = std::move(tail);
    motif.characteristic_score = 0.8f;
    motif.origin = "subject_tail";
    motifs_.push_back(std::move(motif));
  }

  // --- 4. Countersubject head (if available) ---
  constexpr size_t kCsHeadLength = 4;
  if (!countersubject_notes.empty()) {
    auto cs_head = extractHead(countersubject_notes, kCsHeadLength);
    normalizeToTickZero(cs_head);
    PooledMotif motif;
    motif.notes = std::move(cs_head);
    motif.characteristic_score = 0.7f;
    motif.origin = "countersubject";
    motifs_.push_back(std::move(motif));
  }

  // --- 5. Subject fragments (split into 2 halves) ---
  constexpr size_t kNumFragments = 2;
  if (subject_notes.size() >= kNumFragments) {
    size_t frag_size = subject_notes.size() / kNumFragments;
    for (size_t frag_idx = 0; frag_idx < kNumFragments; ++frag_idx) {
      size_t start = frag_idx * frag_size;
      size_t end = (frag_idx + 1 == kNumFragments) ? subject_notes.size()
                                                    : (frag_idx + 1) * frag_size;
      auto frag_notes = std::vector<NoteEvent>(
          subject_notes.begin() + static_cast<int>(start),
          subject_notes.begin() + static_cast<int>(end));
      normalizeToTickZero(frag_notes);
      PooledMotif motif;
      motif.notes = std::move(frag_notes);
      motif.characteristic_score = 0.6f;
      motif.origin = "fragment";
      motifs_.push_back(std::move(motif));
    }
  }

  // --- 6. Sort by descending characteristic score ---
  std::sort(motifs_.begin(), motifs_.end(),
            [](const PooledMotif& lhs, const PooledMotif& rhs) {
              return lhs.characteristic_score > rhs.characteristic_score;
            });
}

const PooledMotif* MotifPool::best() const {
  if (motifs_.empty()) return nullptr;
  return &motifs_[0];
}

const PooledMotif* MotifPool::getByRank(size_t rank) const {
  if (rank >= motifs_.size()) return nullptr;
  return &motifs_[rank];
}

const std::vector<PooledMotif>& MotifPool::motifs() const {
  return motifs_;
}

size_t MotifPool::size() const {
  return motifs_.size();
}

bool MotifPool::empty() const {
  return motifs_.empty();
}

const PooledMotif* MotifPool::getForOp(MotifOp op) const {
  if (motifs_.empty()) return nullptr;

  switch (op) {
    case MotifOp::Fragment: {
      // Find first fragment entry.
      for (const auto& motif : motifs_) {
        if (motif.origin == "fragment") return &motif;
      }
      return best();  // Fallback.
    }
    case MotifOp::Original:
    case MotifOp::Invert:
    case MotifOp::Retrograde:
    case MotifOp::Augment:
    case MotifOp::Diminish:
    case MotifOp::Sequence:
    default:
      return best();  // Rank 0 (subject_head).
  }
}

}  // namespace bach
