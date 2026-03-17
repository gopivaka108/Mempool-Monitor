#include "WorkerThread.h"
#include "SharedState.h"
#include "AnvilSimulator.h"
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
#include <cctype>
#include <map>
#include <chrono>
#include <thread>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

void consumer_thread() {
    try {
        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        tcp::resolver resolver{ioc};
        websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};

        auto const results = resolver.resolve("eth-mainnet.g.alchemy.com", "443");
        net::connect(get_lowest_layer(ws), results);
        SSL_set_tlsext_host_name(ws.next_layer().native_handle(), "eth-mainnet.g.alchemy.com");
        ws.next_layer().handshake(ssl::stream_base::client);
        
        // INSERT YOUR ALCHEMY KEY HERE
        ws.handshake("eth-mainnet.g.alchemy.com", "/v2/YOUR_ALCHEMY_KEY_HERE");

        while(true) {
            std::string tx_hash = "";
            
            {
                std::lock_guard<std::mutex> lock(queue_lock);
                if(!tx_queue.empty()) {
                    tx_hash = tx_queue.front();
                    tx_queue.pop();
                }
            }

            if(tx_hash.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

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

                    if (method_id == "0x7ff36ab5" && input_data.length() >= 458) {
                        std::string target_token = "0x" + input_data.substr(418, 40);
                        
                        std::string eth_spent_hex = "0x0";
                        if (!tx_details["result"]["value"].is_null()) {
                            eth_spent_hex = tx_details["result"]["value"];
                        }
                        
                        std::string min_tokens_raw = input_data.substr(10, 64);
                        min_tokens_raw.erase(0, std::min(min_tokens_raw.find_first_not_of('0'), min_tokens_raw.size() - 1));
                        std::string min_tokens_hex = "0x" + min_tokens_raw;

                        double eth_spent = 0.0, min_tokens = 0.0, implied_price = 0.0;
                        try { 
                            eth_spent = std::stod(eth_spent_hex) / 1e18;
                            min_tokens = std::stod(min_tokens_hex) / 1e18;
                            if (min_tokens > 0) implied_price = eth_spent / min_tokens;
                        } catch(...) {}

                        std::cout << "    Target:      " << target_token << "\n";
                        std::cout << "    ETH Spent:   " << std::fixed << std::setprecision(6) << eth_spent << " ETH\n";
                        std::cout << "    Min Tokens:  " << std::fixed << std::setprecision(2) << min_tokens << "\n";
                        std::cout << "    Max Price:   " << std::fixed << std::setprecision(8) << implied_price << " ETH/Token\n";

                        std::string from_addr = tx_details["result"]["from"];
                        std::string input_hex = tx_details["result"]["input"];
                        
                        simulate_trade(from_addr, to_address, eth_spent_hex, input_hex);

                        if (eth_spent >= 0.5) {
                            std::cout << "    [$$$] WHALE DETECTED: PRIME MEV SANDWICH TARGET [$$$]\n";
                        }
                    } else if (method_id == "0x38ed1739" && input_data.length() >= 522) {
                        std::string token_selling = "0x" + input_data.substr(418, 40);
                        std::string token_buying = "0x" + input_data.substr(482, 40);
                        std::cout << "    Selling:     " << token_selling << "\n";
                        std::cout << "    Buying:      " << token_buying << "\n";
                    } else if (method_id == "0x3593564c") {
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