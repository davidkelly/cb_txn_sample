#include <couchbase/client/bucket.hxx>
#include <couchbase/client/cluster.hxx>
#include <couchbase/transactions.hxx>
#include <iostream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using namespace couchbase;

struct MyData {
  std::string a_string;
  int an_int;
};

// for conversion to/from MyData when using our api
void to_json(nlohmann::json &json, const MyData &data) {
  nlohmann::json j{{"a_string", data.a_string}, {"an_int", data.an_int}};
  json = j;
}

// for conversion to/from MyData when using our api
void from_json(const nlohmann::json &json, MyData &data) {
  json.at("a_string").get_to(data.a_string);
  json.at("an_int").get_to(data.an_int);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    spdlog::info("You need to supply a connection string");
    return -1;
  }

  // defaults, but you can pass in overrides
  std::string user("Administrator");
  std::string pass("password");
  if (argc > 2) {
    user = argv[2];
  }
  if (argc > 3) {
    pass = argv[3];
  }

  // Lets connect to a cluster, get a bucket and a collection
  // (for now, txn only supports default_cluster)
  std::string connection_string(argv[1]);
  cluster cluster(connection_string, user, pass);
  auto bucket = cluster.bucket("default");
  auto coll = bucket->default_collection();
  spdlog::info("got collection");

  // Ok lets use the collection to upsert a bit of data
  MyData something{"foo", 3};
  auto result = coll->upsert("imarandomkey", something);
  spdlog::info("got result: {}", result);

  // Now, we need a transactions object...
  transactions::transaction_config config;
  transactions::transactions txns(cluster, config);

  // Finally, lets do a very simple transaction
  txns.run([&](transactions::attempt_context &ctx) {
    // The result is a transaction_document, which contains the content, and
    // the metadata (like the CAS)
    auto result = ctx.get(coll, "imarandomkey");
    MyData data = result.content<MyData>();
    data.an_int *= 2;
    // since we give the replace the transaction document, if someone changes
    // it after we read and before we write (only can happen outside a txn),
    // then it will rollback and retry
    auto replace_result = ctx.replace(coll, result, data);
    // result's ostream operator<< should work like this:
    // spdlog::info("replace result: {}", replace_result);
    // but doesn't, so
    spdlog::info("new CAS for doc: {}", replace_result);
  });
}
