#pragma once

#include <vector>

#include "query/queries.hpp"
#include "scorer/index_scorer.hpp"
#include "wand_data.hpp"

namespace pisa {

template <typename Cursor>
class ScoredCursor {
  public:
    using base_cursor_type = Cursor;

    ScoredCursor(Cursor cursor, TermScorer term_scorer, float query_weight)
        : m_base_cursor(std::move(cursor)),
          m_term_scorer(std::move(term_scorer)),
          m_query_weight(query_weight)
    {}
    ScoredCursor(ScoredCursor const&) = delete;
    ScoredCursor(ScoredCursor&&) = default;
    ScoredCursor& operator=(ScoredCursor const&) = delete;
    ScoredCursor& operator=(ScoredCursor&&) = default;
    ~ScoredCursor() = default;

    [[nodiscard]] PISA_ALWAYSINLINE auto query_weight() const noexcept -> float
    {
        return m_query_weight;
    }
    [[nodiscard]] PISA_ALWAYSINLINE auto docid() const -> std::uint32_t
    {
        return m_base_cursor.docid();
    }
    [[nodiscard]] PISA_ALWAYSINLINE auto freq() -> std::uint32_t { return m_base_cursor.freq(); }
    [[nodiscard]] PISA_ALWAYSINLINE auto score() -> float { return m_term_scorer(docid(), freq()); }
    void PISA_ALWAYSINLINE next() { m_base_cursor.next(); }
    void PISA_ALWAYSINLINE next_geq(std::uint32_t docid) { m_base_cursor.next_geq(docid); }
    [[nodiscard]] PISA_ALWAYSINLINE auto size() -> std::size_t { return m_base_cursor.size(); }

  private:
    Cursor m_base_cursor;
    TermScorer m_term_scorer;
    float m_query_weight = 1.0;
};

template <typename Index, typename Scorer>
[[nodiscard]] auto
make_scored_cursors(Index const& index, Scorer const& scorer, Query query, bool weighted = false)
{
    std::size_t term_size = query.terms.size();
    std::vector<ScoredCursor<typename Index::document_enumerator>> cursors;
    cursors.reserve(term_size);
    for (std::size_t i = 0; i < term_size; ++i) {
        uint32_t term_id = query.terms[i];
        float term_weight = query.term_weights[i];
        term_scorer_t score_func = scorer.term_scorer(term_id);
        if (weighted) {
            score_func = [scorer = scorer.term_scorer(term_id), weight = term_weight](
                             uint32_t doc, uint32_t freq) { return weight * scorer(doc, freq); };
        }

        cursors.push_back(ScoredCursor<typename Index::document_enumerator>(
            index[term_id], score_func, term_weight));
    }

    return cursors;
}

}  // namespace pisa
