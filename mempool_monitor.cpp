#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <iomanip>
#include <string>
#include <cctype>
#include <map>
#include <thread>
#include <mutex>
#include <queue>
#include <chrono>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

// --- MULTITHREADING GLOBALS ---
std::queue<std::string> tx_queue;  
std::mutex queue_lock;             

// --- CONSUMER: Background Worker ---
void consumer_thread() {
    try {
        // Worker gets its own private connection to Alchemy so it doesn't collide with the main stream
        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        tcp::resolver resolver{ioc};
        websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};

        auto const results = resolver.resolve("eth-mainnet.g.alchemy.com", "443");
        net::connect(get_lowest_layer(ws), results);
        SSL_set_tlsext_host_name(ws.next_layer().native_handle(), "eth-mainnet.g.alchemy.com");
        ws.next_layer().handshake(ssl::stream_base::client);
        ws.handshake("eth-mainnet.g.alchemy.com", "/v2/WXVextenuyecpevTAhdpO");

        while(true) {
            std::string tx_hash = "";
            
            // Safely grab a hash from the line
            {
                std::lock_guard<std::mutex> lock(queue_lock);
                if(!tx_queue.empty()) {
                    tx_hash = tx_queue.front();
                    tx_queue.pop();
                }
            }

            // If queue is empty, take a tiny 10ms nap to save CPU, then check again
            if(tx_hash.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            // Ask for transaction details
            json tx_req = {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "eth_getTransactionByHash"}, {"params", {tx_hash}}};
            ws.write(net::buffer(tx_req.dump()));

            beast::flat_buffer tx_buffer;
            ws.read(tx_buffer);
            json tx_details = json::parse(beast::buffers_to_string(tx_buffer.data()));

            if (tx_details.contains("result") && !tx_details["result"].is_null() && !tx_details["result"]["to"].is_null()) {
                std::string to_address = tx_details["result"]["to"];
                std::string uniswap_v2 = "0x7a250d5630b4cf539739df2c5dacb4c659f2488d";
                std::string uniswap_universal = "0x3fc91a3afd70395cd496c647d5a6cc9d4b2b7fad";

                for(auto& c : to_address) { c = std::tolower(c); }

                if (to_address == uniswap_v2 || to_address == uniswap_universal) {
                    std::string gas_hex = "0x0";
                    if (!tx_details["result"]["gasPrice"].is_null()) gas_hex = tx_details["result"]["gasPrice"];
                    
                    uint64_t gas_decimal = 0;
                    if(gas_hex.length() > 2) gas_decimal = std::stoull(gas_hex.substr(2), nullptr, 16);

                    std::string input_data = "None", method_id = "None";
                    if (!tx_details["result"]["input"].is_null() && tx_details["result"]["input"].get<std::string>().length() >= 10) {
                        input_data = tx_details["result"]["input"];
                        method_id = input_data.substr(0, 10);
                    }

                    std::map<std::string, std::string> known_methods = {
                        {"0x7ff36ab5", "swapExactETHForTokens"},
                        {"0x38ed1739", "swapExactTokensForTokens"},
                        {"0x18cbafe5", "swapExactTokensForETH"},
                        {"0xb6f9de95", "swapExactETHForTokensSupportingFee"},
                        {"0x3593564c", "execute (Universal Router)"}
                    };

                    std::string method_name = "Unknown (" + method_id + ")";
                    if (known_methods.count(method_id)) method_name = known_methods[method_id];

                    std::cout << "\n\n[!] UNISWAP TRADE DETECTED [!]\n";
                    std::cout << "    Hash:        " << tx_hash << "\n";
                    std::cout << "    Gas (Wei):   " << gas_decimal << "\n";
                    std::cout << "    Action:      " << method_name << "\n";

// --- HEX SLICING LOGIC ---
                    if (method_id == "0x7ff36ab5" && input_data.length() >= 458) {
                        // swapExactETHForTokens
                        std::string target_token = "0x" + input_data.substr(418, 40);
                        
                        std::string eth_spent_hex = "0x0";
                        if (!tx_details["result"]["value"].is_null()) {
                            eth_spent_hex = tx_details["result"]["value"];
                        }
                        
                        std::string min_tokens_raw = input_data.substr(10, 64);
                        min_tokens_raw.erase(0, std::min(min_tokens_raw.find_first_not_of('0'), min_tokens_raw.size() - 1));
                        std::string min_tokens_hex = "0x" + min_tokens_raw;

                        double eth_spent = 0.0, min_tokens = 0.0;
                        try { 
                            eth_spent = std::stod(eth_spent_hex) / 1e18;
                            min_tokens = std::stod(min_tokens_hex) / 1e18;
                        } catch(...) {}

                        std::cout << "    Target:      " << target_token << " (Token being bought)\n";
                        std::cout << "    ETH Spent:   " << std::fixed << std::setprecision(6) << eth_spent << " ETH\n";
                        std::cout << "    Min Tokens:  " << std::fixed << std::setprecision(2) << min_tokens << "\n";

                    } else if (method_id == "0x38ed1739" && input_data.length() >= 522) {
                        // swapExactTokensForTokens (Token to Token swap)
                        // path[0] is at offset 418, path[1] is at offset 482
                        std::string token_selling = "0x" + input_data.substr(418, 40);
                        std::string token_buying = "0x" + input_data.substr(482, 40);
                        
                        std::cout << "    Selling:     " << token_selling << "\n";
                        std::cout << "    Buying:      " << token_buying << "\n";

                    } else if (method_id == "0x3593564c") {
                        // Universal Router
                        std::cout << "    Note:        Universal Router payload detected. Deep parsing bypassed for V1.0.\n";
                    }
                    
                    std::cout << "--------------------------------------\n";
                }
            }
        }
    } catch(std::exception const& e) {
        std::cerr << "Worker Error: " << e.what() << std::endl;
    }
}

// --- PRODUCER: Main Stream ---
int main() {
    try {
        // Fire up the background worker thread
        std::thread worker(consumer_thread);
        worker.detach();

        std::string host = "eth-mainnet.g.alchemy.com", port = "443", path = "/v2/WXVextenuyecpevTAhdpO";
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
                
                // Toss the hash to the worker thread and instantly go back to listening
                {
                    std::lock_guard<std::mutex> lock(queue_lock);
                    tx_queue.push(tx_hash);
                }
                std::cout << "." << std::flush;
            }
        }
    } catch(std::exception const& e) {
        std::cerr << "Main Error: " << e.what() << std::endl;
    }
    return 0;
}