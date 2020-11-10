#include <iostream>
#include <random>
#include <string>
#include <chrono>

#include <couchbase/client/cluster.hxx>
#include <couchbase/transactions.hxx>
#include <couchbase/transactions/transaction_config.hxx>


using namespace std;
using namespace couchbase;

std::string
make_uuid()
{
    static std::random_device dev;
    static std::mt19937 rng(dev());

    uniform_int_distribution<int> dist(0, 15);

    const char* v = "0123456789abcdef";
    const bool dash[] = { 0, 0, 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0 };

    string res;
    for (int i = 0; i < 16; i++) {
        if (dash[i])
            res += "-";
        res += v[dist(rng)];
        res += v[dist(rng)];
    }
    return res;
}

const nlohmann::json CONTENT = nlohmann::json::parse("{\"some\":\"content\", \"num\": 0}");

void just_make_connection(const std::string& conn_str)
{
    cluster c(conn_str, "Administrator", "password");
}

void upsert_random_docs(std::shared_ptr<collection> coll, int n_docs)
{
    auto start = std::chrono::steady_clock::now();
    for (int i=0; i<n_docs; i++) {
        std::ostringstream stream;
        stream << "doc_" << i;
        auto res = coll->upsert(stream.str(), CONTENT);
        if (!res.is_success()) {
            cerr << stream.str() << " upsert error" << res.strerror() << endl;
        }
    }
    auto end = std::chrono::steady_clock::now();
    std::cerr << "===== upsert  ======" << endl;
    std::chrono::duration<double> elapsed_seconds = end-start;
    std::cerr << "ops/sec:" << n_docs / elapsed_seconds.count() << endl;
}

void read_random_docs(std::shared_ptr<collection> coll, int n_docs)
{
    auto start = std::chrono::steady_clock::now();
    for (int i=0; i<n_docs; i++) {
        std::ostringstream stream;
        stream << "doc_" << i;
        auto res = coll->get(stream.str());
        if (!res.is_success()) {
            cerr << stream.str() << " get error: " << res.strerror() << endl;
        }
    }
    auto end = std::chrono::steady_clock::now();
    std::cerr << "===== get ======" << endl;
    std::chrono::duration<double> elapsed_seconds = end-start;
    std::cerr << "ops/sec:" << n_docs / elapsed_seconds.count() << endl;
}
void read_write_no_txn(cluster& cluster, std::shared_ptr<collection> coll, int n_docs)
{
    auto start = std::chrono::steady_clock::now();
    int n_errors = 0;
    std::thread::id this_id = std::this_thread::get_id();
    for (int i=0; i<n_docs; i++) {
        try {
            bool done = false;
            while(!done) {
                std::ostringstream stream;
                stream << "doc_" << i;
                auto res = coll->get(stream.str());
                if (res.is_success()) {
                    auto content = res.value.get();
                    content["num"] = content["num"].get<int>() + 1;
                    auto res2 = coll->upsert(stream.str(), content, upsert_options().cas(res.cas));
                    if (!res.is_success()) {
                        n_errors++;
                    } else {
                        done = true;
                    }
                } else {
                    n_errors++;
                }
            }
        } catch (const std::runtime_error& e) {
            cerr << this_id << "got error " << e.what() << endl;
        }
    }
    auto end = std::chrono::steady_clock::now();
    std::cerr << this_id << " ===== read/replace/no_txn ======" << endl;
    std::chrono::duration<double> elapsed_seconds = end-start;
    std::cerr << this_id << " ops/sec:" << n_docs / elapsed_seconds.count() << ", " << n_errors << " errors" << endl;
}

void read_write_in_txn(cluster& cluster, std::shared_ptr<collection> coll, int n_docs, bool cleanup)
{
    auto start = std::chrono::steady_clock::now();
    transactions::transaction_config config;
    config.cleanup_client_attempts(cleanup);
    config.cleanup_lost_attempts(cleanup);
    transactions::transactions txn(cluster, config);
    int n_errors = 0;
    std::thread::id this_id = std::this_thread::get_id();
    for (int i=0; i<n_docs; i++) {
        try {
            txn.run([&](transactions::attempt_context& ctx) {
                std::ostringstream stream;
                stream << "doc_" << i;
                auto doc = ctx.get(coll, stream.str());
                auto content = doc.content<nlohmann::json>();
                content["num"] = content["num"].get<int>() + 1;
                auto doc2 = ctx.replace(coll, doc,  content);
            });
        } catch (transactions::transaction_expired& exp) {
            n_errors++;
            cerr << this_id << " transaction expired " << exp.what() << endl;
        } catch (transactions::transaction_failed& fail) {
            n_errors++;
            cerr << this_id << " transaction failed " << fail.what() << endl;
        } catch (transactions::transaction_commit_ambiguous& amb) {
            n_errors++;
            cerr << this_id << " transaction commit ambiguous " << amb.what() << endl;
        } catch (std::runtime_error& e) {
            n_errors++;
            cerr << this_id << " unexpected error" << e.what() << endl;
        }
    }
    auto end = std::chrono::steady_clock::now();
    std::cerr << this_id << " ===== read/replace/in_txn ======" << endl;
    std::chrono::duration<double> elapsed_seconds = end-start;
    std::cerr << this_id << " ops/sec:" << n_docs / elapsed_seconds.count() << ", " << n_errors << " errors" << endl;
}

const int NUM_DOCS = 100000;
const int NUM_THREADS = 10;
// repeat it so we don't do the dns srv lookup automatically - till
// lcb changes that this makes connect faster.
const std::string CONN_STR = "couchbase://127.0.0.1,127.0.0.1";

int main(int argc, char* argv[])
{
    auto conn_str = CONN_STR;
    if (argc > 1) {
        std::string ip = argv[1];
        conn_str = std::string("couchbase://") + ip + std::string(",") + ip;
    }
    auto n_docs = NUM_DOCS;
    if (argc > 2) {
        std::stringstream str(argv[2]);
        str >> n_docs;
    }
    auto n_threads = NUM_THREADS;
    if (argc > 3) {
        std::stringstream str(argv[3]);
        str >> n_threads;
    }
    bool cleanup = false;
    if (argc > 4) {
        cleanup = true;
    }
    cerr << "using " << conn_str << " as connection string" << endl;
    cerr << "using " << n_docs << " documents" << endl;
    cerr << "using " << n_threads << " threads" << endl;
    cerr << "setting cleanup (both kinds) to " << cleanup << endl;
    std::vector<std::thread> threads;
    // first, upsert them in a single thread
    cluster c(conn_str, "Administrator", "password");
    auto collection = c.bucket("default")->default_collection();
    upsert_random_docs(collection, n_docs);
    // just make connection
    for(int j=0; j<20; j++) {
        cerr << "making " << n_threads << "connections in their own thread " << j << " of 20" << endl;
        for (int i=0; i<n_threads; i++) {
            threads.push_back(std::move(std::thread([conn_str]() {
                try {
                    cluster c(conn_str, "Administrator", "password");

                } catch(const std::runtime_error& e) {
                    cerr << "ERROR " << e.what() << endl;
                }
            })));
        }
        cerr << "all " << n_threads << " threads started" << endl;
        for (auto&& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        cerr << "all " << n_threads << " threads stopped" << endl;
    }
    // now, read/write without a txn
    for (int i=0; i<n_threads; i++) {
        threads.emplace_back([n_docs, conn_str]() {
            try {
                cluster c(conn_str, "Administrator", "password");
                auto coll = c.bucket("default")->default_collection();
                read_write_no_txn(c, coll, n_docs);
            } catch (const std::runtime_error& e) {
                cerr << "ERROR " << e.what() << endl;
            }
        });
    }
    cerr << "all " << n_threads << " threads started" << endl;
    for (auto&& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    cerr << "all " << n_threads << " threads stopped" << endl;
    threads.clear();

    // now read/write with txn
    for (int i=0; i<n_threads; i++) {
        threads.emplace_back(std::thread([n_docs, conn_str, cleanup]() {
            try {
                cluster c(conn_str, "Administrator", "password");
                auto coll = c.bucket("default")->default_collection();
                read_write_in_txn(c, coll, n_docs, cleanup);
            } catch (const std::runtime_error& e) {
                cerr << "ERROR " << e.what() << endl;
            }
        }));
    }
    cerr << "all " << n_threads << " threads started" << endl;
    for (auto&& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }
    cerr << "all " << n_threads << " threads stopped" << endl;
}
