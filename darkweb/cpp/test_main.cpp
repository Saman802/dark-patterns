// native test harness:  ./build/test tests/sample_nodes.tsv
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
extern "C" {
int dw_init(const char*);
const char* dw_scan(const char*);
const char* dw_trace_text(const char*);
const char* dw_rules_dump();
const char* dw_trie_dump();
const char* dw_hash_dump();
}
int main(int argc, char** argv) {
    std::string mode = argc > 2 ? argv[2] : "scan";
    dw_init("1\tMyCat\t6\tfake reviews\n");
    if (mode == "scan") { std::ifstream f(argv[1]); std::stringstream ss; ss << f.rdbuf(); puts(dw_scan(ss.str().c_str())); }
    else if (mode == "trace") puts(dw_trace_text(argv[1]));
    else if (mode == "rules") puts(dw_rules_dump());
    else if (mode == "trie") puts(dw_trie_dump());
    else if (mode == "hash") puts(dw_hash_dump());
    return 0;
}
