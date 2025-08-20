# Overview

This fork adds two additional utilities to PISA:

1. [Access count ordered indices](https://dl.acm.org/doi/pdf/10.1145/3539618.3591987)
2. [Score cache](https://dl.acm.org/doi/10.1145/3357384.3358154)

Both methods allow faster query processing for dynamic pruning methods.

# Building and indexing

Please see the original [PISA](https://github.com/pisa-engine/pisa) repository for how to build the tools and create an index.

# Re-order an index with access counts

1. First create the ranking output using a training query log:

```
./build/bin/evaluate_queries \
    -e block_simdbp # assume BP encoding \
    -i <path_to_inverted_index> \
	-w <path_to_wand_file> \
	-q <path_to_query_file> \
	-terms <path_to_term_lexicon> # file with extension .termlex \
	-stemmer porter2 # assume Porter2 stemmer \
	-k 1000 \
	-a maxscore \
	-s bm25 \
	--threads 8 \
	--documents <path_to_doc_lexicon> # file with extension .doclex \
	> eval_1000.trec
```

2. Create a mapping of new document ID assignments based on access counts:

```
./build/bin/access_count_ordering \
    -t eval_1000.trec \
    -d <path_to_doc_lexicon> # file with extension .doclex \
	-k 100 # count frequencies in top-100 \
	> ac_100.mapping
```

3. Finally create an access count re-ordered index

```
./build/bin/reorder-docids \
    -c <path_to_inverted_index_to_reorder> \
	-o <path_to_reordered_inverted_index> \
	--documents <path_to_docs_to_reorder> \
	--reordered-documents <path_to_reordered_docs> \
	--threads 8 \
	--from-mapping ac_100_mapping
```

# Create a score cache and make a run

1. Similar to re-ordering an get ranking output for a training query log. This time we can only run unique queries since the cache content will unique.


```
./build/bin/evaluate_queries \
    -e block_simdbp # assume BP encoding \
    -i <path_to_inverted_index> \
	-w <path_to_wand_file> \
	-q <path_to_query_file> \
	-terms <path_to_term_lexicon> # file with extension .termlex \
	-stemmer porter2 # assume Porter2 stemmer \
	-k 1000 \
	-a maxscore \
	-s bm25 \
	--threads 8 \
	--documents <path_to_doc_lexicon> # file with extension .doclex \
	--unique # Only run unique queries.
	> eval_1000.trec
```

2. Create the score cache

```
./build/bin/evaluate_queries \
    -stemmer porter2 # assume Porter2 stemmer \
	-q <path_to_query_file> # Same query file used in previous step \
	--terms <path_to_term_lexicon> \
	-k 10 # k'th score to cache. \
	-t eval_1000.trec > scores_10.cache
```

3. Benchmark with the score cache

```
./build/bin/cache_queries \
    -e block_simdbp \
	-i <path_to_inverted_index> \
	-w <path_to_wand_file> \
	-q <path_to_test_query_file> \
	--terms <path_to_term_lexicon> \
	--stemmer porter2 \
	-k 10 # Retrieve top-10 because the cache file is created with top-10 \
	-a maxscore:wand:block_max_wand # Run for usual dynamic pruning \
	-s bm25 \
	-o cache_10 # output file prefix \
	--runs 3 # Make 3 runs for accurate benchmark \
	-T scores_10.cache # the cache file
```
