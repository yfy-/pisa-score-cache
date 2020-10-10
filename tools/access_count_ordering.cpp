#include <CLI/CLI.hpp>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <boost/algorithm/string.hpp>
#include "payload_vector.hpp"

int main(int argc, const char** argv) {
    CLI::App app{"Create document mapping ordered by access counts"};
    std::string trec_file = "";
    std::string doclex_file = "";
    app.add_option("-t,--trec", trec_file, "TREC eval file")->required();
    app.add_option("-d,--doclex", doclex_file, "Document lexicon")->required();
    CLI11_PARSE(app, argc, argv);

    auto doc_buffer = pisa::Payload_Vector_Buffer::from_file(doclex_file);
    auto docs = pisa::Payload_Vector<std::string>(doc_buffer);
    std::unordered_map<std::string, std::uint32_t> trec_to_id;
    std::uint32_t num_docs = docs.size();
    for (std::uint32_t i = 0; i < num_docs; ++i)
      trec_to_id[docs[i]] = i;

    std::vector<std::pair<uint32_t, uint32_t>> access_counts(num_docs);
    std::ifstream trecs(trec_file);
    std::string trec_line;
    while (std::getline(trecs, trec_line)) {
      std::vector<std::string> cols;
      boost::split(cols, trec_line, boost::is_any_of("\t"));
      uint32_t doc_id = trec_to_id[cols[2]];
      access_counts[doc_id].first++;
      access_counts[doc_id].second = doc_id;
    }

    std::sort(std::begin(access_counts), std::end(access_counts),
              [](std::pair<uint32_t, uint32_t> x,
                 std::pair<uint32_t, uint32_t> y) {
                if (x.first == y.first)
                  return x.second < y.second;

                return x.first > y.first;
              });
    std::vector<bool> doc_assgn(num_docs);
    std::vector<uint32_t> mapping(num_docs);
    uint32_t crr_id = 0;
    for (auto count_docid : access_counts) {
      if (!count_docid.first)
        break;

      mapping[count_docid.second] = crr_id;
      doc_assgn[count_docid.second] = true;
      crr_id++;
    }

    for (std::uint32_t i = 0; i < num_docs; ++i) {
      if (!doc_assgn[i]) {
        mapping[i] = crr_id;
        crr_id++;
      }
    }

    for (std::uint32_t i = 0; i < num_docs; ++i)
      std::cout << i << " " << mapping[i] << "\n";

    return 0;
}
