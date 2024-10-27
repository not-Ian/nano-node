#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/logging.hpp>
#include <nano/lib/numbers.hpp>

#include <cstdint>
#include <future>

namespace nano
{
enum class epoch : uint8_t;
class network_params;
class node;
class ledger;
}
namespace nano::store
{
class component;
}

namespace nano
{
class epoch_upgrader final
{
public:
	epoch_upgrader (nano::node &, nano::ledger &, nano::store::component &, nano::network_params &, nano::logger &);

	bool start (nano::raw_key const & prv, nano::epoch epoch, uint64_t count_limit, uint64_t threads);
	void stop ();

private: // Dependencies
	nano::node & node;
	nano::ledger & ledger;
	nano::store::component & store;
	nano::network_params & network_params;
	nano::logger & logger;

private:
	void upgrade_impl (nano::raw_key const & prv, nano::epoch epoch, uint64_t count_limit, uint64_t threads);

	std::atomic<bool> stopped{ false };
	nano::locked<std::future<void>> epoch_upgrading;
};
}
