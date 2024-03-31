#pragma once

#include <vector>

#include "cursor/max_scored_cursor.hpp"
#include "query/queries.hpp"
#include "scorer/index_scorer.hpp"
#include "wand_data.hpp"

namespace pisa {

template <typename Cursor, typename Wand>
class BlockMaxScoredCursor: public MaxScoredCursor<Cursor> {
  public:
    using base_cursor_type = Cursor;

    BlockMaxScoredCursor(
        Cursor cursor,
        TermScorer term_scorer,
        float weight,
        float max_score,
        typename Wand::wand_data_enumerator wdata)
        : MaxScoredCursor<Cursor>(std::move(cursor), std::move(term_scorer), weight, max_score),
          m_wdata(std::move(wdata))
    {}
    BlockMaxScoredCursor(BlockMaxScoredCursor const&) = delete;
    BlockMaxScoredCursor(BlockMaxScoredCursor&&) = default;
    BlockMaxScoredCursor& operator=(BlockMaxScoredCursor const&) = delete;
    BlockMaxScoredCursor& operator=(BlockMaxScoredCursor&&) = default;
    ~BlockMaxScoredCursor() = default;

    [[nodiscard]] PISA_ALWAYSINLINE auto block_max_score() -> float
    {
        return m_wdata.score() * this->query_weight();
    }

    [[nodiscard]] PISA_ALWAYSINLINE auto block_max_docid() -> std::uint32_t
    {
        return m_wdata.docid();
    }

    PISA_ALWAYSINLINE void block_max_next_geq(std::uint32_t docid) { m_wdata.next_geq(docid); }

  private:
    typename Wand::wand_data_enumerator m_wdata;
};

template <typename Index, typename WandType, typename Scorer>
[[nodiscard]] auto make_block_max_scored_cursors(
    Index const& index, WandType const& wdata, Scorer const& scorer, Query query, bool weighted = false)
{
    std::size_t term_size = query.terms.size();
    std::vector<BlockMaxScoredCursor<typename Index::document_enumerator, WandType>> cursors;
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

        cursors.push_back(BlockMaxScoredCursor<typename Index::document_enumerator, WandType>(
            index[term_id], score_func, term_weight, max_weight, wdata.getenum(term_id)));
    }

    return cursors;
}

}  // namespace pisa
