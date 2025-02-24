#pragma once

#include <nano/lib/locks.hpp>
#include <nano/lib/numbers.hpp>
#include <nano/node/fwd.hpp>

#include <boost/optional.hpp>

#include <deque>
#include <memory>
#include <mutex>

namespace nano::scheduler
{
class buckets;

class manual final
{
	std::deque<std::tuple<std::shared_ptr<nano::block>, boost::optional<nano::uint128_t>, nano::election_behavior>> queue;
	nano::node & node;
	mutable nano::mutex mutex;
	nano::condition_variable condition;
	bool stopped{ false };
	std::thread thread;
	void notify ();
	bool predicate () const;
	void run ();

public:
	explicit manual (nano::node & node);
	~manual ();

	void start ();
	void stop ();

	// Manually start an election for a block
	// Call action with confirmed block, may be different than what we started with
	void push (std::shared_ptr<nano::block> const &, boost::optional<nano::uint128_t> const & = boost::none);

	bool contains (nano::block_hash const &) const;

	nano::container_info container_info () const;
};
}
