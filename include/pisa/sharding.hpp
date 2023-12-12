#pragma once

#include <optional>

#include <gsl/span>
#include <spdlog/spdlog.h>

#include "io.hpp"
#include "type_safe.hpp"
#include "vec_map.hpp"

namespace pisa {

[[nodiscard]] auto
format_shard(std::string_view basename, Shard_Id shard, std::string_view suffix = {}) -> std::string;

[[nodiscard]] auto expand_shard(std::string_view basename, Shard_Id shard) -> std::string;

auto resolve_shards(std::string_view basename, std::string_view suffix = {}) -> std::vector<Shard_Id>;

auto mapping_from_files(std::istream* full_titles, gsl::span<std::istream*> shard_titles)
    -> VecMap<Document_Id, Shard_Id>;

auto mapping_from_files(std::string const& full_titles, gsl::span<std::string const> shard_titles)
    -> VecMap<Document_Id, Shard_Id>;

auto create_random_mapping(
    int document_count, int shard_count, std::optional<std::uint64_t> seed = std::nullopt)
    -> VecMap<Document_Id, Shard_Id>;

auto create_random_mapping(
    std::string const& input_basename,
    int shard_count,
    std::optional<std::uint64_t> seed = std::nullopt) -> VecMap<Document_Id, Shard_Id>;

void copy_sequence(std::istream& is, std::ostream& os);

void rearrange_sequences(
    std::string const& input_basename,
    std::string const& output_basename,
    VecMap<Document_Id, Shard_Id>& mapping,
    std::optional<Shard_Id> shard_count = std::nullopt);

void process_shard(
    std::string const& input_basename,
    std::string const& output_basename,
    Shard_Id shard_id,
    VecMap<Term_Id, std::string> const& terms);

    spdlog::debug("[Shard {}] Calculating term occurrences", shard_id.as_int());
    std::vector<uint32_t> has_term(terms.size(), 0U);
    for (auto iter = ++shard.begin(); iter != shard.end(); ++iter) {
        for (auto term: *iter) {
            has_term[term] = 1U;
        }
    }


    spdlog::debug("[Shard {}] Writing terms", shard_id.as_int());
    {
        std::vector<size_t> dropped_terms;
        std::ofstream tos(fmt::format("{}.terms", basename));
        for (size_t i = 0; i < terms.size(); ++i) {
            if (has_term[i] != 0U) {
              tos << terms.as_vector()[i] << '\n';
            } else {
              dropped_terms.push_back(i);
            }
        }

        std::ofstream dtos(format_shard(output_basename, shard_id,
                                        ".dropped_terms"));
        std::copy(std::begin(dropped_terms), std::end(dropped_terms),
                  std::ostream_iterator<size_t>(dtos, "\n"));
    }

    {
        spdlog::debug("[Shard {}] Creating term lexicon", shard_id.as_int());
        std::ifstream title_is(fmt::format("{}.terms", basename));
        encode_payload_vector(
            std::istream_iterator<io::Line>(title_is), std::istream_iterator<io::Line>())
            .to_file(fmt::format("{}.termlex", basename));
    }

    spdlog::debug("[Shard {}] Remapping term IDs", shard_id.as_int());
    if (auto pos = std::find(has_term.begin(), has_term.end(), 1U); pos != has_term.end()) {
        *pos = 0U;
    }
    std::partial_sum(has_term.begin(), has_term.end(), has_term.begin());
    auto remapped_term_id = [&](auto term) { return has_term[term]; };
    spdlog::debug("[Shard {}] Writing remapped collection", shard_id.as_int());
    for (auto iter = ++shard.begin(); iter != shard.end(); ++iter) {
        for (auto& term: *iter) {
            term = remapped_term_id(term);
        }
    }
    {
        spdlog::debug("[Shard {}] Creating document lexicon", shard_id.as_int());
        std::ifstream os(fmt::format("{}.documents", basename));
        encode_payload_vector(std::istream_iterator<io::Line>(os), std::istream_iterator<io::Line>())
            .to_file(fmt::format("{}.doclex", basename));
    }
    spdlog::info("Shard {} finished.", shard_id.as_int());
}

void partition_fwd_index(
    std::string const& input_basename,
    std::string const& output_basename,
    VecMap<Document_Id, Shard_Id>& mapping);

}  // namespace pisa
