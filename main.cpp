#include "SharedState.h"
#include "WorkerThread.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

// Define the global variables here so the linker can find them
std::queue<std::string> tx_queue;  
std::mutex queue_lock;             

int main() {
    try {
        std::thread worker(consumer_thread);
        worker.detach();

        std::string host = "eth-mainnet.g.alchemy.com", port = "443";
        // INSERT YOUR ALCHEMY KEY HERE
        std::string path = "/v2/YOUR_ALCHEMY_KEY_HERE"; 
        
        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        tcp::resolver resolver{ioc};
        websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};

        auto const results = resolver.resolve(host, port);
        net::connect(get_lowest_layer(ws), results);
        SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str());
        ws.next_layer().handshake(ssl::stream_base::client);
        ws.set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
            req.set(http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " mempool-monitor");
        }));
        ws.handshake(host, path);

        json sub_msg = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "eth_subscribe"}, {"params", {"newPendingTransactions"}}};
        ws.write(net::buffer(sub_msg.dump()));
        
        std::cout << "[+] Engine Online! Multithreaded scanning initiated...\n";

        while(true) {
            beast::flat_buffer buffer;
            ws.read(buffer);
            std::string raw_data = beast::buffers_to_string(buffer.data());
            json parsed = json::parse(raw_data);
            
            if (parsed.contains("method") && parsed["method"] == "eth_subscription") {
                std::string tx_hash = parsed["params"]["result"];
                {
                    std::lock_guard<std::mutex> lock(queue_lock);
                    tx_queue.push(tx_hash);
                }
            }
        }
    } catch(std::exception const& e) {
        std::cerr << "Main Error: " << e.what() << std::endl;
    }
    return 0;
}