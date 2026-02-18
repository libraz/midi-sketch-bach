// Read-only pool of motif fragments extracted from subject and countersubject.
//
// Built once after subject/countersubject generation, then immutable (Principle 1).
// Provides ranked access to fragments for episode generation and free counterpoint.
// Selection is score-based (not searched -- Principle 3: Reduce Generation).

#ifndef BACH_FUGUE_MOTIF_POOL_H
#define BACH_FUGUE_MOTIF_POOL_H

#include <string>
#include <vector>

#include "core/basic_types.h"

namespace bach {

enum class MotifOp : uint8_t;

/// @brief A scored motif fragment in the pool.
struct PooledMotif {
  std::vector<NoteEvent> notes;          ///< Note events normalized to tick 0.
  float characteristic_score = 0.0f;     ///< Higher = more characteristic of subject.
  std::string origin;                    ///< Origin label: "subject_head", "subject_tail",
                                         ///< "subject_characteristic", "countersubject",
                                         ///< "fragment".
};

/// @brief Read-only pool of motif fragments extracted from subject and countersubject.
///
/// Built once after subject/countersubject generation, then immutable (Principle 1).
/// Provides ranked access to fragments for episode generation and free counterpoint.
/// Selection is score-based (not searched -- Principle 3: Reduce Generation).
///
/// Scoring uses fixed design values (Principle 4: Trust Design Values):
///   - 1.0  subject_head (first 4 notes, most recognizable)
///   - 0.9  subject_characteristic (best 4-note window by rhythmic/intervallic interest)
///   - 0.8  subject_tail (last 3 notes)
///   - 0.7  countersubject (first 4 notes of countersubject, if present)
///   - 0.6  fragment (subject split into 2 halves)
class MotifPool {
 public:
  MotifPool() = default;

  /// @brief Build the pool from a subject and optional countersubject.
  ///
  /// Extracts motif fragments, assigns fixed characteristic scores, normalizes
  /// all fragment timing to start at tick 0, and sorts by descending score.
  /// After build(), the pool is treated as immutable.
  ///
  /// @param subject_notes Subject note events.
  /// @param countersubject_notes Countersubject note events (empty if none).
  /// @param character Subject character for scoring context.
  void build(const std::vector<NoteEvent>& subject_notes,
             const std::vector<NoteEvent>& countersubject_notes,
             SubjectCharacter character);

  /// @brief Get the highest-scored motif.
  /// @return Pointer to the best motif, or nullptr if pool is empty.
  const PooledMotif* best() const;

  /// @brief Get a motif by rank (0 = highest score).
  /// @param rank Zero-based rank index.
  /// @return Pointer to the motif at the given rank, or nullptr if out of range.
  const PooledMotif* getByRank(size_t rank) const;

  /// @brief Get all motifs in the pool, sorted by descending score.
  /// @return Const reference to the internal motif vector.
  const std::vector<PooledMotif>& motifs() const;

  /// @brief Number of motifs in the pool.
  /// @return Motif count.
  size_t size() const;

  /// @brief Check if the pool is empty.
  /// @return True if no motifs are present.
  bool empty() const;

  /// @brief Get an appropriate motif for a given operation.
  ///
  /// Maps operations to preferred pool ranks:
  ///   Original/Invert/Retrograde/Augment/Diminish -> rank 0 (subject_head)
  ///   Fragment -> first entry with origin "fragment" (rank 4+)
  ///   Sequence -> rank 0 (subject head as sequence material)
  ///
  /// Falls back to best() if preferred rank is unavailable.
  ///
  /// @param op Motif operation that will be applied.
  /// @return Pointer to appropriate motif, or nullptr if pool is empty.
  const PooledMotif* getForOp(MotifOp op) const;

 private:
  std::vector<PooledMotif> motifs_;
};

}  // namespace bach

#endif  // BACH_FUGUE_MOTIF_POOL_H
