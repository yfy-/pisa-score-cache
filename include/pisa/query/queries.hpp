#pragma once

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <numeric>

#include <boost/functional/hash.hpp>
#include "query/term_processor.hpp"

namespace pisa {

using term_id_type = uint32_t;
using term_id_vec = std::vector<term_id_type>;
using term_freq_pair = std::pair<uint64_t, uint64_t>;
using term_freq_vec = std::vector<term_freq_pair>;

struct Query {
  std::optional<std::string> id;
  std::vector<term_id_type> terms;
  std::vector<float> term_weights;

  // TODO: Think of how this constructor could be written
  // Query(std::optional<std::string>&& id, std::vector<term_id_type>&& terms,
  //       std::vector<float>&& term_weights) :
  //     id(std::move(id)), terms(std::move(terms)),
  //     term_weights(std::move(term_weights)) {
  //   std::sort(std::begin(terms), std::end(terms));
  // }

  std::vector<Query> subsets(size_t length) const;
  bool operator==(const Query& other) const { return terms == other.terms; };

 private:
  bool next_subset(std::vector<size_t>& ptrs, std::vector<size_t>& end_ptrs,
                   size_t length) const;
};

[[nodiscard]] auto split_query_at_colon(std::string const& query_string)
    -> std::pair<std::optional<std::string>, std::string_view>;

[[nodiscard]] auto parse_query_terms(std::string const& query_string, TermProcessor term_processor)
    -> Query;

[[nodiscard]] auto parse_query_ids(std::string const& query_string) -> Query;

[[nodiscard]] std::function<void(const std::string)> resolve_query_parser(
    std::vector<Query>& queries,
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
  size_t operator()(const Query& q) const {
    size_t seed = 0;
    for (term_id_type term : q.terms)
      boost::hash_combine(seed, term);

    return seed;
  }
};

} // namespace std
