#pragma once

#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "query/term_processor.hpp"
#include "tokenizer.hpp"
#include <boost/functional/hash.hpp>

namespace pisa {

using term_id_type = uint32_t;
using term_id_vec = std::vector<term_id_type>;
using term_freq_pair = std::pair<uint64_t, uint64_t>;
using term_freq_vec = std::vector<term_freq_pair>;

struct Query {
    std::optional<std::string> id;
    std::vector<term_id_type> terms;
    std::vector<float> term_weights;
    static uint32_t default_query_id;

    Query() = default;
    Query(std::optional<std::string>&& id, std::vector<term_id_type>&& terms, std::vector<float>&& term_weights)
        : Query(id, terms, term_weights)
    {}

    Query(
        const std::optional<std::string>& id,
        const std::vector<term_id_type>& terms,
        const std::vector<float>& term_weights)
        : id(id)
    {
        std::size_t terms_size = terms.size();
        auto term_weights_source = &term_weights;
        std::vector<float> unit_term_weights;
        if (term_weights_source->empty()) {
            unit_term_weights.resize(terms_size, 1.0F);
            term_weights_source = &unit_term_weights;
        }

        if (terms_size != term_weights_source->size())
            throw std::invalid_argument("term id and weight size not equal");

        std::vector<std::size_t> indices(terms_size);
        std::iota(std::begin(indices), std::end(indices), 0);
        std::vector<term_id_type> sorted_terms(terms_size);
        std::vector<float> sorted_term_weights(terms_size);
        std::sort(std::begin(indices), std::end(indices), [&terms](auto idx1, auto idx2) {
            return terms[idx1] < terms[idx2];
        });

        for (auto i = 0U; i < terms_size; ++i) {
            sorted_terms[i] = terms[indices[i]];
            sorted_term_weights[i] = (*term_weights_source)[indices[i]];
        }

        this->terms = std::move(sorted_terms);
        this->term_weights = std::move(sorted_term_weights);
    }

    std::vector<Query> subsets(size_t length) const;
    bool operator==(const Query& other) const
    {
        return terms == other.terms && term_weights == other.term_weights;
    };

  private:
    bool next_subset(std::vector<size_t>& ptrs, std::vector<size_t>& end_ptrs, size_t length) const;
};

[[nodiscard]] auto split_query_at_colon(std::string_view query_string)
    -> std::pair<std::optional<std::string>, std::string_view>;

[[nodiscard]] auto parse_query_terms(
    std::string const& query_string, Tokenizer const& tokenizer, TermProcessor term_processor)
    -> Query;

[[nodiscard]] auto parse_query_ids(std::string const& query_string) -> Query;

[[nodiscard]] std::function<void(const std::string)> resolve_query_parser(
    std::vector<Query>& queries,
    std::unique_ptr<pisa::Tokenizer> tokenizer,
    std::optional<std::string> const& terms_file,
    std::optional<std::string> const& stopwords_filename,
    std::optional<std::string> const& stemmer_type);

bool read_query(term_id_vec& ret, std::istream& is = std::cin);

void remove_duplicate_terms(term_id_vec& terms);

term_freq_vec query_freqs(term_id_vec terms);

}  // namespace pisa

namespace std {

using namespace pisa;

template <>
struct hash<Query> {
    size_t operator()(const Query& q) const
    {
        size_t seed = 0;
        for (int i = 0; i < q.terms.size(); ++i) {
            boost::hash_combine(seed, q.terms[i]);
            boost::hash_combine(seed, q.term_weights[i]);
        }

        return seed;
    }
};

}  // namespace std
