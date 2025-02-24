#pragma once

#include <nano/lib/numbers.hpp>
#include <nano/lib/utility.hpp>
#include <nano/node/fwd.hpp>
#include <nano/node/transport/transport.hpp>
#include <nano/node/vote_with_weight_info.hpp>

namespace nano
{
class node_observers final
{
public:
	using blocks_t = nano::observer_set<nano::election_status const &, std::vector<nano::vote_with_weight_info> const &, nano::account const &, nano::uint128_t const &, bool, bool>;
	blocks_t blocks; // Notification upon election completion or cancellation
	nano::observer_set<bool> wallet;
	nano::observer_set<std::shared_ptr<nano::vote>, std::shared_ptr<nano::transport::channel>, nano::vote_source, nano::vote_code> vote;
	nano::observer_set<nano::block_hash const &> active_started;
	nano::observer_set<nano::block_hash const &> active_stopped;
	nano::observer_set<nano::account const &, bool> account_balance;
	nano::observer_set<> disconnect;
	nano::observer_set<nano::root const &> work_cancel;
	nano::observer_set<nano::telemetry_data const &, std::shared_ptr<nano::transport::channel> const &> telemetry;
	nano::observer_set<std::shared_ptr<nano::transport::tcp_socket>> socket_connected;
	nano::observer_set<std::shared_ptr<nano::transport::channel>> channel_connected;

	nano::container_info container_info () const;
};
}
