#include "query/query_parser.hpp"
#include "query/queries.hpp"

namespace pisa {

QueryParser::QueryParser(TextAnalyzer analyzer, std::unique_ptr<TermMap> term_map)
    : m_analyzer(std::move(analyzer)), m_term_map(std::move(term_map))
{}

auto QueryParser::parse(std::string_view query, bool weighted) const -> Query
{
    auto [id, raw_query] = split_query_at_colon(query);
    auto tokens = m_analyzer.analyze(raw_query);
    std::vector<std::uint32_t> query_ids;
    for (auto token: *tokens) {
        if (auto id = (*m_term_map)(token); id) {
            query_ids.push_back(*id);
        }
    }

    term_freq_vec query_w_freqs = query_freqs(query_ids);
    std::size_t unq_term_size = query_w_freqs.size();
    term_id_vec unq_tids(unq_term_size);
    std::vector<float> unq_weights(unq_term_size);
    for (std::size_t i = 0; i < unq_term_size; ++i) {
        unq_tids[i] = query_w_freqs[i].first;
        unq_weights[i] = weighted ? query_w_freqs[i].second : 1.0F;
    }

    return {std::move(id), std::move(unq_tids), std::move(unq_weights)};
}

auto QueryParser::parse(std::string const& query, bool weighted) const -> Query
{
    return parse(std::string_view(query), weighted);
}

}  // namespace pisa
