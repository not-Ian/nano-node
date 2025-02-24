#include <nano/boost/asio/connect.hpp>
#include <nano/boost/asio/ip/tcp.hpp>
#include <nano/boost/asio/strand.hpp>
#include <nano/boost/beast/core/flat_buffer.hpp>
#include <nano/boost/beast/http.hpp>
#include <nano/boost/process/child.hpp>
#include <nano/lib/blocks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/signal_manager.hpp>
#include <nano/lib/thread_runner.hpp>
#include <nano/lib/threading.hpp>
#include <nano/lib/tomlconfig.hpp>
#include <nano/node/daemonconfig.hpp>
#include <nano/secure/utility.hpp>
#include <nano/test_common/testutil.hpp>

#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <csignal>
#include <future>
#include <iomanip>
#include <memory>
#include <random>

/* Boost v1.70 introduced breaking changes; the conditional compilation allows 1.6x to be supported as well. */
#if BOOST_VERSION < 107000
using socket_type = boost::asio::ip::tcp::socket;
#else
using socket_type = boost::asio::basic_stream_socket<boost::asio::ip::tcp, boost::asio::io_context::executor_type>;
#endif

namespace nano
{
void force_nano_dev_network ();
}

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

constexpr auto rpc_port_start = 60000;
constexpr auto peering_port_start = 61000;
constexpr auto ipc_port_start = 62000;

void write_config_files (std::filesystem::path const & data_path, int index)
{
	nano::network_params network_params{ nano::network_constants::active_network };
	nano::daemon_config daemon_config{ data_path, network_params };
	daemon_config.node.peering_port = peering_port_start + index;
	daemon_config.node.ipc_config.transport_tcp.enabled = true;
	daemon_config.node.ipc_config.transport_tcp.port = ipc_port_start + index;

	// Alternate use of memory pool
	daemon_config.node.use_memory_pools = (index % 2) == 0;

	// Write daemon config
	nano::tomlconfig toml;
	daemon_config.serialize_toml (toml);
	toml.write (nano::get_node_toml_config_path (data_path));

	nano::rpc_config rpc_config{ daemon_config.node.network_params.network };
	rpc_config.port = rpc_port_start + index;
	rpc_config.enable_control = true;
	rpc_config.rpc_process.ipc_port = ipc_port_start + index;

	// Write rpc config
	nano::tomlconfig toml_rpc;
	rpc_config.serialize_toml (toml_rpc);
	toml_rpc.write (nano::get_rpc_toml_config_path (data_path));
}

class account final
{
public:
	std::string private_key;
	std::string public_key;
	std::string as_string;
};

class account_info final
{
public:
	bool operator== (account_info const & other) const
	{
		return frontier == other.frontier && block_count == other.block_count && balance == other.balance && error == other.error;
	}

	std::string frontier;
	std::string block_count;
	std::string balance;
	bool error{ false };
};

class send_receive_impl;
class start_receive_session_impl;
class rpc_request_impl;

class start_receive_session_impl : public std::enable_shared_from_this<start_receive_session_impl>
{
private:
	socket_type socket;
	std::atomic<int> & send_calls_remaining;
	tcp::resolver::results_type const & results;

	std::string const wallet;
	std::string const source;
	std::string const destination;

	std::string const block;

	boost::beast::flat_buffer buffer;
	http::request<http::string_body> req;
	http::response<http::string_body> res;

public:
	start_receive_session_impl (
	boost::asio::io_context & io_ctx_a,
	tcp::resolver::results_type const & results_a,
	std::string const & wallet_a,
	std::string const & source_a,
	std::string const & destination_a,
	std::atomic<int> & send_calls_remaining_a,
	std::string const block_a) :
		socket{ io_ctx_a },
		send_calls_remaining{ send_calls_remaining_a },
		results{ results_a },
		wallet{ wallet_a },
		source{ source_a },
		destination{ destination_a },
		block{ std::move (block_a) }
	{
	}

	void start ()
	{
		async_connect ();
	}

private:
	void async_connect ()
	{
		boost::asio::async_connect (socket, results.cbegin (), results.cend (),
		[this_l = shared_from_this ()] (boost::system::error_code const & ec, tcp::resolver::iterator iterator) {
			this_l->request_receive ();
		});
	}

	void request_receive ()
	{
		boost::property_tree::ptree request;
		request.put ("action", "receive");
		request.put ("wallet", wallet);
		request.put ("account", destination);
		request.put ("block", block);
		std::stringstream ostream;
		boost::property_tree::write_json (ostream, request);

		req.method (http::verb::post);
		req.version (11);
		req.target ("/");
		req.body () = ostream.str ();
		req.prepare_payload ();

		async_write ();
	}

	void async_write ()
	{
		http::async_write (socket, req,
		[this_l = shared_from_this ()] (boost::system::error_code const & error_code, std::size_t bytes_transferred) {
			debug_assert (!error_code);
			debug_assert (bytes_transferred > 0);
			this_l->async_read ();
		});
	}

	void async_read ()
	{
		http::async_read (socket, buffer, res,
		[this_l = shared_from_this ()] (boost::system::error_code const & error_code, std::size_t bytes_transferred) {
			debug_assert (!error_code);
			debug_assert (bytes_transferred > 0);
			--this_l->send_calls_remaining;
			this_l->socket_shutdown ();
		});
	}

	void socket_shutdown ()
	{
		// Gracefully close the socket
		boost::system::error_code ec;
		socket.shutdown (tcp::socket::shutdown_both, ec);
		debug_assert (!ec || ec == boost::system::errc::not_connected);
	}
};

class send_receive_impl : public std::enable_shared_from_this<send_receive_impl>
{
private:
	boost::asio::io_context & io_ctx;
	socket_type socket;

	std::string const wallet;
	std::string const source;
	std::string const destination;

	std::atomic<int> & send_calls_remaining;
	tcp::resolver::results_type const results;

	boost::beast::flat_buffer buffer;
	http::request<http::string_body> req;
	http::response<http::string_body> res;

	std::shared_ptr<start_receive_session_impl> start_receive_session = nullptr;

public:
	send_receive_impl (
	boost::asio::io_context & io_ctx_a,
	std::string const & wallet_a,
	std::string const & source_a,
	std::string const & destination_a,
	std::atomic<int> & send_calls_remaining_a,
	tcp::resolver::results_type const & results_a) :
		io_ctx{ io_ctx_a },
		socket{ io_ctx },
		wallet{ wallet_a },
		source{ source_a },
		destination{ destination_a },
		send_calls_remaining{ send_calls_remaining_a },
		results{ results_a }
	{
	}

	void start ()
	{
		async_connect ();
	}

private:
	void async_connect ()
	{
		boost::asio::async_connect (socket, results.cbegin (), results.cend (),
		[this_l = shared_from_this ()] (boost::system::error_code const & ec, tcp::resolver::iterator iterator) {
			this_l->request_send ();
		});
	}

	void request_send ()
	{
		boost::property_tree::ptree request;
		request.put ("action", "send");
		request.put ("wallet", wallet);
		request.put ("source", source);
		request.put ("destination", destination);
		request.put ("amount", "1");
		std::stringstream ostream;
		boost::property_tree::write_json (ostream, request);

		req.method (http::verb::post);
		req.version (11);
		req.target ("/");
		req.body () = ostream.str ();
		req.prepare_payload ();

		async_write ();
	}

	void async_write ()
	{
		http::async_write (socket, req,
		[this_l = shared_from_this ()] (boost::system::error_code const & error_code, std::size_t bytes_transferred) {
			debug_assert (!error_code);
			debug_assert (bytes_transferred > 0);
			this_l->async_read ();
		});
	}

	void async_read ()
	{
		http::async_read (socket, buffer, res,
		[this_l = shared_from_this ()] (boost::system::error_code const & error_code, std::size_t bytes_transferred) {
			debug_assert (!error_code);
			debug_assert (bytes_transferred > 0);
			this_l->receive_start ();
			this_l->socket_shutdown ();
		});
	}

	void socket_shutdown ()
	{
		// Shut down send socket
		boost::system::error_code ec;
		socket.shutdown (tcp::socket::shutdown_both, ec);
		debug_assert (!ec || ec == boost::system::errc::not_connected);
	}

	void receive_start ()
	{
		boost::property_tree::ptree json;
		std::stringstream body (res.body ());
		boost::property_tree::read_json (body, json);
		auto block = json.get<std::string> ("block");

		start_receive_session = std::make_shared<start_receive_session_impl> (
		io_ctx, results, wallet, source, destination, send_calls_remaining, block);
		start_receive_session->start ();
	}
};

class rpc_request_impl : public std::enable_shared_from_this<rpc_request_impl>
{
private:
	boost::property_tree::ptree const request;
	boost::asio::io_context & ioc;
	tcp::resolver::results_type const results;
	socket_type socket;

	boost::beast::flat_buffer buffer;
	http::request<http::string_body> req;
	http::response<http::string_body> res;

	std::promise<boost::optional<boost::property_tree::ptree>> promise;

public:
	rpc_request_impl (
	boost::property_tree::ptree const & request_a,
	boost::asio::io_context & ioc_a,
	tcp::resolver::results_type const & results_a) :
		request{ request_a },
		ioc{ ioc_a },
		results{ results_a },
		socket{ ioc }
	{
		debug_assert (results.size () == 1);
	}

	void start ()
	{
		async_connect ();
	}

	boost::property_tree::ptree value_get ()
	{
		auto future = promise.get_future ();
		if (future.wait_for (std::chrono::seconds (5)) != std::future_status::ready)
		{
			throw std::runtime_error ("RPC request timed out");
		}
		auto response = future.get ();
		debug_assert (response.is_initialized ());
		return response.value_or (decltype (response)::argument_type{});
	}

private:
	void async_connect ()
	{
		boost::asio::async_connect (socket, results.cbegin (), results.cend (),
		[this_l = shared_from_this ()] (boost::system::error_code const & ec, tcp::resolver::iterator iterator) {
			this_l->request_do ();
		});
	}

	void request_do ()
	{
		std::stringstream ostream;
		boost::property_tree::write_json (ostream, request);

		req.method (http::verb::post);
		req.version (11);
		req.target ("/");
		req.body () = ostream.str ();
		req.prepare_payload ();

		async_write ();
	}

	void async_write ()
	{
		http::async_write (socket, req,
		[this_l = shared_from_this ()] (boost::system::error_code const & error_code, std::size_t bytes_transferred) {
			debug_assert (!error_code);
			debug_assert (bytes_transferred > 0);
			this_l->async_read ();
		});
	}

	void async_read ()
	{
		http::async_read (socket, buffer, res,
		[this_l = shared_from_this ()] (boost::system::error_code const & error_code, std::size_t bytes_transferred) {
			debug_assert (!error_code);
			debug_assert (bytes_transferred > 0);
			this_l->value_set ();
		});
	}

	void value_set ()
	{
		boost::property_tree::ptree json;
		std::stringstream body (res.body ());
		boost::property_tree::read_json (body, json);
		promise.set_value (json);
	}
};

boost::property_tree::ptree rpc_request (boost::property_tree::ptree const & request, boost::asio::io_context & ioc, tcp::resolver::results_type const & results)
{
	auto rpc_request = std::make_shared<rpc_request_impl> (request, ioc, results);
	boost::asio::strand<boost::asio::io_context::executor_type> strand{ ioc.get_executor () };
	boost::asio::post (strand,
	[rpc_request] () {
		rpc_request->start ();
	});
	return rpc_request->value_get ();
}

void keepalive_rpc (boost::asio::io_context & ioc, tcp::resolver::results_type const & results, uint16_t port)
{
	boost::property_tree::ptree request;
	request.put ("action", "keepalive");
	request.put ("address", "::1");
	request.put ("port", port);

	rpc_request (request, ioc, results);
}

account key_create_rpc (boost::asio::io_context & ioc, tcp::resolver::results_type const & results)
{
	boost::property_tree::ptree request;
	request.put ("action", "key_create");

	auto json = rpc_request (request, ioc, results);

	account account_l;
	account_l.private_key = json.get<std::string> ("private");
	account_l.public_key = json.get<std::string> ("public");
	account_l.as_string = json.get<std::string> ("account");

	return account_l;
}

std::string wallet_create_rpc (boost::asio::io_context & ioc, tcp::resolver::results_type const & results)
{
	boost::property_tree::ptree request;
	request.put ("action", "wallet_create");

	auto json = rpc_request (request, ioc, results);
	return json.get<std::string> ("wallet");
}

void wallet_add_rpc (boost::asio::io_context & ioc, tcp::resolver::results_type const & results, std::string const & wallet, std::string const & prv_key)
{
	boost::property_tree::ptree request;
	request.put ("action", "wallet_add");
	request.put ("wallet", wallet);
	request.put ("key", prv_key);
	rpc_request (request, ioc, results);
}

void stop_rpc (boost::asio::io_context & ioc, tcp::resolver::results_type const & results)
{
	boost::property_tree::ptree request;
	request.put ("action", "stop");
	rpc_request (request, ioc, results);
}

account_info account_info_rpc (boost::asio::io_context & ioc, tcp::resolver::results_type const & results, std::string const & account)
{
	boost::property_tree::ptree request;
	request.put ("action", "account_info");
	request.put ("account", account);

	account_info account_info;
	auto json = rpc_request (request, ioc, results);

	auto error = json.get_optional<std::string> ("error");
	if (error)
	{
		account_info.error = true;
	}
	else
	{
		account_info.balance = json.get<std::string> ("balance");
		account_info.block_count = json.get<std::string> ("block_count");
		account_info.frontier = json.get<std::string> ("frontier");
	}
	return account_info;
}

/** This launches a node and fires a lot of send/recieve RPC requests at it (configurable), then other nodes are tested to make sure they observe these blocks as well. */
int main (int argc, char * const * argv)
{
	nano::logger::initialize_for_tests (nano::log_config::tests_default ());
	nano::force_nano_dev_network ();

	boost::program_options::options_description description ("Command line options");

	// clang-format off
	description.add_options ()
		("help", "Print out options")
		("node_count,n", boost::program_options::value<int> ()->default_value (10), "The number of nodes to spin up")
		("send_count,s", boost::program_options::value<int> ()->default_value (2000), "How many send blocks to generate")
		("simultaneous_process_calls", boost::program_options::value<int> ()->default_value (20), "Number of simultaneous rpc sends to do")
		("destination_count", boost::program_options::value<int> ()->default_value (2), "How many destination accounts to choose between")
		("node_path", boost::program_options::value<std::string> (), "The path to the nano_node to test")
		("rpc_path", boost::program_options::value<std::string> (), "The path to the nano_rpc to test");
	// clang-format on

	boost::program_options::variables_map vm;
	try
	{
		boost::program_options::store (boost::program_options::parse_command_line (argc, argv, description), vm);
	}
	catch (boost::program_options::error const & err)
	{
		std::cerr << err.what () << std::endl;
		return 1;
	}
	boost::program_options::notify (vm);

	auto node_count = vm.find ("node_count")->second.as<int> ();
	auto destination_count = vm.find ("destination_count")->second.as<int> ();
	auto send_count = vm.find ("send_count")->second.as<int> ();
	auto simultaneous_process_calls = vm.find ("simultaneous_process_calls")->second.as<int> ();

	boost::system::error_code err;
	auto running_executable_filepath = boost::dll::program_location (err);

	auto node_path_it (vm.find ("node_path"));
	std::string node_path;
	if (node_path_it != vm.end ())
	{
		node_path = node_path_it->second.as<std::string> ();
	}
	else
	{
		auto node_filepath = running_executable_filepath.parent_path () / "nano_node";
		if (running_executable_filepath.has_extension ())
		{
			node_filepath.replace_extension (running_executable_filepath.extension ());
		}
		node_path = node_filepath.string ();
	}
	if (!std::filesystem::exists (node_path))
	{
		std::cerr << "nano_node executable could not be found in " << node_path << std::endl;
		return 1;
	}

	auto rpc_path_it (vm.find ("rpc_path"));
	std::string rpc_path;
	if (rpc_path_it != vm.end ())
	{
		rpc_path = rpc_path_it->second.as<std::string> ();
	}
	else
	{
		auto rpc_filepath = running_executable_filepath.parent_path () / "nano_rpc";
		if (running_executable_filepath.has_extension ())
		{
			rpc_filepath.replace_extension (running_executable_filepath.extension ());
		}
		rpc_path = rpc_filepath.string ();
	}
	if (!std::filesystem::exists (rpc_path))
	{
		std::cerr << "nano_rpc executable could not be found in " << rpc_path << std::endl;
		return 1;
	}

	std::vector<std::filesystem::path> data_paths;
	for (auto i = 0; i < node_count; ++i)
	{
		auto data_path = nano::unique_path ();
		std::filesystem::create_directory (data_path);
		write_config_files (data_path, i);
		data_paths.push_back (std::move (data_path));
	}

	std::string current_network{ nano::dev::network_params.network.get_current_network_as_string () };
	std::vector<std::unique_ptr<boost::process::child>> nodes;
	std::vector<std::unique_ptr<boost::process::child>> rpc_servers;
	for (auto const & data_path : data_paths)
	{
		nodes.emplace_back (std::make_unique<boost::process::child> (node_path, "--daemon", "--data_path", data_path.string (), "--network", current_network));
		rpc_servers.emplace_back (std::make_unique<boost::process::child> (rpc_path, "--daemon", "--data_path", data_path.string (), "--network", current_network));
	}

	std::cout << "Waiting for nodes to spin up..." << std::endl;
	std::this_thread::sleep_for (std::chrono::seconds (7));
	std::cout << "Connecting nodes..." << std::endl;

	std::shared_ptr<boost::asio::io_context> ioc_shared = std::make_shared<boost::asio::io_context> ();
	boost::asio::io_context & ioc{ *ioc_shared };

	nano::signal_manager sigman;

	auto signal_handler = [&ioc] (int signum) {
		ioc.stop ();
	};

	sigman.register_signal_handler (SIGINT, signal_handler, true);
	sigman.register_signal_handler (SIGTERM, signal_handler, false);

	tcp::resolver resolver{ ioc };
	auto const primary_node_results = resolver.resolve ("::1", std::to_string (rpc_port_start));

	std::thread t ([send_count, &ioc, &primary_node_results, &resolver, &node_count, &destination_count] () {
		for (int i = 0; i < node_count; ++i)
		{
			keepalive_rpc (ioc, primary_node_results, peering_port_start + i);
		}

		std::cout << "Beginning tests" << std::endl;

		// Create keys
		std::vector<account> destination_accounts;
		for (int i = 0; i < destination_count; ++i)
		{
			destination_accounts.emplace_back (key_create_rpc (ioc, primary_node_results));
		}

		// Create wallet
		std::string wallet = wallet_create_rpc (ioc, primary_node_results);

		// Add genesis account to it
		wallet_add_rpc (ioc, primary_node_results, wallet, nano::dev::genesis_key.prv.to_string ());

		// Add destination accounts
		for (auto & account : destination_accounts)
		{
			wallet_add_rpc (ioc, primary_node_results, wallet, account.private_key);
		}

		std::cout << "\rPrimary node processing transactions: 00%";

		std::random_device rd;
		std::mt19937 mt (rd ());
		std::uniform_int_distribution<size_t> dist (0, destination_accounts.size () - 1);

		std::atomic<int> send_calls_remaining{ send_count };
		for (auto i = 0; i < send_count; ++i)
		{
			account * destination_account;
			if (i < destination_accounts.size ())
			{
				destination_account = &destination_accounts[i];
			}
			else
			{
				auto random_account_index = dist (mt);
				destination_account = &destination_accounts[random_account_index];
			}

			// Send from genesis account to different accounts and receive the funds
			auto send_receive = std::make_shared<send_receive_impl> (ioc, wallet, nano::dev::genesis_key.pub.to_account (), destination_account->as_string, send_calls_remaining, primary_node_results);
			boost::asio::strand<boost::asio::io_context::executor_type> strand{ ioc.get_executor () };
			boost::asio::post (strand,
			[send_receive] () {
				send_receive->start ();
			});
		}

		while (send_calls_remaining != 0)
		{
			static int last_percent = 0;
			auto percent = static_cast<int> (100 * ((send_count - send_calls_remaining) / static_cast<double> (send_count)));

			if (last_percent != percent)
			{
				std::cout << "\rPrimary node processing transactions: " << std::setfill ('0') << std::setw (2) << percent << "%";
				last_percent = percent;
			}
		}

		std::cout << "\rPrimary node processed transactions                " << std::endl;

		std::cout << "Waiting for nodes to catch up..." << std::endl;

		std::map<std::string, account_info> known_account_info;
		for (int i = 0; i < destination_accounts.size (); ++i)
		{
			known_account_info.emplace (destination_accounts[i].as_string, account_info_rpc (ioc, primary_node_results, destination_accounts[i].as_string));
		}

		nano::timer<std::chrono::milliseconds> timer;
		timer.start ();

		for (int i = 1; i < node_count; ++i)
		{
			auto const results = resolver.resolve ("::1", std::to_string (rpc_port_start + i));
			for (auto & account_info : known_account_info)
			{
				while (true)
				{
					auto other_account_info = account_info_rpc (ioc, results, account_info.first);
					if (!other_account_info.error && account_info.second == other_account_info)
					{
						// Found the account in this node
						break;
					}

					if (timer.since_start () > std::chrono::seconds (120))
					{
						throw std::runtime_error ("Timed out");
					}

					std::this_thread::sleep_for (std::chrono::seconds (1));
				}
			}

			stop_rpc (ioc, results);
		}

		// Stop main node
		stop_rpc (ioc, primary_node_results);
	});

	nano::thread_runner runner (ioc_shared, nano::default_logger (), simultaneous_process_calls);
	t.join ();
	runner.join ();

	for (auto & node : nodes)
	{
		node->wait ();
	}
	for (auto & rpc_server : rpc_servers)
	{
		rpc_server->wait ();
	}

	std::cout << "Done!" << std::endl;
}
