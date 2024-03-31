#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

#include <mio/mmap.hpp>

#include "app.hpp"
#include "query/queries.hpp"

using namespace pisa;

int main(int argc, const char** argv)
{
    std::uint32_t subset_max_size = 0;
    std::size_t only_top = 0;
    App<arg::Query<arg::QueryMode::Unranked>> app{"Generate subsets starting from given size."};
    app.add_option("-s,--subset-max-size", subset_max_size)->required();
    app.add_option("--only-top", only_top, "Use only top-k highest weight terms from query.")
        ->needs(app.weighted_option());
    CLI11_PARSE(app, argc, argv);

    mio::mmap_source source;
    Payload_Vector<> lexicon;
    if (app.term_lexicon()) {
        source = mio::mmap_source(*app.term_lexicon());
        lexicon = Payload_Vector<>::from(source);
    }

    std::vector<Query> queries = app.queries();
    auto queries_p = &queries;
    std::size_t queries_size = queries.size();
    std::vector<Query> post_queries(queries_size);
    if (only_top > 0) {
        for (std::size_t i = 0; i < queries_size; ++i) {
            auto& q = queries[i];
            std::size_t q_size = q.terms.size();
            std::vector<std::size_t> indices(q_size);
            std::iota(begin(indices), end(indices), 0);
            std::sort(begin(indices), end(indices), [&q](auto idx1, auto idx2) {
                return q.term_weights[idx1] > q.term_weights[idx2];
            });
            std::size_t new_q_size = std::min(only_top, q_size);
            std::vector<term_id_type> new_term_ids(new_q_size);
            std::vector<float> new_term_weights(new_q_size);
            for (std::size_t j = 0; j < new_q_size; ++j) {
                new_term_ids[j] = q.terms[indices[j]];
                new_term_weights[j] = q.term_weights[indices[j]];
            }

            post_queries[i] = {q.id, std::move(new_term_ids), std::move(new_term_weights)};
        }

        queries_p = &post_queries;
    }

    std::uint32_t qid = 0;
    for (const Query& q: *queries_p) {
        for (std::uint32_t subset_size = 1; subset_size <= subset_max_size; ++subset_size) {
            for (const Query& subset_q: q.subsets(subset_size)) {
                std::cout << qid++ << ":";
                for (term_id_type tid: subset_q.terms) {
                    std::cout << " ";
                    if (app.term_lexicon())
                        std::cout << lexicon[tid];
                    else
                        std::cout << tid;
                }

                std::cout << "\n";
            }
        }
    }
    return 0;
}
