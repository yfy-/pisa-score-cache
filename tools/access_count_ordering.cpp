#include <CLI/CLI.hpp>
#include <boost/algorithm/string.hpp>
#include <fstream>
#include <map>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include "payload_vector.hpp"

struct Doc {
    std::uint32_t orig_id;
    std::uint32_t access_freq;
    std::uint32_t access_freq_above_threshold;
    double total_score;
};

int main(int argc, const char** argv)
{
    CLI::App app{"Create document mapping ordered by access counts"};
    std::string trec_file = "";
    std::string doclex_file = "";
    bool sort_total_score = false;
    bool distro = false;
    uint32_t topk = 10;
    uint32_t access_threshold = 0;

    spdlog::set_default_logger(spdlog::stderr_color_mt("stderr"));
    app.add_option("-t,--trec", trec_file, "TREC eval file")->required();
    app.add_option("-d,--doclex", doclex_file, "Document lexicon")->required();
    app.add_option("-k,--topk", topk, "Count until k results");
    app.add_flag("-s,--total-score", sort_total_score, "Break ties by total score before id");
    app.add_flag(
        "-i,--distro", distro, "Outputs frequency distribution rather than new doc mapping");
    app.add_option(
        "-a,--access-threshold",
        access_threshold,
        "Only access counts greater or equal to"
        " threshold will be re-ordered");
    CLI11_PARSE(app, argc, argv);

    auto doc_buffer = pisa::Payload_Vector_Buffer::from_file(doclex_file);
    auto docs = pisa::Payload_Vector<std::string>(doc_buffer);
    std::unordered_map<std::string, std::uint32_t> trec_to_id;
    std::uint32_t num_docs = docs.size();
    for (std::uint32_t i = 0; i < num_docs; ++i) {
        trec_to_id[docs[i]] = i;
    }

    std::vector<Doc> access_counts(num_docs);
    for (int i = 0; i < num_docs; ++i) {
        access_counts[i].orig_id = i;
    }

    std::ifstream trecs(trec_file);
    std::string trec_line;
    spdlog::info("Reading TREC file: {}", trec_file);
    while (std::getline(trecs, trec_line)) {
        std::vector<std::string> cols;
        boost::split(cols, trec_line, boost::is_any_of("\t, "));
        uint32_t doc_id = trec_to_id[cols[2]];
        uint32_t rank = std::stoul(cols[3]);
        double score = std::stod(cols[4]);
        if (rank <= topk) {
            if (++access_counts[doc_id].access_freq >= access_threshold) {
                access_counts[doc_id].access_freq_above_threshold = access_counts[doc_id].access_freq;
            }

            access_counts[doc_id].total_score += score;
        }
    }

    spdlog::info("Sorting access counts");
    std::function<bool(const Doc&, const Doc&)> sort_func = [](const Doc& d1, const Doc& d2) {
        if (d1.access_freq_above_threshold == d2.access_freq_above_threshold) {
            return d1.orig_id < d2.orig_id;
        }

        return d1.access_freq_above_threshold > d2.access_freq_above_threshold;
    };
    if (sort_total_score) {
        sort_func = [](const Doc& d1, const Doc& d2) {
            if (d1.total_score == d2.total_score) {
                return d1.orig_id < d2.orig_id;
            }
            return d1.total_score > d2.total_score;
        };
	spdlog::info("Sort key: Total Score");
    } else {
	spdlog::info("Sort key: Access count");
    }

    std::sort(std::begin(access_counts), std::end(access_counts), sort_func);
    std::map<std::uint32_t, std::uint32_t> freq_counts;
    for (std::uint32_t i = 0; i < num_docs; ++i) {
        if (distro) {
            ++freq_counts[access_counts[i].access_freq];
        } else {
            std::cout << access_counts[i].orig_id << " " << i << "\n";
        }
    }

    if (distro) {
        for (auto [freq, freq_count]: freq_counts) {
            std::cout << freq << " " << freq_count << "\n";
        }
    }

    return 0;
}
