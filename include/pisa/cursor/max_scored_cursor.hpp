#pragma once

#include <vector>

#include "cursor/scored_cursor.hpp"
#include "query/queries.hpp"
#include "wand_data.hpp"

namespace pisa {

template <typename Cursor>
class MaxScoredCursor: public ScoredCursor<Cursor> {
  public:
    using base_cursor_type = Cursor;

    MaxScoredCursor(Cursor cursor, TermScorer term_scorer, float query_weight, float max_score)
        : ScoredCursor<Cursor>(std::move(cursor), std::move(term_scorer), query_weight),
          m_max_score(max_score)
    {}
    MaxScoredCursor(MaxScoredCursor const&) = delete;
    MaxScoredCursor(MaxScoredCursor&&) = default;
    MaxScoredCursor& operator=(MaxScoredCursor const&) = delete;
    MaxScoredCursor& operator=(MaxScoredCursor&&) = default;
    ~MaxScoredCursor() = default;

    [[nodiscard]] PISA_ALWAYSINLINE auto max_score() const noexcept -> float { return m_max_score; }

  private:
    float m_max_score;
};

template <typename Index, typename WandType, typename Scorer>
[[nodiscard]] auto make_max_scored_cursors(
    Index const& index, WandType const& wdata, Scorer const& scorer, Query query, bool weighted = false)
{
    std::size_t term_size = query.terms.size();
    std::vector<MaxScoredCursor<typename Index::document_enumerator>> cursors;
    cursors.reserve(term_size);
    for (std::size_t i = 0; i < term_size; ++i) {
        uint32_t term_id = query.terms[i];
        float term_weight = query.term_weights[i];
        float max_weight = wdata.max_term_weight(term_id);
        term_scorer_t score_func = scorer.term_scorer(term_id);
        if (weighted) {
            max_weight *= term_weight;
            score_func = [scorer = scorer.term_scorer(term_id), weight = term_weight](
                             uint32_t doc, uint32_t freq) { return weight * scorer(doc, freq); };
        }

        cursors.push_back(MaxScoredCursor<typename Index::document_enumerator>(
            index[term_id], score_func, term_weight, max_weight));
    }

    return cursors;
}

}  // namespace pisa
